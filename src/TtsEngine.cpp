#include "TtsEngine.h"

#include "Log.h"
#include "ModuleLock.h"
#include "Synth.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

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

struct SpeechPiece {
  std::wstring text;
  ULONG start = 0;
  bool sentenceStart = false;
};

struct WordMark {
  ULONG offset = 0;
  ULONG length = 0;
  ULONGLONG audioByte = 0;
};

std::atomic<long> g_activeSpeaks{0};

std::vector<SpeechPiece> SplitForSpeech(const std::wstring& text) {
  std::vector<SpeechPiece> parts;
  std::wstring current;
  ULONG currentStart = 0;
  bool sentenceStart = true;
  bool firstPiece = true;
  auto flush = [&](ULONG nextStart) {
    if (HasSpeakableText(current)) {
      parts.push_back(SpeechPiece{current, currentStart, sentenceStart});
      firstPiece = false;
      sentenceStart = false;
    }
    current.clear();
    currentStart = nextStart;
  };
  for (size_t i = 0; i < text.size(); ++i) {
    if (current.empty()) {
      currentStart = static_cast<ULONG>(i);
    }
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
    ULONG softLimit = firstPiece ? 24 : 48;
    ULONG hardLimit = firstPiece ? 36 : 64;
    if (!boundary && current.size() >= softLimit && (ch == L',' || ch == L':' || ch == L' ' || ch == L'\t')) {
      boundary = true;
    }
    if (!boundary && current.size() >= hardLimit) {
      boundary = true;
    }
    if (boundary) {
      bool endedSentence = ch == L'\n' || ch == L'!' || ch == L'?' || ch == L';' || ch == 0x2026 || ch == L'.';
      flush(static_cast<ULONG>(i + 1));
      if (endedSentence) {
        sentenceStart = true;
      }
    }
  }
  flush(static_cast<ULONG>(text.size()));
  return parts;
}

std::vector<WordMark> WordsInPiece(const SpeechPiece& piece,
                                     ULONG textBase,
                                     ULONGLONG audioStart,
                                     ULONGLONG audioBytes) {
  std::vector<WordMark> words;
  ULONG index = 0;
  while (index < piece.text.size()) {
    while (index < piece.text.size() && iswspace(piece.text[index])) {
      ++index;
    }
    if (index >= piece.text.size()) {
      break;
    }
    ULONG start = index;
    while (index < piece.text.size() && !iswspace(piece.text[index])) {
      ++index;
    }
    WordMark word;
    word.offset = textBase + piece.start + start;
    word.length = index - start;
    word.audioByte = 0;
    words.push_back(word);
  }
  if (words.empty() || audioBytes == 0) {
    return words;
  }
  ULONG total = 0;
  for (const WordMark& word : words) {
    total += word.length;
  }
  if (total == 0) {
    total = 1;
  }
  ULONG consumed = 0;
  for (WordMark& word : words) {
    ULONGLONG byte = audioBytes * consumed / total;
    byte -= byte % 2;
    word.audioByte = audioStart + byte;
    consumed += word.length;
  }
  return words;
}

bool Aborted(ISpTTSEngineSite* site) {
  if (!site) {
    return false;
  }
  return (site->GetActions() & SPVES_ABORT) != 0;
}

bool SkipCurrentChunk(ISpTTSEngineSite* site);

bool SiteRequestsAbort(void* argument) {
  ISpTTSEngineSite* site = static_cast<ISpTTSEngineSite*>(argument);
  if (!site) {
    return false;
  }
  DWORD actions = site->GetActions();
  return (actions & (SPVES_ABORT | SPVES_SKIP)) != 0;
}

void AddTextEvent(ISpTTSEngineSite* site,
                  SPEVENTENUM id,
                  ULONGLONG audioOffset,
                  ULONG textOffset,
                  ULONG textLength) {
  if (!site || textLength == 0) {
    return;
  }
  SPEVENT event = {};
  event.eEventId = id;
  event.elParamType = SPET_LPARAM_IS_UNDEFINED;
  event.ullAudioStreamOffset = audioOffset;
  event.wParam = static_cast<WPARAM>(textLength);
  event.lParam = static_cast<LPARAM>(textOffset);
  site->AddEvents(&event, 1);
}

enum class WriteResult { Ok, Abort, Skip, Failed };

WriteResult StopRequest(ISpTTSEngineSite* site) {
  if (Aborted(site)) {
    return WriteResult::Abort;
  }
  if (SkipCurrentChunk(site)) {
    return WriteResult::Skip;
  }
  return WriteResult::Ok;
}

