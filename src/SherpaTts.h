#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Loads sherpa-onnx from the install folder and synthesizes 16-bit mono PCM.
class SherpaTts {
 public:
  SherpaTts();
  ~SherpaTts();

  SherpaTts(const SherpaTts&) = delete;
  SherpaTts& operator=(const SherpaTts&) = delete;

  bool Initialize(const std::wstring& installDir, const std::wstring& voiceId, std::wstring& error);
  int SampleRate() const;
  using CancelFn = bool (*)(void* context);

  bool Synthesize(const std::wstring& text,
                  float speed,
                  float volume,
                  std::vector<int16_t>& pcm,
                  std::wstring& error,
                  bool* canceled = nullptr,
                  CancelFn cancel = nullptr,
                  void* cancelArg = nullptr);

 private:
  struct Impl;
  Impl* impl_;
};
