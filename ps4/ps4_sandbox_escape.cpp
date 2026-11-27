// PS4 USB sandbox escape for Caliza.
//
// The kernel-access portions of this file are a reduced, modified extraction from
// ps4-payload-sdk's crt/kernel.c and crt/syscall.h:
//   Copyright (C) 2025 John Törnblom
//   https://github.com/ps4-payload-dev/sdk
// They are used under the GNU General Public License version 3 or later, consistently
// with this PCSX2-derived package. The nullfs-in-sandbox arrangement follows the
// public ps4-libjbc technique by sleirsgoevy, reimplemented here against the GPLed
// kernel-access layer above rather than copying libjbc's implementation.
//
// This is deliberately narrow: the fixed kernel offsets below are only used for
// firmware 14.00.x, the firmware the current PS4 test console reports. Other
// firmwares fail closed instead of writing kernel memory at guessed addresses.

#include "ps4_sandbox_escape.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
constexpr long SYS_KEXEC = 11;
constexpr long SYS_MKDIR = 136;
constexpr long SYS_GETCWD = 326;
constexpr long SYS_NMOUNT = 378;
constexpr long SYS_STATFS = 396;
constexpr long SYS_SYSCTL = 202;

constexpr uint32_t MSR_LSTAR = 0xC0000082;
constexpr uint64_t X86_CR0_WP = 1ull << 16;

constexpr uintptr_t OFF_PROC_UCRED = 0x40;
constexpr uintptr_t OFF_PROC_FD = 0x48;
constexpr uintptr_t OFF_PROC_PID = 0xb0;
constexpr uintptr_t OFF_UCRED_PRISON = 0x30;
constexpr uintptr_t OFF_UCRED_AUTHID = 0x58;
constexpr uintptr_t OFF_UCRED_SONYCRED = 0x60;
constexpr uintptr_t OFF_UCRED_PROCCAP = 0x68;
constexpr uintptr_t OFF_FILEDESC_CDIR = 0x10;
constexpr uintptr_t OFF_FILEDESC_RDIR = 0x18;
constexpr uintptr_t OFF_FILEDESC_JDIR = 0x20;

struct thread;

struct KexecArgs
{
  int (*fn)(thread*, KexecArgs*);
  long arg1;
  long arg2;
  long arg3;
  long arg4;
  long arg5;
};

struct Iovec
{
  const void* base;
  uint64_t len;
};

// OpenOrbis' sys/mount.h is a Linux-like subset and does not expose FreeBSD statfs.
// This is the userland layout Lodestar/ps4-payload-sdk uses on the console.
struct OrbisStatfs
{
  uint32_t f_version;
  uint32_t f_type;
  uint64_t f_flags;
  uint64_t f_bsize;
  uint64_t f_iosize;
  uint64_t f_blocks;
  uint64_t f_bfree;
  int64_t f_bavail;
  uint64_t f_files;
  int64_t f_ffree;
  uint64_t f_syncwrites;
  uint64_t f_asyncwrites;
  uint64_t f_syncreads;
  uint64_t f_asyncreads;
  uint64_t f_spare[10];
  uint32_t f_namemax;
  uint32_t f_owner;
  int32_t f_fsid[2];
  char f_charspare[80];
  char f_fstypename[16];
  char f_mntfromname[88];
  char f_mntonname[88];
};

struct CredentialSnapshot
{
  uint32_t uid = 0;
  uint32_t ruid = 0;
  uint32_t svuid = 0;
  uint32_t rgid = 0;
  uint32_t svgid = 0;
  uint64_t prison = 0;
  uint64_t authid = 0;
  uint64_t sony_cred = 0;
  uint64_t proc_cap = 0;
  uint64_t cdir = 0;
  uint64_t rdir = 0;
  uint64_t jdir = 0;
};

