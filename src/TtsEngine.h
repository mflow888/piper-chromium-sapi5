#pragma once

#include <sapi.h>
#include <sapiddk.h>

#include <string>

class TtsEngine : public ISpTTSEngine, public ISpObjectWithToken {
 public:
  TtsEngine();
  virtual ~TtsEngine();

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP Speak(DWORD dwSpeakFlags,
                     REFGUID rguidFormatId,
                     const WAVEFORMATEX* pWaveFormatEx,
                     const SPVTEXTFRAG* pTextFragList,
                     ISpTTSEngineSite* pOutputSite) override;
  STDMETHODIMP GetOutputFormat(const GUID* pTargetFmtId,
                               const WAVEFORMATEX* pTargetWaveFormatEx,
                               GUID* pOutputFormatId,
                               WAVEFORMATEX** ppCoMemOutputWaveFormatEx) override;

  STDMETHODIMP SetObjectToken(ISpObjectToken* pToken) override;
  STDMETHODIMP GetObjectToken(ISpObjectToken** ppToken) override;

 private:
  long ref_;
  ISpObjectToken* token_;
  std::wstring voiceId_ = L"pl_PL-bass-high";
};
