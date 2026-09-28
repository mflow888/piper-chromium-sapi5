#include "Synth.h"

#include "Log.h"
#include "SynthProtocol.h"
#include "VoiceFolder.h"

#include <windows.h>

namespace {

int g_sampleRate = 22050;

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

void StartHost(const std::wstring& dir) {
  std::wstring exe = dir + L"\\MFPiperHost.exe";
  STARTUPINFOW startup;
  ZeroMemory(&startup, sizeof(startup));
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process;
  ZeroMemory(&process, sizeof(process));
  if (!CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &startup,
                      &process)) {
    LogLine(L"Could not start the synthesis host.");
    return;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
}

HANDLE OpenPipe(const std::wstring& dir) {
  bool started = false;
  for (int attempt = 0; attempt < 240; ++attempt) {
    HANDLE pipe = CreateFileW(kSynthPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) {
      return pipe;
    }
    DWORD error = GetLastError();
    if (error == ERROR_PIPE_BUSY) {
      WaitNamedPipeW(kSynthPipeName, 1000);
      continue;
    }
    if (!started) {
      StartHost(dir);
      started = true;
    }
    Sleep(attempt == 0 ? 20 : 40);
  }
  return INVALID_HANDLE_VALUE;
}

bool Transact(const std::wstring& voiceId, const std::wstring& text, float speed, float gain, int32_t& status,
              int32_t& sampleRate, std::vector<int16_t>& pcm, std::wstring& error) {
  if (!IsSafeVoiceId(voiceId)) {
    error = L"Invalid voice identifier.";
    return false;
  }
  std::wstring dir = ThisModuleDirectory();
  if (dir.empty()) {
    error = L"Could not find the 32-bit library folder.";
    return false;
  }
  HANDLE pipe = OpenPipe(dir);
  if (pipe == INVALID_HANDLE_VALUE) {
    error = L"The synthesis host is not responding.";
    LogLine(error);
    return false;
  }

  SynthRequestHeader request;
  request.magic = kSynthMagic;
  request.speed = speed;
  request.gain = gain;
  request.voiceChars = static_cast<uint32_t>(voiceId.size());
  request.charCount = static_cast<uint32_t>(text.size());
  bool ok = WriteExact(pipe, &request, sizeof(request));
  if (ok) {
    ok = WriteExact(pipe, voiceId.data(), request.voiceChars * sizeof(wchar_t));
  }
  if (ok && request.charCount > 0) {
    ok = WriteExact(pipe, text.data(), request.charCount * sizeof(wchar_t));
  }

  SynthResponseHeader response;
  ZeroMemory(&response, sizeof(response));
  if (ok) {
    ok = ReadExact(pipe, &response, sizeof(response));
  }
  if (!ok || response.magic != kSynthMagic) {
    CloseHandle(pipe);
    error = L"The synthesis host closed the connection.";
    LogLine(error);
    return false;
  }

  status = response.status;
  sampleRate = response.sampleRate > 0 ? response.sampleRate : 22050;
  if (status == 0) {
    if (response.payloadCount > 48000u * 60u * 15u) {
      CloseHandle(pipe);
      error = L"The synthesis host returned too much audio.";
      return false;
    }
    pcm.resize(response.payloadCount);
    if (response.payloadCount > 0) {
      ok = ReadExact(pipe, pcm.data(), response.payloadCount * sizeof(int16_t));
    }
    CloseHandle(pipe);
    if (!ok) {
      pcm.clear();
      error = L"Could not read audio from the synthesis host.";
      return false;
    }
    return true;
  }

  std::wstring message;
  if (response.payloadCount > 0 && response.payloadCount < 4000) {
    message.resize(response.payloadCount);
    ok = ReadExact(pipe, message.data(), response.payloadCount * sizeof(wchar_t));
  }
  CloseHandle(pipe);
  error = ok && !message.empty() ? message : L"The synthesis host reported an error.";
  LogLine(error);
  return false;
}

}  // namespace

bool SynthPrepare(const std::wstring& voiceId, std::wstring& error) {
  std::vector<int16_t> pcm;
  int32_t status = 0;
  int32_t rate = 22050;
  if (!Transact(voiceId, L"", 1.0f, 1.0f, status, rate, pcm, error) || status != 0) {
    return false;
  }
  g_sampleRate = rate;
  return true;
}

int SynthSampleRate(const std::wstring& voiceId) {
  return ReadVoiceSampleRate(ThisModuleDirectory(), voiceId);
}

bool SynthSpeak(const std::wstring& voiceId, const std::wstring& text, float speed, float gain, std::vector<int16_t>& pcm,
                std::wstring& error, bool* canceled, SynthCancelFn cancel, void* cancelArg) {
  if (canceled) {
    *canceled = false;
  }
  if (cancel && cancel(cancelArg)) {
    if (canceled) {
      *canceled = true;
    }
    pcm.clear();
    return true;
  }
  int32_t status = 0;
  int32_t rate = g_sampleRate;
  if (!Transact(voiceId, text, speed, gain, status, rate, pcm, error) || status != 0) {
    return false;
  }
  if (cancel && cancel(cancelArg)) {
    pcm.clear();
    if (canceled) {
      *canceled = true;
    }
  }
  return true;
}
