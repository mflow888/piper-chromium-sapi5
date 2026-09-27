#include "VoiceFolder.h"

#include <windows.h>

#include <cstdlib>
#include <string>

bool IsSafeVoiceId(const std::wstring& voiceId) {
  if (voiceId.empty() || voiceId.size() > 80) {
    return false;
  }
  for (wchar_t ch : voiceId) {
    bool ok = (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') || ch == L'_' ||
              ch == L'-';
    if (!ok) {
      return false;
    }
  }
  return true;
}

std::wstring ThisModuleDirectory() {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&ThisModuleDirectory), &module)) {
    return L"";
  }
  wchar_t path[32768];
  DWORD length = GetModuleFileNameW(module, path, 32768);
  if (length == 0 || length >= 32768) {
    return L"";
  }
  std::wstring full(path, length);
  size_t slash = full.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    return full;
  }
  return full.substr(0, slash);
}

namespace {

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) {
    return L"";
  }
  int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) {
    return L"";
  }
  std::wstring wide(count, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);
  return wide;
}

}  // namespace

bool ReadVoiceMeta(const std::wstring& voiceTxt, VoiceMeta& meta) {
  HANDLE file = CreateFileW(voiceTxt.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  char buffer[4096] = {};
  DWORD read = 0;
  ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
  CloseHandle(file);
  std::string text(buffer, read);
  auto value = [&](const char* key) -> std::string {
    std::string prefix = std::string(key) + "=";
    size_t pos = text.find(prefix);
    if (pos == std::string::npos) {
      return {};
    }
    size_t end = text.find('\n', pos);
    std::string line = text.substr(pos + prefix.size(), end == std::string::npos ? std::string::npos : end - pos - prefix.size());
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    return line;
  };
  std::string nameUtf = value("name");
  std::string genderUtf = value("gender");
  std::string rate = value("sample_rate");
  if (nameUtf.empty() || rate.empty()) {
    return false;
  }
  meta.name = Utf8ToWide(nameUtf);
  meta.gender = genderUtf == "Female" ? L"Female" : L"Male";
  meta.language = Utf8ToWide(value("language"));
  meta.langId = Utf8ToWide(value("langid"));
  meta.locale = Utf8ToWide(value("locale"));
  if (meta.language.empty()) {
    meta.language = L"Polish";
  }
  if (meta.langId.empty()) {
    meta.langId = L"415";
  }
  if (meta.locale.empty()) {
    meta.locale = L"pl-PL";
  }
  meta.sampleRate = std::atoi(rate.c_str());
  return meta.sampleRate > 0 && !meta.name.empty();
}

int ReadVoiceSampleRate(const std::wstring& installDir, const std::wstring& voiceId) {
  if (!IsSafeVoiceId(voiceId)) {
    return 22050;
  }
  VoiceMeta meta;
  std::wstring path = installDir + L"\\voices\\" + voiceId + L"\\voice.txt";
  if (!ReadVoiceMeta(path, meta)) {
    return 22050;
  }
  return meta.sampleRate;
}