static inline long ps4_syscall6(long n, long a1 = 0, long a2 = 0, long a3 = 0, long a4 = 0, long a5 = 0, long a6 = 0)
{
  unsigned long ret;
  char err;
  register long r10 asm("r10") = a4;
  register long r8 asm("r8") = a5;
  register long r9 asm("r9") = a6;
  asm volatile("syscall"
               : "=a"(ret), "=@ccc"(err), "+r"(r10), "+r"(r8), "+r"(r9)
               : "a"(n), "D"(a1), "S"(a2), "d"(a3)
               : "rcx", "r11", "memory");
  return err ? -(long)ret : (long)ret;
}

static int kernel_kexec(int (*fn)(thread*, KexecArgs*), long a1 = 0, long a2 = 0, long a3 = 0, long a4 = 0, long a5 = 0)
{
  const long rc = ps4_syscall6(SYS_KEXEC, (long)fn, a1, a2, a3, a4, a5);
  return rc < 0 ? (int)rc : (int)rc;
}

static int kexec_get_msr(thread*, KexecArgs* args)
{
  const uint32_t reg = (uint32_t)args->arg1;
  uint64_t* value = (uint64_t*)args->arg2;
  uint32_t edx = 0, eax = 0;
  if (!value)
    return 22;
  asm volatile(".att_syntax\n\trdmsr" : "=d"(edx), "=a"(eax) : "c"(reg));
  *value = ((uint64_t)edx << 32) | eax;
  return 0;
}

static int kexec_get_thread(thread* td, KexecArgs* args)
{
  uint64_t* value = (uint64_t*)args->arg1;
  if (!value)
    return 22;
  *value = (uint64_t)td;
  return 0;
}

using KernelCopyFn = int (*)(const void*, void*, unsigned long);

static int kexec_copyin(thread*, KexecArgs* args)
{
  KernelCopyFn copyin = (KernelCopyFn)args->arg1;
  const void* user = (const void*)args->arg2;
  void* kernel = (void*)args->arg3;
  const unsigned long length = (unsigned long)args->arg4;
  unsigned long cr0;
  asm volatile(".att_syntax\n\tmov %%cr0, %0" : "=r"(cr0));
  asm volatile(".att_syntax\n\tmov %0, %%cr0" ::"r"(cr0 & ~X86_CR0_WP));
  const int rc = copyin(user, kernel, length);
  asm volatile(".att_syntax\n\tmov %0, %%cr0" ::"r"(cr0));
  return rc;
}

static int kexec_copyout(thread*, KexecArgs* args)
{
  KernelCopyFn copyout = (KernelCopyFn)args->arg1;
  const void* kernel = (const void*)args->arg2;
  void* user = (void*)args->arg3;
  const unsigned long length = (unsigned long)args->arg4;
  return copyout(kernel, user, length);
}

struct KernelAccess
{
  bool ready = false;
  int init_error = 0;
  uint32_t firmware = 0;
  uint64_t image_base = 0;
  uint64_t copyin = 0;
  uint64_t copyout = 0;
};

KernelAccess g_kernel;

static uint32_t ps4_firmware_version()
{
  int mib[2] = {1, 38};
  uint32_t version = 0;
  unsigned long size = sizeof(version);
  if (ps4_syscall6(SYS_SYSCTL, (long)mib, 2, (long)&version, (long)&size, 0, 0) < 0)
    return 0;
  return version;
}

static int kernel_copyout(uint64_t kernel, void* user, uint64_t length)
{
  return kernel_kexec(kexec_copyout, (long)g_kernel.copyout, (long)kernel, (long)user, (long)length);
}

static int kernel_copyin(const void* user, uint64_t kernel, uint64_t length)
{
  return kernel_kexec(kexec_copyin, (long)g_kernel.copyin, (long)user, (long)kernel, (long)length);
}

static bool kernel_read64(uint64_t address, uint64_t* value)
{
  *value = 0;
  return kernel_copyout(address, value, sizeof(*value)) == 0;
}

static bool kernel_write64(uint64_t address, uint64_t value)
{
  return kernel_copyin(&value, address, sizeof(value)) == 0;
}

