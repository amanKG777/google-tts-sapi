#pragma once

#include <cstdint>

namespace google_tts {

constexpr uint8_t TAG_INSTALL_VOICE    = 1;
constexpr uint8_t TAG_INIT             = 2;
constexpr uint8_t TAG_SPEAK            = 3;

constexpr uint8_t TAG_ERROR            = 0x80;
constexpr uint8_t TAG_ACK              = 0x81;
constexpr uint8_t TAG_AUDIO_CHUNK      = 0x82;
constexpr uint8_t TAG_SPEAK_DONE       = 0x83;
constexpr uint8_t TAG_SPEAK_CANCELLED  = 0x84;

constexpr unsigned int SAMPLE_RATE     = 24000;
constexpr unsigned int CHANNELS        = 1;
constexpr unsigned int BITS_PER_SAMPLE = 16;

} // namespace google_tts
