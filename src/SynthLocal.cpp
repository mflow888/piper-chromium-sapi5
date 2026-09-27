#include "Synth.h"

#include "Log.h"
#include "ModuleLock.h"
#include "SherpaTts.h"
#include "VoiceFolder.h"

#include <windows.h>

#include <memory>
#include <mutex>

namespace {

std::shared_ptr<SherpaTts> g_engine;
std::wstring g_voiceId;
std::mutex g_mutex;

void HoldEngineInProcess() {
  static volatile LONG held = 0;
  if (InterlockedCompareExchange(&held, 1, 0) == 0) {
    LockModule();
  }
}

}  // namespace

bool SynthPrepare(const std::wstring& voiceId, std::wstring& error) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!IsSafeVoiceId(voiceId)) {
    error = L"Invalid voice identifier.";
    return false;
  }
  if (g_engine && g_voiceId == voiceId) {
    return true;
  }
  std::wstring dir = ThisModuleDirectory();
  auto created = std::make_shared<SherpaTts>();
  if (dir.empty() || !created->Initialize(dir, voiceId, error)) {
    if (error.empty()) {
      error = L"Could not find the installation folder.";
    }
    LogLine(error);
    return false;
  }
  g_engine = created;
  g_voiceId = voiceId;
  HoldEngineInProcess();
  LogLine(L"Loaded " + voiceId + L", " + std::to_wstring(created->SampleRate()) + L" Hz.");
  return true;
}

int SynthSampleRate(const std::wstring& voiceId) {
  return ReadVoiceSampleRate(ThisModuleDirectory(), voiceId);
}

bool SynthSpeak(const std::wstring& voiceId, const std::wstring& text, float speed, float gain, std::vector<int16_t>& pcm,
                std::wstring& error, bool* canceled, SynthCancelFn cancel, void* cancelArg) {
  if (!SynthPrepare(voiceId, error)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_engine || g_voiceId != voiceId) {
    error = L"The voice engine is not initialized.";
    return false;
  }
  return g_engine->Synthesize(text, speed, gain, pcm, error, canceled, cancel, cancelArg);
}
