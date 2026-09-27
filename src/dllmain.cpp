#include "ModuleLock.h"
#include "Register.h"
#include "TtsEngine.h"
#include "guid.h"

#include <new>
#include <sapi.h>

namespace {

long g_moduleLocks = 0;

class ClassFactory : public IClassFactory {
 public:
  ClassFactory() : ref_(1) { LockModule(); }
  virtual ~ClassFactory() { UnlockModule(); }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) {
      return E_POINTER;
    }
    *ppv = nullptr;
    if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IClassFactory)) {
      *ppv = static_cast<IClassFactory*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }

  STDMETHODIMP_(ULONG) Release() override {
    ULONG count = static_cast<ULONG>(InterlockedDecrement(&ref_));
    if (count == 0) {
      delete this;
    }
    return count;
  }

  STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
    if (ppv) {
      *ppv = nullptr;
    }
    if (pUnkOuter) {
      return CLASS_E_NOAGGREGATION;
    }
    TtsEngine* engine = new (std::nothrow) TtsEngine();
    if (!engine) {
      return E_OUTOFMEMORY;
    }
    HRESULT hr = engine->QueryInterface(riid, ppv);
    engine->Release();
    return hr;
  }

  STDMETHODIMP LockServer(BOOL fLock) override {
    if (fLock) {
      LockModule();
    } else {
      UnlockModule();
    }
    return S_OK;
  }

 private:
  long ref_;
};

}  // namespace

void LockModule() { InterlockedIncrement(&g_moduleLocks); }

void UnlockModule() { InterlockedDecrement(&g_moduleLocks); }

long ModuleLockCount() { return g_moduleLocks; }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

extern "C" HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
  if (!ppv) {
    return E_POINTER;
  }
  *ppv = nullptr;
  if (!IsEqualGUID(rclsid, CLSID_PiperBassHigh)) {
    return CLASS_E_CLASSNOTAVAILABLE;
  }
  ClassFactory* factory = new (std::nothrow) ClassFactory();
  if (!factory) {
    return E_OUTOFMEMORY;
  }
  HRESULT hr = factory->QueryInterface(riid, ppv);
  factory->Release();
  return hr;
}

extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow() {
  return ModuleLockCount() == 0 ? S_OK : S_FALSE;
}

extern "C" HRESULT STDAPICALLTYPE DllRegisterServer() { return RegisterServer(); }

extern "C" HRESULT STDAPICALLTYPE DllUnregisterServer() { return UnregisterServer(); }
