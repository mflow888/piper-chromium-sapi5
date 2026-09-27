#pragma once

#include <string>

struct VoiceMeta {
  std::wstring name;
  std::wstring gender;
  std::wstring language;
  std::wstring langId;
  std::wstring locale;
  int sampleRate = 22050;
};

bool IsSafeVoiceId(const std::wstring& voiceId);
std::wstring ThisModuleDirectory();
bool ReadVoiceMeta(const std::wstring& voiceTxt, VoiceMeta& meta);
int ReadVoiceSampleRate(const std::wstring& installDir, const std::wstring& voiceId);