WriteResult WritePcm(ISpTTSEngineSite* site,
                     const std::vector<int16_t>& pcm,
                     int sampleRate,
                     ULONGLONG& audioOffset,
                     const std::vector<WordMark>& words,
                     bool sentenceStart,
                     ULONG sentenceOffset,
                     ULONG sentenceLength) {
  if (pcm.empty() || sampleRate <= 0) {
    return WriteResult::Ok;
  }
  WriteResult stop = StopRequest(site);
  if (stop != WriteResult::Ok) {
    return stop;
  }
  const BYTE* data = reinterpret_cast<const BYTE*>(pcm.data());
  const size_t totalBytes = pcm.size() * sizeof(int16_t);
  size_t sliceBytes = static_cast<size_t>(sampleRate) * 25 / 1000 * sizeof(int16_t);
  if (sliceBytes < sizeof(int16_t)) {
    sliceBytes = sizeof(int16_t);
  }
  sliceBytes -= sliceBytes % sizeof(int16_t);

  bool sentenceSent = false;
  size_t nextWord = 0;
  size_t offset = 0;
  ULONGLONG started = GetTickCount64();
  ULONGLONG queuedMs = 0;
  while (offset < totalBytes) {
    stop = StopRequest(site);
    if (stop != WriteResult::Ok) {
      return stop;
    }
    ULONGLONG elapsed = GetTickCount64() - started;
    if (queuedMs > elapsed + 200) {
      Sleep(5);
      continue;
    }
    ULONGLONG sliceStart = audioOffset;
    if (sentenceStart && !sentenceSent) {
      AddTextEvent(site, SPEI_SENTENCE_BOUNDARY, sliceStart, sentenceOffset, sentenceLength);
      sentenceSent = true;
    }
    while (nextWord < words.size() && words[nextWord].audioByte <= sliceStart + sliceBytes) {
      AddTextEvent(site, SPEI_WORD_BOUNDARY, words[nextWord].audioByte, words[nextWord].offset, words[nextWord].length);
      ++nextWord;
    }
    size_t count = totalBytes - offset;
    if (count > sliceBytes) {
      count = sliceBytes;
    }
    ULONG written = 0;
    HRESULT hr = site->Write(data + offset, static_cast<ULONG>(count), &written);
    if (FAILED(hr)) {
      wchar_t code[16];
      wsprintfW(code, L"%08X", static_cast<unsigned>(hr));
      LogLine(std::wstring(L"Write audio failed: 0x") + code);
      return WriteResult::Failed;
    }
    if (written == 0) {
      written = static_cast<ULONG>(count);
    }
    offset += written;
    audioOffset += written;
    queuedMs += static_cast<ULONGLONG>(written) * 1000 / (static_cast<ULONGLONG>(sampleRate) * sizeof(int16_t));
    stop = StopRequest(site);
    if (stop != WriteResult::Ok) {
      return stop;
    }
  }
  while (nextWord < words.size()) {
    AddTextEvent(site, SPEI_WORD_BOUNDARY, words[nextWord].audioByte, words[nextWord].offset, words[nextWord].length);
    ++nextWord;
  }
  return WriteResult::Ok;
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
  WriteResult result = WritePcm(site, silence, sampleRate, audioOffset, {}, false, 0, 0);
  if (result == WriteResult::Failed) {
    return E_FAIL;
  }
  if (result == WriteResult::Abort || result == WriteResult::Skip) {
    return S_FALSE;
  }
  return S_OK;
}

float SpeedFromRate(long rate) {
  if (rate > 10) {
    rate = 10;
  } else if (rate < -10) {
    rate = -10;
  }
  return std::pow(2.0f, static_cast<float>(rate) / 10.0f);
}

bool WarmupShouldCancel(void*) {
  return g_activeSpeaks.load() > 0;
}

