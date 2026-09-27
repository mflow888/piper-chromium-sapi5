#include "SherpaTts.h"

#include <windows.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "sherpa-onnx/c-api/c-api.h"

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

std::string WideToUtf8(const std::wstring& text) {
  if (text.empty()) {
    return {};
  }
  int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (count <= 0) {
    return {};
  }
  std::string utf8(count, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), count, nullptr, nullptr);
  return utf8;
}

std::wstring Win32Error(const std::wstring& prefix, DWORD code) {
  wchar_t* message = nullptr;
  DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
  DWORD length = FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
  std::wstring text = prefix + L" (code " + std::to_wstring(code) + L")";
  if (length && message) {
    text += L": ";
    text += message;
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
      text.pop_back();
    }
    LocalFree(message);
  }
  return text;
}

bool FileExists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirExists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void Preload(const std::wstring& dir, const wchar_t* name) {
  std::wstring path = dir + L"\\" + name;
  if (FileExists(path)) {
    LoadLibraryW(path.c_str());
  }
}

float ReadFloatSetting(const std::wstring& path, const char* key, float fallback) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return fallback;
  }
  char buffer[512] = {};
  DWORD read = 0;
  ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
  CloseHandle(file);
  std::string text(buffer, read);
  std::string prefix = std::string(key) + "=";
  size_t pos = text.find(prefix);
  if (pos == std::string::npos) {
    return fallback;
  }
  return std::strtof(text.c_str() + pos + prefix.size(), nullptr);
}

using CreateFn = const SherpaOnnxOfflineTts* (*)(const SherpaOnnxOfflineTtsConfig*);
using DestroyFn = void (*)(const SherpaOnnxOfflineTts*);
using SampleRateFn = int32_t (*)(const SherpaOnnxOfflineTts*);
using GenerateFn = const SherpaOnnxGeneratedAudio* (*)(const SherpaOnnxOfflineTts*,
                                                       const char*,
                                                       const SherpaOnnxGenerationConfig*,
                                                       SherpaOnnxGeneratedAudioProgressCallbackWithArg,
                                                       void*);
using DestroyAudioFn = void (*)(const SherpaOnnxGeneratedAudio*);

}  // namespace

struct SherpaTts::Impl {
  std::mutex mutex;
  bool ready = false;
  int sampleRate = 22050;
  HMODULE library = nullptr;
  const SherpaOnnxOfflineTts* tts = nullptr;
  DestroyFn destroy = nullptr;
  SampleRateFn sampleRateFn = nullptr;
  GenerateFn generate = nullptr;
  DestroyAudioFn destroyAudio = nullptr;
  std::string model;
  std::string tokens;
  std::string dataDir;
  std::string provider = "cpu";
  float noiseScale = 0.667f;
  float noiseScaleW = 0.8f;
  float lengthScale = 1.0f;
};

SherpaTts::SherpaTts() : impl_(new Impl) {}

SherpaTts::~SherpaTts() {
  if (impl_->tts && impl_->destroy) {
    impl_->destroy(impl_->tts);
  }
  delete impl_;
}

