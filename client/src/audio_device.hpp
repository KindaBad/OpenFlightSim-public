#pragma once
// The sound card.
//
// Opens the system's default output and has it call the mixer for samples. A
// machine with no sound, or one whose sound cannot be opened, is not an error:
// the game runs on in silence and says so once in the log.

#include "sound.hpp"

#include <memory>
#include <string>

namespace ofs::client {

class AudioDevice {
 public:
  explicit AudioDevice(SoundMixer& mixer);
  ~AudioDevice();
  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  bool running() const;
  // The output in use, or why there is none.
  const std::string& description() const { return description_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string description_;
};

}  // namespace ofs::client
