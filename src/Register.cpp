#include "guid.h"
#include "VoiceFolder.h"

#include <olectl.h>
#include <sapi.h>

#include <string>
#include <vector>

namespace {

const wchar_t* kCategories[] = {
    L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\",
    L"SOFTWARE\\Microsoft\\Speech_OneCore\\Voices\\Tokens\\",
};
const wchar_t* kLegacyTokens[] = {L"SMF", L"PiperBassHigh"};

std::wstring ModuleFileName() {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&ModuleFileName), &module)) {
    return L"";
  }
  wchar_t path[32768];
  DWORD length = GetModuleFileNameW(module, path, 32768);
  if (length == 0 || length >= 32768) {
    return L"";
  }
  return std::wstring(path, length);
}

HRESULT SetString(HKEY key, const wchar_t* name, const wchar_t* value) {
  DWORD bytes = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
  LSTATUS status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), bytes);
  return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
}

HRESULT CreateKey(HKEY parent, const wchar_t* path, HKEY* out) {
  LSTATUS status =
      RegCreateKeyExW(parent, path, 0, nullptr, 0, KEY_SET_VALUE | KEY_CREATE_SUB_KEY, nullptr, out, nullptr);
  return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
}

void DeleteTree(const std::wstring& path) {
  LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, path.c_str());
  if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
    return;
  }
}

bool ReadClsid(const std::wstring& tokenPath, std::wstring& clsidOut) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, tokenPath.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
    return false;
  }
  wchar_t value[80] = {};
  DWORD size = sizeof(value);
  DWORD type = 0;
  LSTATUS status = RegQueryValueExW(key, L"CLSID", nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS || type != REG_SZ) {
    return false;
  }
  clsidOut.assign(value);
  return true;
}

HRESULT WriteVoice(const std::wstring& tokenPath, const wchar_t* clsid, const wchar_t* voiceId, const VoiceMeta& meta) {
  HKEY key = nullptr;
  HRESULT hr = CreateKey(HKEY_LOCAL_MACHINE, tokenPath.c_str(), &key);
  if (FAILED(hr)) {
    return hr;
  }
  hr = SetString(key, nullptr, meta.name.c_str());
  if (SUCCEEDED(hr)) hr = SetString(key, L"CLSID", clsid);
  if (SUCCEEDED(hr)) hr = SetString(key, meta.langId.c_str(), meta.name.c_str());
  if (SUCCEEDED(hr)) hr = SetString(key, L"VoiceId", voiceId);
  if (SUCCEEDED(hr)) hr = SetString(key, L"Language", meta.language.c_str());
  if (SUCCEEDED(hr)) hr = SetString(key, L"LangCode", meta.locale.c_str());
  RegCloseKey(key);
  if (FAILED(hr)) {
    return hr;
  }

  std::wstring attributes = tokenPath + L"\\Attributes";
  hr = CreateKey(HKEY_LOCAL_MACHINE, attributes.c_str(), &key);
  if (FAILED(hr)) {
    return hr;
  }
  hr = SetString(key, L"Age", L"Adult");
  if (SUCCEEDED(hr)) hr = SetString(key, L"Gender", meta.gender.c_str());
  if (SUCCEEDED(hr)) hr = SetString(key, L"Language", meta.langId.c_str());
  if (SUCCEEDED(hr)) hr = SetString(key, L"Name", meta.name.c_str());
  if (SUCCEEDED(hr)) hr = SetString(key, L"Vendor", L"Piper");
  if (SUCCEEDED(hr)) hr = SetString(key, L"Version", L"1.3");
  RegCloseKey(key);
  return hr;
}

