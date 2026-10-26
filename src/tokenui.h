#pragma once

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>

namespace google_tts {

class TokenUI : public ISpTokenUI {
public:
    TokenUI() = default;
    virtual ~TokenUI() = default;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ISpTokenUI
    STDMETHODIMP IsUISupported(
        LPCWSTR type,
        void* extra,
        ULONG extraBytes,
        IUnknown* object,
        BOOL* supported
    ) override;

    STDMETHODIMP DisplayUI(
        HWND parent,
        LPCWSTR title,
        LPCWSTR type,
        void* extra,
        ULONG extraBytes,
        ISpObjectToken* token,
        IUnknown* object
    ) override;

private:
    LONG references_ = 1;
};

} // namespace google_tts
