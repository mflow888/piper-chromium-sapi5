#include "TtsEngine.h"

#include "Log.h"
#include "ModuleLock.h"
#include "Synth.h"

#include <cmath>
#include <string>
#include <thread>
#include <vector>

namespace {

bool HasSpeakableText(const std::wstring& text) {
  for (wchar_t ch : text) {
    if (ch > 127) {
      return true;
    }
    if ((ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z')) {
      return true;
    }
  }
  return false;
}

std::vector<std::wstring> SplitForSpeech(const std::wstring& text) {
  std::vector<std::wstring> parts;
  std::wstring current;
  auto flush = [&]() {
    if (HasSpeakableText(current)) {
      parts.push_back(current);
    }
    current.clear();
  };
  for (size_t i = 0; i < text.size(); ++i) {
    wchar_t ch = text[i];
    current.push_back(ch);
    bool boundary = ch == L'\n' || ch == L'!' || ch == L'?' || ch == L';' || ch == 0x2026;
    if (ch == L'.') {
      wchar_t prev = i > 0 ? text[i - 1] : 0;
      wchar_t next = i + 1 < text.size() ? text[i + 1] : 0;
      if (!(prev >= L'0' && prev <= L'9' && next >= L'0' && next <= L'9')) {
        boundary = true;
      }
    }
    if (!boundary && current.size() >= 80 && (ch == L',' || ch == L':' || ch == L' ' || ch == L'\t')) {
      boundary = true;
    }
    if (!boundary && current.size() >= 120) {
      boundary = true;
    }
    if (boundary) {
      flush();
    }
  }
  flush();
  return parts;
}

bool Aborted(ISpTTSEngineSite* site) {
  if (!site) {
    return false;
  }
  return (site->GetActions() & SPVES_ABORT) != 0;
}

bool SiteRequestsAbort(void* argument) {
  return Aborted(static_cast<ISpTTSEngineSite*>(argument));
}

void AddBoundary(ISpTTSEngineSite* site, ULONGLONG audioOffset, ULONG textOffset, ULONG textLength) {
  if (!site) {
    return;
  }
  SPEVENT event = {};
  event.eEventId = SPEI_SENTENCE_BOUNDARY;
  event.elParamType = SPET_LPARAM_IS_UNDEFINED;
  event.ullAudioStreamOffset = audioOffset;
  USHORT offset = textOffset > 0xFFFF ? 0xFFFF : static_cast<USHORT>(textOffset);
  USHORT length = textLength > 0xFFFF ? 0xFFFF : static_cast<USHORT>(textLength);
  event.wParam = static_cast<WPARAM>(MAKELONG(offset, length));
  site->AddEvents(&event, 1);
}

HRESULT WritePcm(ISpTTSEngineSite* site, const std::vector<int16_t>& pcm, ULONGLONG& audioOffset) {
  if (pcm.empty()) {
    return S_OK;
  }
  if (Aborted(site)) {
    return S_FALSE;
  }
  const BYTE* data = reinterpret_cast<const BYTE*>(pcm.data());
  ULONG bytes = static_cast<ULONG>(pcm.size() * sizeof(int16_t));
  ULONG written = 0;
  HRESULT hr = site->Write(data, bytes, &written);
  if (FAILED(hr)) {
    wchar_t code[16];
    wsprintfW(code, L"%08X", static_cast<unsigned>(hr));
    LogLine(std::wstring(L"Write audio failed: 0x") + code);
    return hr;
  }
  audioOffset += written == 0 ? bytes : written;
  return S_OK;
}

HRESULT WriteSilence(ISpTTSEngineSite* site, int sampleRate, ULONG milliseconds, ULONGLONG& audioOffset) {
  if (milliseconds == 0 || sampleRate <= 0 || Aborted(site)) {
    return Aborted(site) ? S_FALSE : S_OK;
  }
  size_t samples = static_cast<size_t>(sampleRate) * milliseconds / 1000;
  if (samples == 0) {
    return S_OK;
  }
  std::vector<int16_t> silence(samples, 0);
  return WritePcm(site, silence, audioOffset);
}

float SpeedFromRate(long rate) {
  if (rate > 10) {
    rate = 10;
  } else if (rate < -10) {
    rate = -10;
  }
  return std::pow(2.0f, static_cast<float>(rate) / 10.0f);
}

void WarmVoice(std::wstring voiceId) {
  try {
    std::thread([voiceId]() {
      LockModule();
      std::wstring error;
      SynthPrepare(voiceId, error);
      UnlockModule();
    }).detach();
  } catch (...) {
  }
}

bool SkipCurrentChunk(ISpTTSEngineSite* site) {
  if (!site || (site->GetActions() & SPVES_SKIP) == 0) {
    return false;
  }
  SPVSKIPTYPE skipType = SPVST_SENTENCE;
  long skipCount = 0;
  if (FAILED(site->GetSkipInfo(&skipType, &skipCount)) || skipCount == 0) {
    return false;
  }
  site->CompleteSkip(1);
  return true;
}

}  // namespace

TtsEngine::TtsEngine() : ref_(1), token_(nullptr) {
  LockModule();
}

TtsEngine::~TtsEngine() {
  if (token_) {
    token_->Release();
    token_ = nullptr;
  }
  UnlockModule();
}

STDMETHODIMP TtsEngine::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) {
    return E_POINTER;
  }
  *ppv = nullptr;
  if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_ISpTTSEngine)) {
    *ppv = static_cast<ISpTTSEngine*>(this);
  } else if (IsEqualGUID(riid, IID_ISpObjectWithToken)) {
    *ppv = static_cast<ISpObjectWithToken*>(this);
  } else {
    return E_NOINTERFACE;
  }
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) TtsEngine::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&ref_));
}