static bool kernel_read32(uint64_t address, uint32_t* value)
{
  *value = 0;
  return kernel_copyout(address, value, sizeof(*value)) == 0;
}

static bool kernel_write32(uint64_t address, uint32_t value)
{
  return kernel_copyin(&value, address, sizeof(value)) == 0;
}

static bool kernel_pointer_plausible(uint64_t address)
{
  return address >= 0xffff800000000000ull && address < 0xfffffffffffff000ull;
}

static int kernel_access_init()
{
  if (g_kernel.ready)
    return 0;
  if (g_kernel.init_error)
    return g_kernel.init_error;

  const uint32_t fw = ps4_firmware_version();
  g_kernel.firmware = fw;
  if ((fw & 0xffff0000u) != 0x14000000u)
  {
    g_kernel.init_error = -78;
    return g_kernel.init_error;
  }

  uint64_t lstar = 0;
  int rc = kernel_kexec(kexec_get_msr, MSR_LSTAR, (long)&lstar);
  if (rc != 0 || !kernel_pointer_plausible(lstar))
  {
    g_kernel.init_error = rc ? rc : -14;
    return g_kernel.init_error;
  }

  // ps4-payload-sdk's firmware-14.00 layout. LSTAR is the kernel's syscall entry;
  // these offsets were published for the 14.00 kernel image and are not extrapolated.
  g_kernel.image_base = lstar - 0x1c0;
  g_kernel.copyin = g_kernel.image_base + 0x002bda40;
  g_kernel.copyout = g_kernel.image_base + 0x002bd950;
  g_kernel.ready = true;
  return 0;
}

static bool current_process(uint64_t* proc)
{
  uint64_t td = 0;
  if (kernel_kexec(kexec_get_thread, (long)&td) != 0 || !kernel_pointer_plausible(td))
    return false;
  return kernel_read64(td + 8, proc) && kernel_pointer_plausible(*proc);
}

static bool find_pid1_process(uint64_t* result)
{
  uint64_t proc = 0;
  if (!current_process(&proc))
    return false;

  // LIST_ENTRY(proc): offset 0 is le_next. Validate each step through the next node's
  // le_prev before following it, as libjbc does; never follow a userland or null pointer.
  for (int depth = 0; depth < 256; depth++)
  {
    uint32_t pid = 0;
    if (!kernel_read32(proc + OFF_PROC_PID, &pid))
      return false;
    if (pid == 1)
    {
      *result = proc;
      return true;
    }
    uint64_t next = 0;
    if (!kernel_read64(proc, &next) || !kernel_pointer_plausible(next))
      return false;
    uint64_t next_prev = 0;
    if (!kernel_read64(next + 8, &next_prev) || next_prev != proc)
      return false;
    proc = next;
  }
  return false;
}

static bool read_process_context(uint64_t proc, uint64_t* ucred, uint64_t* filedesc)
{
  return kernel_read64(proc + OFF_PROC_UCRED, ucred) && kernel_pointer_plausible(*ucred) &&
         kernel_read64(proc + OFF_PROC_FD, filedesc) && kernel_pointer_plausible(*filedesc);
}

static bool read_credentials(uint64_t ucred, uint64_t filedesc, CredentialSnapshot* out)
{
  return kernel_read32(ucred + 0x04, &out->uid) && kernel_read32(ucred + 0x08, &out->ruid) &&
         kernel_read32(ucred + 0x0c, &out->svuid) && kernel_read32(ucred + 0x14, &out->rgid) &&
         kernel_read32(ucred + 0x18, &out->svgid) && kernel_read64(ucred + OFF_UCRED_PRISON, &out->prison) &&
         kernel_read64(ucred + OFF_UCRED_AUTHID, &out->authid) && kernel_read64(ucred + OFF_UCRED_SONYCRED, &out->sony_cred) &&
         kernel_read64(ucred + OFF_UCRED_PROCCAP, &out->proc_cap) && kernel_read64(filedesc + OFF_FILEDESC_CDIR, &out->cdir) &&
         kernel_read64(filedesc + OFF_FILEDESC_RDIR, &out->rdir) && kernel_read64(filedesc + OFF_FILEDESC_JDIR, &out->jdir);
}

