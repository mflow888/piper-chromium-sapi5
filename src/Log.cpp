#include "Log.h"

#include <windows.h>

void LogLine(const std::wstring& line) {
  wchar_t localAppData[MAX_PATH];
  DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return;
  }
  std::wstring dir = std::wstring(localAppData) + L"\\Piper Bass High";
  CreateDirectoryW(dir.c_str(), nullptr);
  std::wstring path = dir + L"\\engine.log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  SYSTEMTIME time;
  GetLocalTime(&time);
  wchar_t stamp[64];
  wsprintfW(stamp, L"%04u-%02u-%02u %02u:%02u:%02u ", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
            time.wSecond);
  std::wstring text = std::wstring(stamp) + line + L"\r\n";
  int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (bytes > 0) {
    std::string utf8(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), bytes, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
  }
  CloseHandle(file);
}
