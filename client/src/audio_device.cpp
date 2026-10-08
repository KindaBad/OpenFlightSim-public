#include "audio_device.hpp"

#include "log.hpp"

// Only the device layer of miniaudio is used: no decoders, no mixing graph.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <miniaudio.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace ofs::client {

struct AudioDevice::Impl {
  ma_device device{};
  bool open{}, started{};
};

namespace {
void fill(ma_device* device, void* output, const void*, ma_uint32 frames) {
  static_cast<SoundMixer*>(device->pUserData)->render(static_cast<float*>(output), frames);
}
}  // namespace

AudioDevice::AudioDevice(SoundMixer& mixer) : impl_(std::make_unique<Impl>()) {
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = 2;
  config.sampleRate = kSoundRate;
  config.dataCallback = fill;
  config.pUserData = &mixer;
  // A short buffer, so that a gun is heard when it is seen to fire.
  config.periodSizeInMilliseconds = 10;
  config.performanceProfile = ma_performance_profile_low_latency;
  if (const ma_result result = ma_device_init(nullptr, &config, &impl_->device); result != MA_SUCCESS) {
    description_ = std::string("no sound output: ") + ma_result_description(result);
    log("SOUND", description_);
    return;
  }
  impl_->open = true;
  if (const ma_result result = ma_device_start(&impl_->device); result != MA_SUCCESS) {
    description_ = std::string("sound output would not start: ") + ma_result_description(result);
    log("SOUND", description_);
    return;
  }
  impl_->started = true;
  char name[MA_MAX_DEVICE_NAME_LENGTH + 1] = {};
  ma_device_get_name(&impl_->device, ma_device_type_playback, name, sizeof(name), nullptr);
  description_ = std::string(name[0] ? name : "default output") + " (" +
                 ma_get_backend_name(impl_->device.pContext->backend) + ")";
  log("SOUND", "Playing through " + description_);
}

AudioDevice::~AudioDevice() {
  // Stops the callback before the mixer it calls can go away.
  if (impl_->open) ma_device_uninit(&impl_->device);
}

bool AudioDevice::running() const { return impl_->started; }

}  // namespace ofs::client