static bool write_credentials(uint64_t ucred, uint64_t filedesc, const CredentialSnapshot& cred)
{
  return kernel_write32(ucred + 0x04, cred.uid) && kernel_write32(ucred + 0x08, cred.ruid) &&
         kernel_write32(ucred + 0x0c, cred.svuid) && kernel_write32(ucred + 0x14, cred.rgid) &&
         kernel_write32(ucred + 0x18, cred.svgid) && kernel_write64(ucred + OFF_UCRED_PRISON, cred.prison) &&
         kernel_write64(ucred + OFF_UCRED_AUTHID, cred.authid) && kernel_write64(ucred + OFF_UCRED_SONYCRED, cred.sony_cred) &&
         kernel_write64(ucred + OFF_UCRED_PROCCAP, cred.proc_cap) && kernel_write64(filedesc + OFF_FILEDESC_CDIR, cred.cdir) &&
         kernel_write64(filedesc + OFF_FILEDESC_RDIR, cred.rdir) && kernel_write64(filedesc + OFF_FILEDESC_JDIR, cred.jdir);
}

static int count_visible_entries(const char* path)
{
  DIR* dir = opendir(path);
  if (!dir)
    return -1;
  int count = 0;
  while (dirent* entry = readdir(dir))
  {
    if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0)
      count++;
  }
  closedir(dir);
  return count;
}

static long sys_mkdir(const char* path, int mode)
{
  return ps4_syscall6(SYS_MKDIR, (long)path, mode);
}

static long sys_getcwd(char* path, uint64_t size)
{
  return ps4_syscall6(SYS_GETCWD, (long)path, (long)size);
}

static long sys_nmount(Iovec* iov, unsigned int count, int flags)
{
  return ps4_syscall6(SYS_NMOUNT, (long)iov, count, flags);
}

static long sys_statfs(const char* path, OrbisStatfs* out)
{
  return ps4_syscall6(SYS_STATFS, (long)path, (long)out);
}

static bool nmount_usb0_into_sandbox(std::string* detail)
{
  char cwd[1024] = {};
  const long cwd_rc = sys_getcwd(cwd, sizeof(cwd) - 1);
  if (cwd_rc < 0 || cwd[0] != '/')
  {
    *detail = "getcwd while elevated failed rc=" + std::to_string(cwd_rc);
    return false;
  }
  if (std::strcmp(cwd, "/") == 0)
  {
    *detail = "sandbox backing cwd resolved as /, refusing an ambiguous nullfs target";
    return false;
  }

  std::string mountpoint = cwd;
  if (!mountpoint.empty() && mountpoint.back() != '/')
    mountpoint += '/';
  mountpoint += "mnt/usb0";

  std::string mnt_parent = cwd;
  if (!mnt_parent.empty() && mnt_parent.back() != '/')
    mnt_parent += '/';
  mnt_parent += "mnt";
  long mkdir_rc = sys_mkdir(mnt_parent.c_str(), 0777);
  if (mkdir_rc < 0 && mkdir_rc != -17)
  {
    *detail = "mkdir " + mnt_parent + " failed rc=" + std::to_string(mkdir_rc);
    return false;
  }
  mkdir_rc = sys_mkdir(mountpoint.c_str(), 0777);
  if (mkdir_rc < 0 && mkdir_rc != -17)
  {
    *detail = "mkdir " + mountpoint + " failed rc=" + std::to_string(mkdir_rc);
    return false;
  }

  static constexpr char source[] = "/mnt/usb0";
  Iovec iov[] = {
    {"fstype", 7},
    {"nullfs", 7},
    {"from", 5},
    {source, sizeof(source)},
    {"fspath", 7},
    {mountpoint.c_str(), mountpoint.size() + 1},
  };
  const long mount_rc = sys_nmount(iov, 6, 0);
  if (mount_rc < 0 && mount_rc != -16) // EBUSY: an earlier launch may have left the intended layer mounted
  {
    *detail = "nullfs /mnt/usb0 -> " + mountpoint + " failed rc=" + std::to_string(mount_rc);
    return false;
  }

  OrbisStatfs layer = {};
  if (sys_statfs(mountpoint.c_str(), &layer) != 0 || std::strcmp(layer.f_mntonname, mountpoint.c_str()) != 0 ||
      std::strcmp(layer.f_fstypename, "nullfs") != 0 || std::strcmp(layer.f_mntfromname, source) != 0)
  {
    *detail = "nullfs /mnt/usb0 -> " + mountpoint +
      (mount_rc == -16 ? " was busy but did not verify as the exact USB layer" : " did not verify after nmount");
    return false;
  }

  *detail = "nullfs /mnt/usb0 -> " + mountpoint;
  return true;
}

} // namespace