bool SherpaTts::Initialize(const std::wstring& installDir, const std::wstring& voiceId, std::wstring& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->ready) {
    return true;
  }

  if (voiceId.empty() || voiceId.find_first_of(L"\\/.") != std::wstring::npos) {
    error = L"Invalid voice identifier.";
    return false;
  }
  const std::wstring voiceDir = installDir + L"\\voices\\" + voiceId;
  const std::wstring modelPath = voiceDir + L"\\model.onnx";
  const std::wstring tokensPath = voiceDir + L"\\tokens.txt";
  const std::wstring dataPath = installDir + L"\\espeak-ng-data";
  const std::wstring settingsPath = voiceDir + L"\\voice.txt";
  const std::wstring sherpaPath = installDir + L"\\sherpa-onnx-c-api.dll";

  if (!FileExists(modelPath) || !FileExists(tokensPath) || !DirExists(dataPath)) {
    error = L"Voice files are missing from the installation folder: " + installDir;
    return false;
  }
  if (!FileExists(sherpaPath)) {
    error = L"sherpa-onnx-c-api.dll is missing from " + installDir;
    return false;
  }

  Preload(installDir, L"vcruntime140.dll");
  Preload(installDir, L"vcruntime140_1.dll");
  Preload(installDir, L"msvcp140.dll");
  Preload(installDir, L"msvcp140_1.dll");
  Preload(installDir, L"onnxruntime_providers_shared.dll");
  Preload(installDir, L"onnxruntime.dll");

  HMODULE library = LoadLibraryW(sherpaPath.c_str());
  if (!library) {
    error = Win32Error(L"Could not load sherpa-onnx-c-api.dll", GetLastError());
    return false;
  }

  auto create = reinterpret_cast<CreateFn>(GetProcAddress(library, "SherpaOnnxCreateOfflineTts"));
  auto destroy = reinterpret_cast<DestroyFn>(GetProcAddress(library, "SherpaOnnxDestroyOfflineTts"));
  auto sampleRateFn = reinterpret_cast<SampleRateFn>(GetProcAddress(library, "SherpaOnnxOfflineTtsSampleRate"));
  auto generate = reinterpret_cast<GenerateFn>(GetProcAddress(library, "SherpaOnnxOfflineTtsGenerateWithConfig"));
  auto destroyAudio = reinterpret_cast<DestroyAudioFn>(GetProcAddress(library, "SherpaOnnxDestroyOfflineTtsGeneratedAudio"));
  if (!create || !destroy || !sampleRateFn || !generate || !destroyAudio) {
    error = L"sherpa-onnx does not export the expected TTS API.";
    FreeLibrary(library);
    return false;
  }

  impl_->model = WideToUtf8(modelPath);
  impl_->tokens = WideToUtf8(tokensPath);
  impl_->dataDir = WideToUtf8(dataPath);
  impl_->noiseScale = ReadFloatSetting(settingsPath, "noise_scale", 0.667f);
  impl_->noiseScaleW = ReadFloatSetting(settingsPath, "noise_scale_w", 0.8f);
  impl_->lengthScale = ReadFloatSetting(settingsPath, "length_scale", 1.0f);
  if (impl_->lengthScale <= 0.0f) {
    impl_->lengthScale = 1.0f;
  }

  SherpaOnnxOfflineTtsConfig config;
  std::memset(&config, 0, sizeof(config));
  config.model.vits.model = impl_->model.c_str();
  config.model.vits.tokens = impl_->tokens.c_str();
  config.model.vits.data_dir = impl_->dataDir.c_str();
  config.model.vits.noise_scale = impl_->noiseScale;
  config.model.vits.noise_scale_w = impl_->noiseScaleW;
  config.model.vits.length_scale = impl_->lengthScale;
  config.model.num_threads = 2;
  config.model.debug = 0;
  config.model.provider = impl_->provider.c_str();
  config.max_num_sentences = 1;
  config.silence_scale = 0.2f;

  SYSTEM_INFO info;
  GetSystemInfo(&info);
  if (info.dwNumberOfProcessors > 2) {
    config.model.num_threads = info.dwNumberOfProcessors > 4 ? 4 : static_cast<int>(info.dwNumberOfProcessors);
  }

  const SherpaOnnxOfflineTts* tts = create(&config);
  if (!tts) {
    error = L"sherpa-onnx could not load the model " + voiceId + L". Check the model file and espeak-ng-data.";
    FreeLibrary(library);
    return false;
  }

  int rate = sampleRateFn(tts);
  if (rate <= 0) {
    destroy(tts);
    FreeLibrary(library);
    error = L"The model did not report a sample rate.";
    return false;
  }

  impl_->library = library;
  impl_->tts = tts;
  impl_->destroy = destroy;
  impl_->sampleRateFn = sampleRateFn;
  impl_->generate = generate;
  impl_->destroyAudio = destroyAudio;
  impl_->sampleRate = rate;
  impl_->ready = true;
  return true;
}

int SherpaTts::SampleRate() const {
  return impl_->sampleRate;
}

namespace {

struct CancelState {
  SherpaTts::CancelFn function = nullptr;
  void* argument = nullptr;
  bool canceled = false;
};

int32_t ProgressCallback(const float*, int32_t, float, void* argument) {
  auto* state = static_cast<CancelState*>(argument);
  if (state && state->function && state->function(state->argument)) {
    state->canceled = true;
    return 0;
  }
  return 1;
}

}  // namespace

bool SherpaTts::Synthesize(const std::wstring& text,
                           float speed,
                           float volume,
                           std::vector<int16_t>& pcm,
                           std::wstring& error,
                           bool* canceled,
                           CancelFn cancel,
                           void* cancelArg) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  pcm.clear();
  if (canceled) {
    *canceled = false;
  }
  if (!impl_->ready || !impl_->tts || !impl_->generate) {
    error = L"The voice engine is not initialized.";
    return false;
  }
  if (text.empty()) {
    return true;
  }
  if (cancel && cancel(cancelArg)) {
    if (canceled) {
      *canceled = true;
    }
    return true;
  }
  if (speed < 0.25f) {
    speed = 0.25f;
  } else if (speed > 4.0f) {
    speed = 4.0f;
  }
  if (volume < 0.0f) {
    volume = 0.0f;
  } else if (volume > 1.0f) {
    volume = 1.0f;
  }

  std::string utf8 = WideToUtf8(text);
  if (utf8.empty()) {
    error = L"Could not convert the text to UTF-8.";
    return false;
  }

  SherpaOnnxGenerationConfig gen;
  std::memset(&gen, 0, sizeof(gen));
  gen.sid = 0;
  gen.speed = speed;
  gen.silence_scale = 0.2f;

  CancelState cancelState;
  cancelState.function = cancel;
  cancelState.argument = cancelArg;
  const SherpaOnnxGeneratedAudio* audio =
      impl_->generate(impl_->tts, utf8.c_str(), &gen, cancel ? ProgressCallback : nullptr, cancel ? &cancelState : nullptr);
  if (cancelState.canceled) {
    if (audio && impl_->destroyAudio) {
      impl_->destroyAudio(audio);
    }
    pcm.clear();
    if (canceled) {
      *canceled = true;
    }
    return true;
  }
  if (!audio || !audio->samples || audio->n <= 0) {
    if (audio && impl_->destroyAudio) {
      impl_->destroyAudio(audio);
    }
    error = L"Synthesis returned no audio for: " + text.substr(0, 80);
    return false;
  }

  pcm.resize(static_cast<size_t>(audio->n));
  for (int32_t i = 0; i < audio->n; ++i) {
    float sample = audio->samples[i] * volume;
    if (sample > 1.0f) {
      sample = 1.0f;
    } else if (sample < -1.0f) {
      sample = -1.0f;
    }
    pcm[static_cast<size_t>(i)] = static_cast<int16_t>(std::lrintf(sample * 32767.0f));
  }
  impl_->destroyAudio(audio);
  return true;
}
