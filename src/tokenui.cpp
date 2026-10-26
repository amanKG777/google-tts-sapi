#include "tokenui.h"
#include "catalog.h"
#include <shellapi.h>

extern void GoogleTtsObjectReleased();

namespace google_tts {

STDMETHODIMP TokenUI::QueryInterface(REFIID riid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (riid == IID_IUnknown || riid == __uuidof(ISpTokenUI)) {
        *object = static_cast<ISpTokenUI*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) TokenUI::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&references_));
}

STDMETHODIMP_(ULONG) TokenUI::Release() {
    LONG count = InterlockedDecrement(&references_);
    if (count == 0) {
        GoogleTtsObjectReleased();
        delete this;
    }
    return static_cast<ULONG>(count);
}

STDMETHODIMP TokenUI::IsUISupported(
    LPCWSTR type,
    void* extra,
    ULONG extraBytes,
    IUnknown* object,
    BOOL* supported
) {
    if (!supported) return E_POINTER;
    *supported = (type && wcscmp(type, SPDUI_EngineProperties) == 0);
    return S_OK;
}

STDMETHODIMP TokenUI::DisplayUI(
    HWND parent,
    LPCWSTR title,
    LPCWSTR type,
    void* extra,
    ULONG extraBytes,
    ISpObjectToken* token,
    IUnknown* object
) {
    if (!type || wcscmp(type, SPDUI_EngineProperties) != 0) {
        return E_INVALIDARG;
    }

    // Launch google_tts_config.exe
    std::wstring configExe = VoiceCatalog::GetInstallDir() + L"\\google_tts_config.exe";
    if (GetFileAttributesW(configExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        configExe = VoiceCatalog::GetModuleDir() + L"\\google_tts_config.exe";
    }

    ShellExecuteW(parent, L"open", configExe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}

} // namespace google_tts