OrbisPs4UsbMountResult OrbisPs4MountUsb0Direct()
{
  OrbisPs4UsbMountResult result;
  result.visible_entries_before = count_visible_entries("/mnt/usb0");
  if (result.visible_entries_before > 0)
  {
    result.already_visible = true;
    result.mounted = true;
    result.detail = "/mnt/usb0 is already visible in this process";
    return result;
  }

  result.attempted = true;
  const int init_rc = kernel_access_init();
  if (init_rc != 0 || !g_kernel.ready)
  {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "kernel access unavailable fw=0x%08x rc=%d (only firmware 14.00.x is enabled)", g_kernel.firmware, init_rc);
    result.detail = buf;
    return result;
  }

  uint64_t pid1 = 0, proc = 0, pid1_ucred = 0, pid1_fd = 0, ucred = 0, filedesc = 0;
  uint64_t prison0 = 0, rootvnode = 0;
  CredentialSnapshot saved;
  if (!find_pid1_process(&pid1) || !read_process_context(pid1, &pid1_ucred, &pid1_fd) || !current_process(&proc) ||
      !read_process_context(proc, &ucred, &filedesc) || !kernel_read64(pid1_ucred + OFF_UCRED_PRISON, &prison0) ||
      !kernel_read64(pid1_fd + OFF_FILEDESC_RDIR, &rootvnode) || !kernel_pointer_plausible(prison0) ||
      !kernel_pointer_plausible(rootvnode) || !read_credentials(ucred, filedesc, &saved))
  {
    result.detail = "could not resolve or read the process credential/vnode state";
    return result;
  }

  CredentialSnapshot elevated = saved;
  elevated.uid = elevated.ruid = elevated.svuid = 0;
  elevated.rgid = elevated.svgid = 0;
  elevated.prison = prison0;
  elevated.authid = 0x3801000000000013ull;
  elevated.sony_cred = 0xffffffffffffffffull;
  elevated.proc_cap = 0xffffffffffffffffull;
  elevated.cdir = saved.rdir; // the sandbox backing root, so getcwd names its global path
  elevated.rdir = rootvnode;
  elevated.jdir = rootvnode;

  if (!write_credentials(ucred, filedesc, elevated))
  {
    result.detail = "could not install temporary elevated credentials";
    return result;
  }

  std::string mount_detail;
  const bool mounted = nmount_usb0_into_sandbox(&mount_detail);
  const bool restored = write_credentials(ucred, filedesc, saved);

  result.mounted = mounted;
  result.visible_entries_after = count_visible_entries("/mnt/usb0");
  result.detail = mount_detail;
  if (!restored)
    result.detail += "; WARNING: restoring the original credentials failed";
  if (!mounted)
    return result;

  char buf[240];
  std::snprintf(buf, sizeof(buf), "%s; entries before/after=%d/%d", mount_detail.c_str(), result.visible_entries_before,
    result.visible_entries_after);
  result.detail = buf;
  return result;
}
