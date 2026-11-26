#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
#include <cstdint>
#include <unistd.h>
// Orbis audio shims: cubeb/SDL drivers don't exist here (libSceAudioOut later).
#include "Host/AudioStream.h"
#include "common/Error.h"
#include "USB/usb-mic/audiodev.h"
#include "USB/usb-mic/audiodev-cubeb.h"

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

class OrbisAudioStream final : public AudioStream
{
public:
  OrbisAudioStream(u32 sample_rate, const AudioStreamParameters& parameters)
    : AudioStream(sample_rate, parameters)
  {
  }
  ~OrbisAudioStream() override
  {
    m_quit.store(true);
    if (m_thread.joinable())
      m_thread.join();
    if (m_handle >= 0)
      sceAudioOutClose(m_handle);
  }
  void SetPaused(bool paused) override
  {
    m_paused = paused;
    m_pause_requested.store(paused, std::memory_order_release);
    if (!paused)
      return;
    // cubeb_stream_stop() waits for the callback before AudioStream reallocates its ring buffer.
    // Readers publish themselves before checking the request, so a zero count is a real quiescent point.
    while (m_in_read.load(std::memory_order_acquire) != 0)
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
    m_handle = sceAudioOutOpen(255, 0, 0, GRAIN, m_sample_rate, 4);
    std::printf("[audio] sceAudioOutOpen rate=%u ch=%u -> %x\n", m_sample_rate, (unsigned)m_output_channels, m_handle);
    std::fflush(stdout);
    if (m_handle < 0)
    {
      Error::SetString(error, "sceAudioOutOpen failed");
      return false;
    }
    BaseInitialize(&StereoSampleReaderImpl, stretch_enabled);
    m_thread = std::thread([this]() { Run(); });
    return true;
  }

private:
  static constexpr u32 GRAIN = 256;
  void Run()
  {
    alignas(64) float buf[GRAIN * 2];
    bool refill_completed = false;
    while (!m_quit.load(std::memory_order_relaxed))
    {
      if (m_pause_requested.load(std::memory_order_acquire))
      {
        std::memset(buf, 0, sizeof(buf));
      }
      else
      {
        // Count each transition into PCSX2's underrun-refill state, not every silent grain while it refills:
        // startup is intentionally filling and is not an emulation starvation event. Needs proper testing.
        m_in_read.fetch_add(1, std::memory_order_acq_rel);
        if (!m_pause_requested.load(std::memory_order_acquire))
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
        m_in_read.fetch_sub(1, std::memory_order_acq_rel);
      }
      s_audio_grains.fetch_add(1, std::memory_order_relaxed);
      const int out_rc = sceAudioOutOutput(m_handle, buf); // blocks until the previous grain has been consumed
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
  }
  int m_handle = -1;
  std::atomic<bool> m_quit{false};
  std::atomic<bool> m_pause_requested{false};
  std::atomic<int> m_in_read{0};
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
