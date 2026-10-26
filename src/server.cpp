#include <windows.h>
#include <olectl.h>
#include <sapi.h>
#include <sapiddk.h>

#include <string>
#include <vector>

#include "guids.h"
#include "voice.h"
#include "tokenui.h"
#include "catalog.h"

HMODULE g_module = nullptr;
static LONG g_objects = 0;
static LONG g_locks = 0;

void GoogleTtsObjectReleased() {
    InterlockedDecrement(&g_objects);
}

namespace {

constexpr wchar_t kVoicesKey[] = L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens";
constexpr wchar_t kClassesKey[] = L"SOFTWARE\\Classes\\CLSID";

std::wstring GuidToString(REFGUID guid) {
    wchar_t buffer[64] = {0};
    StringFromGUID2(guid, buffer, 64);
    return std::wstring(buffer);
}

std::wstring ModulePath() {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    return std::wstring(path);
}

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &out[0], size);
    return out;
}

bool SetRegValue(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(
        key,
        name,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))
    ) == ERROR_SUCCESS;
}

HKEY CreateKeyWithViews(HKEY parent, const std::wstring& subkey, REGSAM extraSam = 0) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(parent, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE | extraSam, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        return hKey;
    }
    return nullptr;
}

bool RegisterComClass(REFGUID clsid, const wchar_t* description, REGSAM viewSam) {
    std::wstring clsidStr = GuidToString(clsid);
    std::wstring base = std::wstring(kClassesKey) + L"\\" + clsidStr;

    HKEY key = CreateKeyWithViews(HKEY_LOCAL_MACHINE, base, viewSam);
    if (!key) return false;
    SetRegValue(key, nullptr, description);
    RegCloseKey(key);

    key = CreateKeyWithViews(HKEY_LOCAL_MACHINE, base + L"\\InprocServer32", viewSam);
    if (!key) return false;
    SetRegValue(key, nullptr, ModulePath());
    SetRegValue(key, L"ThreadingModel", L"Both");
    RegCloseKey(key);
    return true;
}

bool RegisterSingleVoiceToken(
    const google_tts::Speaker& speaker,
    const google_tts::VoicePackage& pkg,
    const std::string& depFileId,
    REGSAM viewSam
) {
    std::string safeSpk = speaker.speaker.empty() ? "default" : speaker.speaker;
    std::wstring tokenKeyName = L"GoogleTTS_" + Utf8ToWide(pkg.id) + L"_" + Utf8ToWide(safeSpk);
    std::wstring basePath = std::wstring(kVoicesKey) + L"\\" + tokenKeyName;

    HKEY key = CreateKeyWithViews(HKEY_LOCAL_MACHINE, basePath, viewSam);
    if (!key) return false;

    std::wstring voiceName = Utf8ToWide(speaker.name);
    SetRegValue(key, nullptr, voiceName);
    SetRegValue(key, L"CLSID", GuidToString(CLSID_GoogleTtsEngine));
    SetRegValue(key, L"VoiceId", Utf8ToWide(pkg.id));
    SetRegValue(key, L"FileId", Utf8ToWide(pkg.fileId));
    SetRegValue(key, L"Speaker", Utf8ToWide(speaker.speaker));
    if (!pkg.dependentVoiceId.empty()) {
        SetRegValue(key, L"DependentVoiceId", Utf8ToWide(pkg.dependentVoiceId));
        SetRegValue(key, L"DependentFileId", Utf8ToWide(depFileId));
    }
    RegCloseKey(key);

    // Attributes
    key = CreateKeyWithViews(HKEY_LOCAL_MACHINE, basePath + L"\\Attributes", viewSam);
    if (key) {
        SetRegValue(key, L"Name", voiceName);
        std::wstring gender = (speaker.gender == "female") ? L"Female" : L"Male";
        SetRegValue(key, L"Gender", gender);
        SetRegValue(key, L"Age", L"Adult");
        SetRegValue(key, L"Vendor", L"Google");

        DWORD lcid = google_tts::VoiceCatalog::LanguageToLCID(speaker.language);
        SetRegValue(key, L"Language", google_tts::VoiceCatalog::LCIDToHexString(lcid));
        SetRegValue(key, L".", L"");
        RegCloseKey(key);
    }

    // TokenUI for Settings
    key = CreateKeyWithViews(HKEY_LOCAL_MACHINE, basePath + L"\\UI\\EngineProperties", viewSam);
    if (key) {
        SetRegValue(key, L"CLSID", GuidToString(CLSID_GoogleTtsTokenUI));
        RegCloseKey(key);
    }

    return true;
}