STDMETHODIMP_(ULONG) TtsEngine::Release() {
  ULONG count = static_cast<ULONG>(InterlockedDecrement(&ref_));
  if (count == 0) {
    delete this;
  }
  return count;
}

STDMETHODIMP TtsEngine::SetObjectToken(ISpObjectToken* pToken) {
  if (!pToken) {
    return E_INVALIDARG;
  }
  if (token_) {
    token_->Release();
  }
  token_ = pToken;
  token_->AddRef();
  voiceId_ = L"pl_PL-bass-high";
  WCHAR* value = nullptr;
  if (SUCCEEDED(pToken->GetStringValue(L"VoiceId", &value)) && value) {
    voiceId_.assign(value);
    CoTaskMemFree(value);
  }
  WarmVoice(voiceId_);
  return S_OK;
}

STDMETHODIMP TtsEngine::GetObjectToken(ISpObjectToken** ppToken) {
  if (!ppToken) {
    return E_POINTER;
  }
  if (!token_) {
    *ppToken = nullptr;
    return E_UNEXPECTED;
  }
  token_->AddRef();
  *ppToken = token_;
  return S_OK;
}

STDMETHODIMP TtsEngine::GetOutputFormat(const GUID* /*pTargetFmtId*/,
                                       const WAVEFORMATEX* /*pTargetWaveFormatEx*/,
                                       GUID* pOutputFormatId,
                                       WAVEFORMATEX** ppCoMemOutputWaveFormatEx) {
  if (!pOutputFormatId || !ppCoMemOutputWaveFormatEx) {
    return E_POINTER;
  }
  WAVEFORMATEX* format = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
  if (!format) {
    return E_OUTOFMEMORY;
  }
  int rate = SynthSampleRate(voiceId_);
  format->wFormatTag = WAVE_FORMAT_PCM;
  format->nChannels = 1;
  format->nSamplesPerSec = static_cast<DWORD>(rate);
  format->wBitsPerSample = 16;
  format->nBlockAlign = 2;
  format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
  format->cbSize = 0;
  *pOutputFormatId = SPDFID_WaveFormatEx;
  *ppCoMemOutputWaveFormatEx = format;
  return S_OK;
}

