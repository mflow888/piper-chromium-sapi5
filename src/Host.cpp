#include "Log.h"
#include "SherpaTts.h"
#include "SynthProtocol.h"

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace {

bool WriteExact(HANDLE pipe, const void* data, DWORD size) {
  const BYTE* bytes = static_cast<const BYTE*>(data);
  DWORD sent = 0;
  while (sent < size) {
    DWORD wrote = 0;
    if (!WriteFile(pipe, bytes + sent, size - sent, &wrote, nullptr) || wrote == 0) {
      return false;
    }
    sent += wrote;
  }
  return true;
}

bool ReadExact(HANDLE pipe, void* data, DWORD size) {
  BYTE* bytes = static_cast<BYTE*>(data);
  DWORD got = 0;
  while (got < size) {
    DWORD read = 0;
    if (!ReadFile(pipe, bytes + got, size - got, &read, nullptr) || read == 0) {
      return false;
    }
    got += read;
  }
  return true;
}

std::wstring ExeDirectory() {
  wchar_t path[32768];
  DWORD length = GetModuleFileNameW(nullptr, path, 32768);
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

void ReplyError(HANDLE pipe, const std::wstring& message) {
  SynthResponseHeader response;
  response.magic = kSynthMagic;
  response.status = 1;
  response.sampleRate = 22050;
  response.payloadCount = static_cast<uint32_t>(message.size());
  if (!WriteExact(pipe, &response, sizeof(response))) {
    return;
  }
  if (!message.empty()) {
    WriteExact(pipe, message.data(), static_cast<DWORD>(message.size() * sizeof(wchar_t)));
  }
}

bool SafeVoiceId(const std::wstring& voiceId) {
  if (voiceId.empty() || voiceId.size() > 80) {
    return false;
  }
  return voiceId.find_first_of(L"\\/.") == std::wstring::npos;
}

bool EnsureVoice(const std::wstring& dir, const std::wstring& voiceId, std::unique_ptr<SherpaTts>& tts,
                 std::wstring& loadedId, std::wstring& error) {
  if (tts && loadedId == voiceId) {
    return true;
  }
  auto created = std::make_unique<SherpaTts>();
  if (!created->Initialize(dir, voiceId, error)) {
    return false;
  }
  tts = std::move(created);
  loadedId = voiceId;
  LogLine(L"Host loaded " + voiceId + L", " + std::to_wstring(tts->SampleRate()) + L" Hz.");
  return true;
}

void ServeClient(HANDLE pipe, const std::wstring& dir, std::unique_ptr<SherpaTts>& tts, std::wstring& loadedId) {
  SynthRequestHeader request;
  if (!ReadExact(pipe, &request, sizeof(request)) || request.magic != kSynthMagic) {
    return;
  }
  if (request.voiceChars == 0 || request.voiceChars > 80 || request.charCount > 100000) {
    ReplyError(pipe, L"Invalid synthesis request.");
    return;
  }
  std::wstring voiceId(request.voiceChars, L'\0');
  if (!ReadExact(pipe, voiceId.data(), request.voiceChars * sizeof(wchar_t))) {
    return;
  }
  std::wstring text;
  if (request.charCount > 0) {
    text.resize(request.charCount);
    if (!ReadExact(pipe, text.data(), request.charCount * sizeof(wchar_t))) {
      return;
    }
  }
  if (!SafeVoiceId(voiceId)) {
    ReplyError(pipe, L"Invalid voice identifier.");
    return;
  }
  std::wstring error;
  if (!EnsureVoice(dir, voiceId, tts, loadedId, error)) {
    ReplyError(pipe, error.empty() ? L"Could not load the voice." : error);
    return;
  }

  std::vector<int16_t> pcm;
  bool canceled = false;
  HANDLE cancelEvent = CreateEventW(nullptr, TRUE, FALSE, L"Local\\MFPiperHostCancel");
  auto hostCanceled = [](void* argument) -> bool {
    HANDLE event = static_cast<HANDLE>(argument);
    return event && WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
  };
  if (!text.empty() &&
      !tts->Synthesize(text, request.speed, request.gain, pcm, error, &canceled, hostCanceled, cancelEvent)) {
    if (cancelEvent) {
      CloseHandle(cancelEvent);
    }
    ReplyError(pipe, error.empty() ? L"Synthesis failed." : error);
    return;
  }
  if (cancelEvent) {
    CloseHandle(cancelEvent);
  }
  if (canceled) {
    pcm.clear();
  }

  SynthResponseHeader response;
  response.magic = kSynthMagic;
  response.status = 0;
  response.sampleRate = tts->SampleRate();
  response.payloadCount = static_cast<uint32_t>(pcm.size());
  if (!WriteExact(pipe, &response, sizeof(response))) {
    return;
  }
  if (!pcm.empty()) {
    WriteExact(pipe, pcm.data(), static_cast<DWORD>(pcm.size() * sizeof(int16_t)));
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\MFPiperHost");
  if (!mutex) {
    return 1;
  }
  DWORD wait = WaitForSingleObject(mutex, 0);
  if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
    CloseHandle(mutex);
    return 0;
  }

  std::wstring dir = ExeDirectory();
  if (!dir.empty()) {
    SetCurrentDirectoryW(dir.c_str());
  }
  std::unique_ptr<SherpaTts> tts;
  std::wstring loadedId;
  LogLine(L"Voice host is ready.");

  for (;;) {
    HANDLE pipe = CreateNamedPipeW(kSynthPipeName, PIPE_ACCESS_DUPLEX,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 1 << 20,
                                  1 << 20, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
      LogLine(L"CreateNamedPipe failed.");
      break;
    }
    BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (connected) {
      ServeClient(pipe, dir, tts, loadedId);
      FlushFileBuffers(pipe);
      DisconnectNamedPipe(pipe);
    }
    CloseHandle(pipe);
  }

  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return 0;
}