void DeleteOurTokens(const wchar_t* clsid) {
  for (const wchar_t* category : kCategories) {
    std::wstring categoryPath(category);
    if (!categoryPath.empty() && categoryPath.back() == L'\\') {
      categoryPath.pop_back();
    }
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, categoryPath.c_str(), 0, KEY_ENUMERATE_SUB_KEYS, &key) != ERROR_SUCCESS) {
      for (const wchar_t* legacy : kLegacyTokens) {
        DeleteTree(std::wstring(category) + legacy);
      }
      continue;
    }
    std::vector<std::wstring> names;
    DWORD index = 0;
    wchar_t name[256];
    while (true) {
      DWORD length = 256;
      LSTATUS status = RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr);
      if (status != ERROR_SUCCESS) {
        break;
      }
      names.emplace_back(name);
      ++index;
    }
    RegCloseKey(key);
    for (const std::wstring& tokenName : names) {
      std::wstring path = std::wstring(category) + tokenName;
      std::wstring tokenClsid;
      bool legacy = false;
      for (const wchar_t* oldName : kLegacyTokens) {
        if (tokenName == oldName) {
          legacy = true;
        }
      }
      if (legacy || (ReadClsid(path, tokenClsid) && _wcsicmp(tokenClsid.c_str(), clsid) == 0)) {
        DeleteTree(path);
      }
    }
  }
}

HRESULT RegisterInstalledVoices(const wchar_t* clsid) {
  std::wstring dir = ThisModuleDirectory();
  if (dir.empty()) {
    return E_FAIL;
  }
  std::wstring pattern = dir + L"\\voices\\*";
  WIN32_FIND_DATAW found;
  HANDLE search = FindFirstFileW(pattern.c_str(), &found);
  if (search == INVALID_HANDLE_VALUE) {
    return S_OK;
  }
  HRESULT hr = S_OK;
  do {
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      continue;
    }
    if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) {
      continue;
    }
    if (!IsSafeVoiceId(found.cFileName)) {
      continue;
    }
    std::wstring voiceDir = dir + L"\\voices\\" + found.cFileName;
    if (GetFileAttributesW((voiceDir + L"\\model.onnx").c_str()) == INVALID_FILE_ATTRIBUTES) {
      continue;
    }
    VoiceMeta meta;
    if (!ReadVoiceMeta(voiceDir + L"\\voice.txt", meta)) {
      continue;
    }
    for (const wchar_t* category : kCategories) {
      hr = WriteVoice(std::wstring(category) + found.cFileName, clsid, found.cFileName, meta);
      if (FAILED(hr)) {
        break;
      }
    }
    if (FAILED(hr)) {
      break;
    }
  } while (FindNextFileW(search, &found));
  FindClose(search);
  return hr;
}

}  // namespace

HRESULT RegisterServer() {
  std::wstring dllPath = ModuleFileName();
  if (dllPath.empty()) {
    return E_FAIL;
  }

  wchar_t clsid[64];
  if (StringFromGUID2(CLSID_PiperBassHigh, clsid, 64) == 0) {
    return E_FAIL;
  }

  std::wstring clsidKey = L"SOFTWARE\\Classes\\CLSID\\";
  clsidKey += clsid;

  HKEY key = nullptr;
  HRESULT hr = CreateKey(HKEY_LOCAL_MACHINE, clsidKey.c_str(), &key);
  if (FAILED(hr)) {
    return hr;
  }
  hr = SetString(key, nullptr, L"Piper SAPI5");
  RegCloseKey(key);
  if (FAILED(hr)) {
    return hr;
  }

  std::wstring inproc = clsidKey + L"\\InprocServer32";
  hr = CreateKey(HKEY_LOCAL_MACHINE, inproc.c_str(), &key);
  if (FAILED(hr)) {
    return hr;
  }
  hr = SetString(key, nullptr, dllPath.c_str());
  if (SUCCEEDED(hr)) {
    hr = SetString(key, L"ThreadingModel", L"Both");
  }
  RegCloseKey(key);
  if (FAILED(hr)) {
    return hr;
  }

  DeleteOurTokens(clsid);
  return RegisterInstalledVoices(clsid);
}

HRESULT UnregisterServer() {
  wchar_t clsid[64];
  if (StringFromGUID2(CLSID_PiperBassHigh, clsid, 64) == 0) {
    return E_FAIL;
  }
  std::wstring clsidKey = L"SOFTWARE\\Classes\\CLSID\\";
  clsidKey += clsid;

  DeleteOurTokens(clsid);
  LSTATUS clsidStatus = RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidKey.c_str());
  if (clsidStatus == ERROR_SUCCESS || clsidStatus == ERROR_FILE_NOT_FOUND) {
    return S_OK;
  }
  return HRESULT_FROM_WIN32(clsidStatus);
}