STDMETHODIMP TtsEngine::Speak(DWORD /*dwSpeakFlags*/,
                             REFGUID /*rguidFormatId*/,
                             const WAVEFORMATEX* /*pWaveFormatEx*/,
                             const SPVTEXTFRAG* pTextFragList,
                             ISpTTSEngineSite* pOutputSite) {
  if (!pOutputSite || !pTextFragList) {
    return E_INVALIDARG;
  }
  std::wstring error;
  if (!SynthPrepare(voiceId_, error)) {
    LogLine(L"Speak: " + error);
    return E_FAIL;
  }

  int sampleRate = SynthSampleRate(voiceId_);
  ULONGLONG audioOffset = 0;
  int spoken = 0;
  int failed = 0;

  for (const SPVTEXTFRAG* frag = pTextFragList; frag; frag = frag->pNext) {
    if (Aborted(pOutputSite)) {
      return S_OK;
    }

    long rate = 0;
    pOutputSite->GetRate(&rate);
    USHORT volume = 100;
    pOutputSite->GetVolume(&volume);
    long fragRate = rate + frag->State.RateAdj;
    float speed = SpeedFromRate(fragRate);
    float gain = static_cast<float>(volume) / 100.0f;

    if (frag->State.eAction == SPVA_Silence) {
      HRESULT hr = WriteSilence(pOutputSite, sampleRate, frag->State.SilenceMSecs, audioOffset);
      if (hr == S_FALSE || Aborted(pOutputSite)) {
        return S_OK;
      }
      if (FAILED(hr)) {
        return hr;
      }
      continue;
    }

    if (frag->State.eAction == SPVA_Bookmark && frag->pTextStart && frag->ulTextLen > 0) {
      std::wstring mark(frag->pTextStart, frag->ulTextLen);
      SPEVENT event = {};
      event.eEventId = SPEI_TTS_BOOKMARK;
      event.elParamType = SPET_LPARAM_IS_STRING;
      event.ullAudioStreamOffset = audioOffset;
      event.lParam = reinterpret_cast<LPARAM>(mark.c_str());
      pOutputSite->AddEvents(&event, 1);
      continue;
    }

    if (!frag->pTextStart || frag->ulTextLen == 0) {
      if (frag->State.eAction == SPVA_Section) {
        HRESULT hr = WriteSilence(pOutputSite, sampleRate, 120, audioOffset);
        if (hr == S_FALSE || Aborted(pOutputSite)) {
          return S_OK;
        }
        if (FAILED(hr)) {
          return hr;
        }
      }
      continue;
    }

    std::wstring text(frag->pTextStart, frag->ulTextLen);
    std::vector<std::wstring> sentences = SplitForSpeech(text);
    if (sentences.empty() && HasSpeakableText(text)) {
      sentences.push_back(text);
    }
    ULONG textCursor = frag->ulTextSrcOffset;
    for (const std::wstring& sentence : sentences) {
      if (Aborted(pOutputSite)) {
        return S_OK;
      }
      if (SkipCurrentChunk(pOutputSite)) {
        textCursor += static_cast<ULONG>(sentence.size());
        continue;
      }
      pOutputSite->GetRate(&rate);
      pOutputSite->GetVolume(&volume);
      fragRate = rate + frag->State.RateAdj;
      speed = SpeedFromRate(fragRate);
      gain = static_cast<float>(volume) / 100.0f;

      AddBoundary(pOutputSite, audioOffset, textCursor, static_cast<ULONG>(sentence.size()));
      std::vector<int16_t> pcm;
      std::wstring synthError;
      bool canceled = false;
      if (!SynthSpeak(voiceId_, sentence, speed, gain, pcm, synthError, &canceled, SiteRequestsAbort, pOutputSite) ||
          canceled || Aborted(pOutputSite)) {
        if (canceled || Aborted(pOutputSite)) {
          return S_OK;
        }
        LogLine(synthError);
        ++failed;
      } else {
        HRESULT hr = WritePcm(pOutputSite, pcm, audioOffset);
        if (hr == S_FALSE || Aborted(pOutputSite)) {
          return S_OK;
        }
        if (FAILED(hr)) {
          return hr;
        }
        hr = WriteSilence(pOutputSite, sampleRate, 20, audioOffset);
        if (hr == S_FALSE || Aborted(pOutputSite)) {
          return S_OK;
        }
        if (FAILED(hr)) {
          return hr;
        }
        ++spoken;
      }
      textCursor += static_cast<ULONG>(sentence.size());
    }
  }

  if (spoken == 0 && failed > 0) {
    return E_FAIL;
  }
  return S_OK;
}
