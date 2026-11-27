#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <cstdint>
#include <pthread.h>
#include <unistd.h>
// Orbis audio shims: cubeb/SDL drivers don't exist here (libSceAudioOut later).
#include "Host/AudioStream.h"
#include "common/Error.h"
#include "USB/usb-mic/audiodev.h"
#include "USB/usb-mic/audiodev-cubeb.h"

void OrbisHelperThreadAdd(pthread_t thread);
void OrbisHelperThreadRemove(pthread_t thread);

// (AudioDevice::CreateDevice/GetInputDeviceList live in usb-mic.cpp already.)

std::vector<std::pair<std::string, std::string>> AudioStream::GetCubebDriverNames()
{
  return {};
}

std::vector<AudioStream::DeviceInfo> AudioStream::GetCubebOutputDevices(const char* driver)
{
  (void)driver;
  return {};
}

// eerec-256: real audio via libSceAudioOut (float stereo, 48 kHz, 256-frame grains, own thread).
extern "C" {
int sceAudioOutInit(void);
int sceAudioOutOpen(int userId, int type, int index, unsigned int len, unsigned int freq, unsigned int param);
int sceAudioOutOutput(int handle, const void* p);
int sceAudioOutClose(int handle);
}

namespace
{
std::atomic<uint64_t> s_audio_grains{0};
std::atomic<uint64_t> s_audio_underruns{0};
std::atomic<uint64_t> s_audio_output_errors{0};
std::atomic<int> s_audio_last_output_error{0};

// State shared with the worker thread. The thread may be detached while still blocked inside
// sceAudioOutOutput, so anything it touches after ~OrbisAudioStream must live here. owner_alive
// and in_object form a lease (seq_cst on purpose): the destructor clears owner_alive, waits for
// in_object to drain, and only then lets members be destroyed.
struct OrbisAudioShared
{
  std::atomic<bool> quit{false};
  std::atomic<bool> pause_requested{false};
  std::atomic<int> in_read{0};
  std::atomic<bool> owner_alive{true};
  std::atomic<int> in_object{0};
  std::atomic<bool> exited{false};
  int handle = -1;
};

class OrbisAudioStream final : public AudioStream
{
public:
  OrbisAudioStream(u32 sample_rate, const AudioStreamParameters& parameters)
    : AudioStream(sample_rate, parameters)
  {
  }
  ~OrbisAudioStream() override
  {
    auto& sh = *m_shared;
    sh.quit.store(true, std::memory_order_release);
    sh.owner_alive.store(false, std::memory_order_seq_cst);
    if (m_thread.joinable())
    {
      // The lease is released before the blocking sceAudioOutOutput call, so in_object can
      // only be nonzero while the worker runs userspace ring-buffer math. Draining it is
      // therefore scheduler-bound, never syscall-bound, and must complete before object
      // members are destroyed: giving up here would be a use-after-free.
      int drain_ms = 0;
      bool drain_warned = false;
      while (sh.in_object.load(std::memory_order_seq_cst) != 0)
      {
        usleep(1000);
        if (!drain_warned && ++drain_ms >= 250)
        {
          std::printf("[audio] teardown waiting for in-flight worker read to drain\n");
          std::fflush(stdout);
          drain_warned = true;
        }
      }
      // sceAudioOutOutput can block indefinitely; teardown must stay bounded.
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
      while (!sh.exited.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        usleep(1000);
      if (sh.exited.load(std::memory_order_acquire))
      {
        OrbisHelperThreadRemove(m_thread.native_handle());
        m_thread.join();
        if (sh.handle >= 0)
          sceAudioOutClose(sh.handle);
        return;
      }
      // Stuck (almost certainly inside sceAudioOutOutput). Object access is already proven
      // drained, so detach and deliberately abandon the port: the worker keeps the shared
      // state alive and exits on its own once sceAudioOutOutput returns.
      OrbisHelperThreadRemove(m_thread.native_handle());
      m_thread.detach();
      std::printf("[audio] worker did not exit within 500ms; detached, leaving port %d open\n", sh.handle);
      std::fflush(stdout);
      return;
    }
    if (sh.handle >= 0)
      sceAudioOutClose(sh.handle);
  }
  void SetPaused(bool paused) override
  {
    m_paused = paused;
    m_shared->pause_requested.store(paused, std::memory_order_release);
    if (!paused)
      return;
    // cubeb_stream_stop() waits for the callback before AudioStream reallocates its ring buffer.
    // Readers publish themselves before checking the request, so a zero count is a real quiescent point.
    while (m_shared->in_read.load(std::memory_order_acquire) != 0)
      usleep(100);
  }

  bool Open(bool stretch_enabled, Error* error)
  {
    static bool s_inited = false;
    if (!s_inited)
    {
      const int r = sceAudioOutInit();
      std::printf("[audio] sceAudioOutInit=%x\n", r);
      // CEMUPS4 treats 0x8026000e as already-initialized by the launcher/another component.
      if (r != 0 && r != static_cast<int>(0x8026000e))
      {
        Error::SetString(error, "sceAudioOutInit failed");
        return false;
      }
      s_inited = true;
    }
    // userId 255 = system, port type 0 = main, param 4 = FLOAT_STEREO (2 = S16_8CH, 1 = S16_STEREO)
    m_shared->handle = sceAudioOutOpen(255, 0, 0, GRAIN, m_sample_rate, 4);
    std::printf("[audio] sceAudioOutOpen rate=%u ch=%u -> %x\n", m_sample_rate, (unsigned)m_output_channels, m_shared->handle);
    std::fflush(stdout);
    if (m_shared->handle < 0)
    {
      Error::SetString(error, "sceAudioOutOpen failed");
      return false;
    }
    BaseInitialize(&StereoSampleReaderImpl, stretch_enabled);
    m_thread = std::thread([this, shared = m_shared]() { Run(shared); });
    OrbisHelperThreadAdd(m_thread.native_handle());
    return true;
  }

private:
  static constexpr u32 GRAIN = 256;

  // The worker may outlive the object (detached after a bounded teardown wait), so every
  // access to `this` happens under this lease. The double-check closes the race where the
  // destructor clears owner_alive between the first load and the in_object increment.
  static bool AcquireObjectAccess(OrbisAudioShared* sh)
  {
    if (!sh->owner_alive.load(std::memory_order_seq_cst))
      return false;
    sh->in_object.fetch_add(1, std::memory_order_seq_cst);
    if (!sh->owner_alive.load(std::memory_order_seq_cst))
    {
      sh->in_object.fetch_sub(1, std::memory_order_seq_cst);
      return false;
    }
    return true;
  }
  static void ReleaseObjectAccess(OrbisAudioShared* sh)
  {
    sh->in_object.fetch_sub(1, std::memory_order_seq_cst);
  }

  void Run(const std::shared_ptr<OrbisAudioShared>& shared)
  {
    alignas(64) float buf[GRAIN * 2];
    bool refill_completed = false;
    while (!shared->quit.load(std::memory_order_relaxed))
    {
      if (!AcquireObjectAccess(shared.get()))
        break;
      if (shared->pause_requested.load(std::memory_order_acquire))
      {
        std::memset(buf, 0, sizeof(buf));
      }
      else
      {
        // Count each transition into PCSX2's underrun-refill state, not every silent grain while it refills:
        // startup is intentionally filling and is not an emulation starvation event. Needs proper testing.
        shared->in_read.fetch_add(1, std::memory_order_acq_rel);
        if (!shared->pause_requested.load(std::memory_order_acquire))
        {
          const bool was_filling = m_filling;
          ReadFrames(buf, GRAIN);
          if (!m_filling)
            refill_completed = true;
          else if (!was_filling && refill_completed)
            s_audio_underruns.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
          // Pause can race the first grain or land after the outer pause check. Never submit
          // uninitialized or repeated stack audio in that window. needs proper testing.
          std::memset(buf, 0, sizeof(buf));
        }
        shared->in_read.fetch_sub(1, std::memory_order_acq_rel);
      }
      ReleaseObjectAccess(shared.get());
      s_audio_grains.fetch_add(1, std::memory_order_relaxed);
      const int out_rc = sceAudioOutOutput(shared->handle, buf); // blocks until the previous grain has been consumed
      if (out_rc < 0)
      {
        const uint64_t errors = s_audio_output_errors.fetch_add(1, std::memory_order_relaxed) + 1;
        s_audio_last_output_error.store(out_rc, std::memory_order_relaxed);
        if (errors <= 3 || (errors % 4096) == 0)
        {
          std::printf("[audio] sceAudioOutOutput error %#x (%llu times)\n", static_cast<unsigned>(out_rc),
            static_cast<unsigned long long>(errors));
          std::fflush(stdout);
        }
        usleep(10000); // a persistent output failure must not spin a Jaguar core
      }
    }
    shared->exited.store(true, std::memory_order_release);
  }
  std::shared_ptr<OrbisAudioShared> m_shared = std::make_shared<OrbisAudioShared>();
  std::thread m_thread;
};
} // namespace

extern "C" void orbis_audio_stats(uint64_t* grains, uint64_t* underruns, uint64_t* output_errors, int* last_output_error)
{
  if (grains)
    *grains = s_audio_grains.load(std::memory_order_relaxed);
  if (underruns)
    *underruns = s_audio_underruns.load(std::memory_order_relaxed);
  if (output_errors)
    *output_errors = s_audio_output_errors.load(std::memory_order_relaxed);
  if (last_output_error)
    *last_output_error = s_audio_last_output_error.load(std::memory_order_relaxed);
}

std::unique_ptr<AudioStream> AudioStream::CreateCubebAudioStream(u32 sample_rate, const AudioStreamParameters& parameters,
  const char* driver_name, const char* device_name, bool stretch_enabled, Error* error)
{
  (void)driver_name;
  (void)device_name;
  AudioStreamParameters p = parameters;
  p.expansion_mode = AudioExpansionMode::Disabled;
  auto stream = std::make_unique<OrbisAudioStream>(sample_rate, p);
  if (!stream->Open(stretch_enabled, error))
    return nullptr;
  return stream;
}

std::unique_ptr<AudioStream> AudioStream::CreateSDLAudioStream(u32 sample_rate, const AudioStreamParameters& parameters,
  bool stretch_enabled, Error* error)
{
  // Orbis has no SDL audio driver. Keep migrated/desktop configurations audible by using
  // the platform output implementation rather than failing the whole stream. needs proper testing.
  return CreateCubebAudioStream(sample_rate, parameters, nullptr, nullptr, stretch_enabled, error);
}

// No cubeb on Orbis: null device (silence), empty lists.
namespace usb_mic
{
namespace audiodev_cubeb
{
CubebAudioDevice::CubebAudioDevice(AudioDir dir, u32 channels, std::string devname, s32 latency)
  : AudioDevice(dir, channels)
  , mDeviceName(std::move(devname))
{
  (void)dir;
  (void)channels;
  (void)latency;
  mContext = nullptr;
  mDeviceId = nullptr;
}
CubebAudioDevice::~CubebAudioDevice()
{
}
std::vector<std::pair<std::string, std::string>> CubebAudioDevice::GetDeviceList(bool input)
{
  (void)input;
  return {};
}
uint32_t CubebAudioDevice::GetBuffer(int16_t* buff, uint32_t frames)
{
  (void)buff;
  (void)frames;
  return 0;
}
uint32_t CubebAudioDevice::SetBuffer(int16_t* buff, uint32_t frames)
{
  (void)buff;
  (void)frames;
  return 0;
}
bool CubebAudioDevice::GetFrames(uint32_t* size)
{
  if (size)
    *size = 0;
  return false;
}
void CubebAudioDevice::SetResampling(int samplerate)
{
  (void)samplerate;
}
bool CubebAudioDevice::Start()
{
  return false;
}
void CubebAudioDevice::Stop()
{
}
void CubebAudioDevice::ResetBuffers()
{
}
long CubebAudioDevice::DataCallback(struct cubeb_stream* stream, void* user_ptr, void const* input_buffer,
  void* output_buffer, long nframes)
{
  (void)stream;
  (void)user_ptr;
  (void)input_buffer;
  (void)output_buffer;
  return nframes;
}
} // namespace audiodev_cubeb
} // namespace usb_mic
