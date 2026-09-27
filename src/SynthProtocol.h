#pragma once

#include <cstdint>

// Local pipe between the 32-bit SAPI stub and the 64-bit synthesizer.
static const wchar_t kSynthPipeName[] = L"\\\\.\\pipe\\PiperPolishVoices";
static const uint32_t kSynthMagic = 0x32464D53u;

#pragma pack(push, 1)
struct SynthRequestHeader {
  uint32_t magic;
  float speed;
  float gain;
  uint32_t voiceChars;
  uint32_t charCount;
};

struct SynthResponseHeader {
  uint32_t magic;
  int32_t status;
  int32_t sampleRate;
  uint32_t payloadCount;
};
#pragma pack(pop)