void WarmVoice(std::wstring voiceId) {
  try {
    std::thread([voiceId]() {
      LockModule();
      std::wstring error;
      if (SynthPrepare(voiceId, error) && g_activeSpeaks.load() == 0) {
        std::vector<int16_t> pcm;
        bool canceled = false;
        SynthSpeak(voiceId, L"A", 1.0f, 0.0f, pcm, error, &canceled, WarmupShouldCancel, nullptr);
      }
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
  struct SpeakGuard {
    SpeakGuard() { g_activeSpeaks.fetch_add(1); }
    ~SpeakGuard() { g_activeSpeaks.fetch_sub(1); }
  } speakGuard;

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
    std::vector<SpeechPiece> pieces = SplitForSpeech(text);
    if (pieces.empty() && HasSpeakableText(text)) {
      pieces.push_back(SpeechPiece{text, 0, true});
    }

    struct ReadyPiece {
      std::wstring text;
      std::vector<int16_t> pcm;
      ULONG start = 0;
      bool sentenceStart = false;
      bool ok = true;
      bool stopped = false;
    };
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::deque<ReadyPiece> queue;
    bool producerFinished = false;
    std::atomic<bool> cancelSynth{false};
    std::atomic<float> liveSpeed{speed};
    std::atomic<float> liveGain{gain};
    HANDLE cancelEvent = nullptr;
#ifndef _WIN64
    cancelEvent = CreateEventW(nullptr, TRUE, FALSE, L"Local\\MFPiperHostCancel");
    if (cancelEvent) {
      ResetEvent(cancelEvent);
    }
#endif
    auto requestCancel = [&]() {
      cancelSynth.store(true);
      if (cancelEvent) {
        SetEvent(cancelEvent);
      }
      queueCv.notify_all();
    };
    std::wstring voiceId = voiceId_;
    ULONG textBase = frag->ulTextSrcOffset;
    long rateAdjust = frag->State.RateAdj;
    std::thread producer([&]() {
      for (const SpeechPiece& piece : pieces) {
        if (cancelSynth.load()) {
          break;
        }
        {
          std::unique_lock<std::mutex> lock(queueMutex);
          queueCv.wait(lock, [&]() { return queue.size() < 1 || cancelSynth.load(); });
        }
        if (cancelSynth.load()) {
          break;
        }
        ReadyPiece item;
        item.text = piece.text;
        item.start = piece.start;
        item.sentenceStart = piece.sentenceStart;
        std::wstring synthError;
        bool canceled = false;
        if (!SynthSpeak(voiceId, piece.text, liveSpeed.load(), liveGain.load(), item.pcm, synthError, &canceled,
                        [](void* argument) { return static_cast<std::atomic<bool>*>(argument)->load(); },
                        &cancelSynth)) {
          LogLine(synthError);
          item.ok = false;
        } else if (canceled || cancelSynth.load()) {
          item.pcm.clear();
          item.stopped = true;
        }
        bool stopProducer = !item.ok || item.stopped;
        {
          std::lock_guard<std::mutex> lock(queueMutex);
          queue.push_back(std::move(item));
        }
        queueCv.notify_all();
        if (stopProducer) {
          break;
        }
      }
      {
        std::lock_guard<std::mutex> lock(queueMutex);
        producerFinished = true;
      }
      queueCv.notify_all();
    });
    struct JoinProducer {
      std::thread& worker;
      std::atomic<bool>& cancel;
      HANDLE event;
      std::condition_variable& cv;
      ~JoinProducer() {
        cancel.store(true);
        if (event) {
          SetEvent(event);
        }
        cv.notify_all();
        if (worker.joinable()) {
          worker.join();
        }
        if (event) {
          CloseHandle(event);
        }
      }
    } joinProducer{producer, cancelSynth, cancelEvent, queueCv};

    bool leaveSpeak = false;
    while (!leaveSpeak) {
      ReadyPiece item;
      {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCv.wait_for(lock, std::chrono::milliseconds(15), [&]() { return !queue.empty() || producerFinished; });
        if (queue.empty()) {
          if (producerFinished) {
            break;
          }
          lock.unlock();
          if (StopRequest(pOutputSite) == WriteResult::Abort) {
            requestCancel();
            leaveSpeak = true;
          }
          continue;
        }
        item = std::move(queue.front());
        queue.pop_front();
      }
      queueCv.notify_all();
      pOutputSite->GetRate(&rate);
      pOutputSite->GetVolume(&volume);
      liveSpeed.store(SpeedFromRate(rate + rateAdjust));
      liveGain.store(static_cast<float>(volume) / 100.0f);
      WriteResult stop = StopRequest(pOutputSite);
      if (stop == WriteResult::Abort) {
        requestCancel();
        leaveSpeak = true;
        break;
      }
      if (stop == WriteResult::Skip || item.stopped) {
        continue;
      }
      if (!item.ok) {
        ++failed;
        continue;
      }
      if (item.pcm.empty()) {
        continue;
      }
      ULONGLONG audioBytes = static_cast<ULONGLONG>(item.pcm.size() * sizeof(int16_t));
      SpeechPiece piece{item.text, item.start, item.sentenceStart};
      std::vector<WordMark> words = WordsInPiece(piece, textBase, audioOffset, audioBytes);
      WriteResult result = WritePcm(pOutputSite, item.pcm, sampleRate, audioOffset, words, item.sentenceStart,
                                    textBase + item.start, static_cast<ULONG>(item.text.size()));
      if (result == WriteResult::Failed) {
        requestCancel();
        return E_FAIL;
      }
      if (result == WriteResult::Abort) {
        requestCancel();
        return S_OK;
      }
      if (result == WriteResult::Skip) {
        continue;
      }
      ++spoken;
    }
    if (leaveSpeak || Aborted(pOutputSite)) {
      return S_OK;
    }
  }

  if (spoken == 0 && failed > 0) {
    return E_FAIL;
  }
  return S_OK;
}
