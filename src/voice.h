#pragma once

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <string>

namespace google_tts {

class Voice : public ISpTTSEngine, public ISpObjectWithToken {
public:
    Voice();
    virtual ~Voice();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ISpTTSEngine
    STDMETHODIMP Speak(
        DWORD flags,
        REFGUID formatId,
        const WAVEFORMATEX* waveFormat,
        const SPVTEXTFRAG* fragments,
        ISpTTSEngineSite* site
    ) override;

    STDMETHODIMP GetOutputFormat(
        const GUID* targetFormatId,
        const WAVEFORMATEX* targetWaveFormat,
        GUID* outputFormatId,
        WAVEFORMATEX** outputWaveFormat
    ) override;

    // ISpObjectWithToken
    STDMETHODIMP SetObjectToken(ISpObjectToken* token) override;
    STDMETHODIMP GetObjectToken(ISpObjectToken** token) override;

private:
    float SapiRateToEngine(long rate) const;
    float SapiPitchToEngine(long pitch) const;

    LONG references_ = 1;
    ISpObjectToken* token_ = nullptr;

    std::string voiceId_;          // e.g. "en-us-x-multi"
    std::string fileId_;           // e.g. "en-us-x-multi-r83"
    std::string speaker_;          // e.g. "hol"
    std::string dependentVoiceId_; // e.g. "en-us-x-multi" (for seanet)
    std::string dependentFileId_;  // e.g. "en-us-x-multi-r83"
    std::wstring zvoicePath_;      // override path if any
};

} // namespace google_tts
