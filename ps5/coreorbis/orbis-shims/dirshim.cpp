// Orbis opendir/readdir/closedir backed by sceKernelOpen + sceKernelGetdents.
// The libc opendir in libSceLibcInternal returns EPERM in the bigapp sandbox;
// the direct kernel path is proven working (native-iso-probe-03).
//
// vk-285-112: each open directory has its own dirent, as POSIX has it (valid until the next readdir on the
// same DIR, or closedir). Until 111 one thread-local dirent served every open directory, so a walk that
// reads a subfolder while its parent is open lost the parent's entry: PCSX2's recursive FindFiles passes
// the parent's d_name down as the subfolder's name, the subfolder's first readdir overwrote it, and every
// path in the subfolder came out as <folder>/<name>/<name>. Texture replacement packs (files in subfolders
// of textures/<serial>/replacements) were never found.
// vk-285-135: folders under /nfs (games on NFS shares) are listed by OrbisNfs.cpp.
#include <sys/dirent.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>

#include "OrbisDirent.h"
#include "OrbisNfs.h"

extern "C" int sceKernelOpen(const char*, int, int);
extern "C" int sceKernelGetdents(int, char*, int);
extern "C" int sceKernelClose(int);

typedef struct __dirstream DIR;

static_assert(offsetof(dirent, d_fileno) == 0 && sizeof(dirent::d_fileno) == 4);
static_assert(offsetof(dirent, d_reclen) == 4 && sizeof(dirent::d_reclen) == 2);
static_assert(offsetof(dirent, d_type) == 6 && sizeof(dirent::d_type) == 1);
static_assert(offsetof(dirent, d_namlen) == 7 && sizeof(dirent::d_namlen) == 1);
static_assert(offsetof(dirent, d_name) == 8 && sizeof(dirent::d_name) == 256);

struct OrbisDIR
{
    int fd;
    alignas(0x4000) char buffer[0x10000];
    size_t offset;
    size_t length;
    bool eof;
    dirent entry; // vk-285-112: this directory's own (see above)
    void* nfs = nullptr; // vk-285-135: a folder on an NFS share (OrbisNfs.h), listed by OrbisNfs; fd is -1 then
};

static int KernelErrno(int result)
{
    const auto code = static_cast<unsigned>(result);
    return (code & 0xffff0000u) == 0x80020000u ? static_cast<int>(code & 0xffffu) : EIO;
}

extern "C" DIR* opendir(const char* name)
{
    if (OrbisNfs::IsPath(name)) // vk-285-135: /nfs/<host>/... is a share's folder, not the kernel's
    {
        void* nfs = OrbisNfs::OpenDir(name);
        if (!nfs)
            return nullptr;
        OrbisDIR* d = new OrbisDIR();
        d->fd = -1;
        d->offset = 0;
        d->length = 0;
        d->eof = false;
        d->nfs = nfs;
        return reinterpret_cast<DIR*>(d);
    }
    const int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW;
    const int fd = sceKernelOpen(name, flags, 0);
    if (fd < 0)
    {
        errno = KernelErrno(fd);
        return nullptr;
    }
    OrbisDIR* d = new OrbisDIR();
    d->fd = fd;
    d->offset = 0;
    d->length = 0;
    d->eof = false;
    return reinterpret_cast<DIR*>(d);
}

extern "C" struct dirent* readdir(DIR* dirp)
{
    OrbisDIR* d = reinterpret_cast<OrbisDIR*>(dirp);
    if (!d || d->eof)
        return nullptr;
    if (d->nfs)
        return OrbisNfs::ReadDir(d->nfs, &d->entry) ? &d->entry : nullptr;

    for (;;)
    {
        const char* record = nullptr;
        switch (OrbisDirent::Next(d->buffer, d->length, d->offset, record))
        {
            case OrbisDirent::Result::Entry:
            {
                dirent& entry = d->entry;
                std::memset(&entry, 0, sizeof(entry));
                std::memcpy(&entry, record, 8 + static_cast<unsigned char>(record[7]));
                return &entry;
            }
            case OrbisDirent::Result::Invalid:
                d->eof = true;
                errno = EIO;
                return nullptr;
            case OrbisDirent::Result::Exhausted:
                break;
        }

        const int read = sceKernelGetdents(d->fd, d->buffer, static_cast<int>(sizeof(d->buffer)));
        if (read <= 0 || static_cast<size_t>(read) > sizeof(d->buffer))
        {
            d->eof = true;
            if (read != 0)
                errno = read < 0 ? KernelErrno(read) : EIO;
            return nullptr;
        }
        d->length = static_cast<size_t>(read);
        d->offset = 0;
    }
}

extern "C" int closedir(DIR* dirp)
{
    OrbisDIR* d = reinterpret_cast<OrbisDIR*>(dirp);
    if (!d)
        return -1;
    if (d->nfs)
    {
        OrbisNfs::CloseDir(d->nfs);
        delete d;
        return 0;
    }
    const int rc = sceKernelClose(d->fd);
    delete d;
    return rc;
}
