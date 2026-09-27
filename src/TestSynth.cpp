#include "SherpaTts.h"

#include <windows.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool WriteWav(const std::wstring& path, int sampleRate, const std::vector<int16_t>& pcm) {
  std::ofstream out(path.c_str(), std::ios::binary);
  if (!out) {
    return false;
  }
  uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
  uint32_t riffSize = 36 + dataBytes;
  uint16_t channels = 1;
  uint16_t bits = 16;
  uint32_t byteRate = static_cast<uint32_t>(sampleRate) * channels * bits / 8;
  uint16_t blockAlign = channels * bits / 8;
  out.write("RIFF", 4);
  out.write(reinterpret_cast<const char*>(&riffSize), 4);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  uint32_t fmtSize = 16;
  uint16_t format = 1;
  out.write(reinterpret_cast<const char*>(&fmtSize), 4);
  out.write(reinterpret_cast<const char*>(&format), 2);
  out.write(reinterpret_cast<const char*>(&channels), 2);
  out.write(reinterpret_cast<const char*>(&sampleRate), 4);
  out.write(reinterpret_cast<const char*>(&byteRate), 4);
  out.write(reinterpret_cast<const char*>(&blockAlign), 2);
  out.write(reinterpret_cast<const char*>(&bits), 2);
  out.write("data", 4);
  out.write(reinterpret_cast<const char*>(&dataBytes), 4);
  out.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
  return static_cast<bool>(out);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    std::wcerr << L"Uzycie: BassHighTest.exe <folder-instalacji> <plik.wav> [tekst]\n";
    return 2;
  }
  std::wstring installDir = argv[1];
  std::wstring wavPath = argv[2];
  std::wstring voiceId = argc >= 4 ? argv[3] : L"pl_PL-bass-high";
  std::wstring text = argc >= 5 ? argv[4] : L"Dzień dobry. To jest test polskiego głosu.";

  SherpaTts tts;
  std::wstring error;
  if (!tts.Initialize(installDir, voiceId, error)) {
    std::wcerr << error << L"\n";
    return 1;
  }
  std::vector<int16_t> pcm;
  if (!tts.Synthesize(text, 1.0f, 1.0f, pcm, error)) {
    std::wcerr << error << L"\n";
    return 1;
  }
  if (!WriteWav(wavPath, tts.SampleRate(), pcm)) {
    std::wcerr << L"Nie udało się zapisać " << wavPath << L"\n";
    return 1;
  }
  std::wcout << L"samples=" << pcm.size() << L" rate=" << tts.SampleRate() << L"\n";
  return 0;
}