void UnregisterVoicesInView(REGSAM viewSam) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kVoicesKey, 0, KEY_READ | KEY_WRITE | viewSam, &key) == ERROR_SUCCESS) {
        std::vector<std::wstring> toDelete;
        wchar_t name[256];
        DWORD index = 0;
        DWORD nameLen = 256;
        while (RegEnumKeyExW(key, index++, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            if (wcsncmp(name, L"GoogleTTS_", 10) == 0) {
                toDelete.push_back(name);
            }
            nameLen = 256;
        }
        for (const auto& k : toDelete) {
            RegDeleteTreeW(key, k.c_str());
        }
        RegCloseKey(key);
    }

    // Delete classes
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(kClassesKey) + L"\\" + GuidToString(CLSID_GoogleTtsEngine)).c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(kClassesKey) + L"\\" + GuidToString(CLSID_GoogleTtsTokenUI)).c_str());
}

template <typename T>
class ClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;

        T* pObj = new (std::nothrow) T();
        if (!pObj) return E_OUTOFMEMORY;

        InterlockedIncrement(&g_objects);
        HRESULT hr = pObj->QueryInterface(riid, object);
        pObj->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) {
            InterlockedIncrement(&g_locks);
        } else {
            InterlockedDecrement(&g_locks);
        }
        return S_OK;
    }
};

ClassFactory<google_tts::Voice> g_voiceFactory;
ClassFactory<google_tts::TokenUI> g_tokenUIFactory;

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (clsid == CLSID_GoogleTtsEngine) {
        return g_voiceFactory.QueryInterface(riid, object);
    }
    if (clsid == CLSID_GoogleTtsTokenUI) {
        return g_tokenUIFactory.QueryInterface(riid, object);
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return (g_objects == 0 && g_locks == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    google_tts::VoiceCatalog& catalog = google_tts::VoiceCatalog::Instance();
    catalog.Load();

    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    bool is64BitOS = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64);

#ifdef _WIN64
    REGSAM classView = KEY_WOW64_64KEY;
#else
    REGSAM classView = is64BitOS ? KEY_WOW64_32KEY : 0;
#endif

    // Register COM classes ONLY in this DLL's bitness view
    if (!RegisterComClass(CLSID_GoogleTtsEngine, L"Google TTS SAPI Engine", classView)) {
        return SELFREG_E_CLASS;
    }
    if (!RegisterComClass(CLSID_GoogleTtsTokenUI, L"Google TTS Settings", classView)) {
        return SELFREG_E_CLASS;
    }

    // Register voice tokens in both 64-bit and 32-bit views
    std::vector<REGSAM> views = { 0 };
    if (is64BitOS) {
        views = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    }

    for (REGSAM view : views) {
        auto installedSpeakers = catalog.GetInstalledSpeakers();
        for (const auto* spk : installedSpeakers) {
            const auto* pkg = catalog.FindPackageById(spk->packageId);
            if (!pkg) continue;

            std::string depFileId;
            if (!pkg->dependentVoiceId.empty()) {
                const auto* depPkg = catalog.FindPackageById(pkg->dependentVoiceId);
                if (depPkg) depFileId = depPkg->fileId;
            }

            RegisterSingleVoiceToken(*spk, *pkg, depFileId, view);
        }
    }

    return S_OK;
}

STDAPI DllUnregisterServer() {
#ifdef _WIN64
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(kClassesKey) + L"\\" + GuidToString(CLSID_GoogleTtsEngine)).c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(kClassesKey) + L"\\" + GuidToString(CLSID_GoogleTtsTokenUI)).c_str());
#else
    HKEY kClass = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kClassesKey, 0, KEY_READ | KEY_WRITE | KEY_WOW64_32KEY, &kClass) == ERROR_SUCCESS) {
        RegDeleteTreeW(kClass, GuidToString(CLSID_GoogleTtsEngine).c_str());
        RegDeleteTreeW(kClass, GuidToString(CLSID_GoogleTtsTokenUI).c_str());
        RegCloseKey(kClass);
    }
#endif

    UnregisterVoicesInView(0);
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        UnregisterVoicesInView(KEY_WOW64_64KEY);
        UnregisterVoicesInView(KEY_WOW64_32KEY);
    }
    return S_OK;
}
