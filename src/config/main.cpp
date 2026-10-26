#include <windows.h>
#include <commctrl.h>
#include <urlmon.h>
#include <shellapi.h>
#include <sapi.h>

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <algorithm>

#include "catalog.h"
#include "guids.h"
#include "json.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

HMODULE g_module = nullptr;

namespace {

// Control IDs
constexpr int IDC_TAB = 100;
constexpr int IDC_INSTALLED_SELECT_ALL = 101;
constexpr int IDC_INSTALLED_LIST = 102;
constexpr int IDC_BTN_REMOVE = 103;
constexpr int IDC_DOWNLOAD_SELECT_ALL = 104;
constexpr int IDC_DOWNLOAD_LIST = 105;
constexpr int IDC_BTN_DOWNLOAD = 106;
constexpr int IDC_STATUS_TEXT = 107;
constexpr int IDC_PROGRESS_BAR = 108;
constexpr int IDC_BTN_REFRESH = 109;
constexpr int IDC_BTN_OPEN_FOLDER = 110;
constexpr int IDC_BTN_CLOSE = 2; // IDCANCEL

inline HMENU MakeHMenu(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

HWND g_hWnd = nullptr;
HWND g_hTab = nullptr;

// Installed Tab controls
HWND g_hInstalledSelectAll = nullptr;
HWND g_hInstalledList = nullptr;
HWND g_hBtnRemove = nullptr;

// Download Tab controls
HWND g_hDownloadSelectAll = nullptr;
HWND g_hDownloadList = nullptr;
HWND g_hBtnDownload = nullptr;

// Status & Progress
HWND g_hStatus = nullptr;
HWND g_hProgress = nullptr;

// Bottom row buttons
HWND g_hBtnRefresh = nullptr;
HWND g_hBtnFolder = nullptr;
HWND g_hBtnClose = nullptr;

HFONT g_hFont = nullptr;
std::atomic<bool> g_busy = false;
bool g_updatingChecks = false;
bool g_startOnDownloadTab = false;
bool g_openVoiceManagerClicked = false;

std::vector<const google_tts::VoicePackage*> g_installedPackages;
std::vector<const google_tts::VoicePackage*> g_downloadPackages;

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &out[0], size);
    return out;
}

std::string WideToUtf8(const std::wstring& str) {
    if (str.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &out[0], size, nullptr, nullptr);
    return out;
}

std::wstring GuidToString(REFGUID guid) {
    wchar_t buffer[64] = {0};
    StringFromGUID2(guid, buffer, 64);
    return std::wstring(buffer);
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

constexpr wchar_t kVoicesKey[] = L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens";

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

void UnregisterPackageTokens(const std::string& packageId) {
    std::vector<REGSAM> views = { 0 };
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        views = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    }

    std::wstring prefix = L"GoogleTTS_" + Utf8ToWide(packageId) + L"_";

    for (REGSAM view : views) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kVoicesKey, 0, KEY_READ | KEY_WRITE | view, &key) == ERROR_SUCCESS) {
            std::vector<std::wstring> toDelete;
            wchar_t name[256];
            DWORD index = 0;
            DWORD nameLen = 256;
            while (RegEnumKeyExW(key, index++, name, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                if (wcsncmp(name, prefix.c_str(), prefix.size()) == 0) {
                    toDelete.push_back(name);
                }
                nameLen = 256;
            }
            for (const auto& k : toDelete) {
                RegDeleteTreeW(key, k.c_str());
            }
            RegCloseKey(key);
        }
    }
}

int RegisterAllInstalledVoices() {
    google_tts::VoiceCatalog& catalog = google_tts::VoiceCatalog::Instance();
    catalog.Load();

    std::wstring installDir = google_tts::VoiceCatalog::GetInstallDir();
    std::wstring dll64 = installDir + L"\\google_tts_sapi.dll";
    std::wstring dll32 = installDir + L"\\google_tts_sapi_32.dll";

    // 1. Current user classes (takes precedence in COM for current user processes)
    std::wstring hkcuClsid = L"Software\\Classes\\CLSID\\" + GuidToString(CLSID_GoogleTtsEngine);
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (hkcuClsid + L"\\InprocServer32").c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        SetRegValue(hKey, nullptr, dll64);
        SetRegValue(hKey, L"ThreadingModel", L"Both");
        RegCloseKey(hKey);
    }
    std::wstring hkcuUI = L"Software\\Classes\\CLSID\\" + GuidToString(CLSID_GoogleTtsTokenUI);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (hkcuUI + L"\\InprocServer32").c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        SetRegValue(hKey, nullptr, dll64);
        SetRegValue(hKey, L"ThreadingModel", L"Both");
        RegCloseKey(hKey);
    }

    // 2. HKLM 64-bit class
    std::wstring hklmClsid = L"SOFTWARE\\Classes\\CLSID\\" + GuidToString(CLSID_GoogleTtsEngine);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, (hklmClsid + L"\\InprocServer32").c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        SetRegValue(hKey, nullptr, dll64);
        SetRegValue(hKey, L"ThreadingModel", L"Both");
        RegCloseKey(hKey);
    }
    std::wstring hklmUI = L"SOFTWARE\\Classes\\CLSID\\" + GuidToString(CLSID_GoogleTtsTokenUI);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, (hklmUI + L"\\InprocServer32").c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        SetRegValue(hKey, nullptr, dll64);
        SetRegValue(hKey, L"ThreadingModel", L"Both");
        RegCloseKey(hKey);
    }

    // 3. HKLM 32-bit class (WOW64)
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, (hklmClsid + L"\\InprocServer32").c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_32KEY, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        SetRegValue(hKey, nullptr, dll32);
        SetRegValue(hKey, L"ThreadingModel", L"Both");
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, (hklmUI + L"\\InprocServer32").c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_32KEY, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        SetRegValue(hKey, nullptr, dll32);
        SetRegValue(hKey, L"ThreadingModel", L"Both");
        RegCloseKey(hKey);
    }

    std::vector<REGSAM> views = { 0 };
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        views = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    }

    int count = 0;
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

            if (RegisterSingleVoiceToken(*spk, *pkg, depFileId, view)) {
                if (view == views[0]) count++;
            }
        }
    }
    return count;
}

std::string SpeakerNames(const google_tts::VoicePackage& pkg) {
    std::string result;
    for (size_t i = 0; i < pkg.speakers.size(); i++) {
        std::string name = pkg.speakers[i].name.empty() ? pkg.speakers[i].speaker : pkg.speakers[i].name;
        if (!name.empty()) {
            if (!result.empty()) result += ", ";
            result += name;
        }
    }
    return result;
}

std::wstring FormatSize(int64_t bytes) {
    if (bytes <= 0) return L"";
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    wchar_t buf[32] = {0};
    swprintf_s(buf, L"%.1f MB", mb);
    return std::wstring(buf);
}

class DownloadCallback : public IBindStatusCallback {
public:
    HWND hwndProgress = nullptr;
    HWND hwndStatus = nullptr;
    std::wstring pkgId;
    int currentIndex = 0;
    int totalCount = 1;

    DownloadCallback(HWND hProg, HWND hStat, const std::wstring& id, int idx, int total)
        : hwndProgress(hProg), hwndStatus(hStat), pkgId(id), currentIndex(idx), totalCount(total) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IBindStatusCallback) {
            *ppv = static_cast<IBindStatusCallback*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP OnStartBinding(DWORD, IBinding*) override { return S_OK; }
    STDMETHODIMP GetPriority(LONG*) override { return S_OK; }
    STDMETHODIMP OnLowResource(DWORD) override { return S_OK; }
    STDMETHODIMP OnProgress(ULONG ulProgress, ULONG ulProgressMax, ULONG, LPCWSTR) override {
        if (ulProgressMax > 0 && totalCount > 0) {
            int filePct = static_cast<int>((ulProgress * 100ULL) / ulProgressMax);
            int overallPct = static_cast<int>((currentIndex * 100ULL + filePct) / totalCount);
            if (hwndProgress) {
                SendMessage(hwndProgress, PBM_SETPOS, overallPct, 0);
            }
            if (hwndStatus) {
                wchar_t buf[128] = {0};
                swprintf_s(buf, L"Downloading %d/%d: %s (%d%%)", currentIndex + 1, totalCount, pkgId.c_str(), filePct);
                SetWindowTextW(hwndStatus, buf);
            }
        }
        return S_OK;
    }
    STDMETHODIMP OnStopBinding(HRESULT, LPCWSTR) override { return S_OK; }
    STDMETHODIMP GetBindInfo(DWORD* grfBINDF, BINDINFO* pbindinfo) override {
        if (grfBINDF) *grfBINDF = BINDF_ASYNCHRONOUS | BINDF_NOWRITECACHE;
        return S_OK;
    }
    STDMETHODIMP OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
    STDMETHODIMP OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }
};

bool DownloadVoicePackage(const google_tts::VoicePackage& pkg, HWND hProg, HWND hStat, int idx, int total) {
    if (pkg.url.empty()) return false;

    std::wstring destDir = google_tts::VoiceCatalog::GetDefaultVoiceDir();
    CreateDirectoryW(destDir.c_str(), nullptr);
    std::wstring destFile = destDir + L"\\" + Utf8ToWide(pkg.fileName());

    std::wstring wUrl = Utf8ToWide(pkg.url);
    DownloadCallback callback(hProg, hStat, Utf8ToWide(pkg.id), idx, total);

    HRESULT hr = URLDownloadToFileW(
        nullptr,
        wUrl.c_str(),
        destFile.c_str(),
        0,
        &callback
    );

    return SUCCEEDED(hr);
}

void SetupListViewColumns(HWND hList, bool includeStatus) {
    ListView_DeleteAllItems(hList);
    while (ListView_DeleteColumn(hList, 0)) {}

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.pszText = const_cast<LPWSTR>(L"Language");
    col.cx = 110;
    col.iSubItem = 0;
    ListView_InsertColumn(hList, 0, &col);

    col.pszText = const_cast<LPWSTR>(L"Package");
    col.cx = 210;
    col.iSubItem = 1;
    ListView_InsertColumn(hList, 1, &col);

    col.pszText = const_cast<LPWSTR>(L"Voices");
    col.cx = 320;
    col.iSubItem = 2;
    ListView_InsertColumn(hList, 2, &col);

    col.pszText = const_cast<LPWSTR>(L"Size");
    col.cx = 100;
    col.iSubItem = 3;
    ListView_InsertColumn(hList, 3, &col);

    if (includeStatus) {
        col.pszText = const_cast<LPWSTR>(L"Status");
        col.cx = 120;
        col.iSubItem = 4;
        ListView_InsertColumn(hList, 4, &col);
    }
}

void InsertListViewRow(HWND hList, int index, const std::wstring& col0, const std::wstring& col1, const std::wstring& col2, const std::wstring& col3, const std::wstring& col4 = L"") {
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.iSubItem = 0;
    item.pszText = const_cast<LPWSTR>(col0.c_str());
    ListView_InsertItem(hList, &item);

    ListView_SetItemText(hList, index, 1, const_cast<LPWSTR>(col1.c_str()));
    ListView_SetItemText(hList, index, 2, const_cast<LPWSTR>(col2.c_str()));
    ListView_SetItemText(hList, index, 3, const_cast<LPWSTR>(col3.c_str()));
    if (!col4.empty()) {
        ListView_SetItemText(hList, index, 4, const_cast<LPWSTR>(col4.c_str()));
    }
}

std::vector<const google_tts::VoicePackage*> ExpandWithDependencies(
    const std::vector<const google_tts::VoicePackage*>& selected,
    google_tts::VoiceCatalog& catalog
) {
    std::vector<const google_tts::VoicePackage*> result;
    std::unordered_set<std::string> chosen;
    for (const auto* pkg : selected) {
        chosen.insert(pkg->id);
    }

    for (const auto* pkg : selected) {
        if (!pkg->dependentVoiceId.empty() && chosen.find(pkg->dependentVoiceId) == chosen.end()) {
            const auto* depPkg = catalog.FindPackageById(pkg->dependentVoiceId);
            if (depPkg && !catalog.IsPackageUsable(*depPkg)) {
                result.push_back(depPkg);
                chosen.insert(depPkg->id);
            }
        }
        result.push_back(pkg);
    }
    return result;
}

std::vector<const google_tts::VoicePackage*> GetCheckedDownloadPackages(int64_t* pTotalBytes, int* pCheckedCount) {
    google_tts::VoiceCatalog& catalog = google_tts::VoiceCatalog::Instance();
    std::vector<const google_tts::VoicePackage*> checked;
    for (int i = 0; i < static_cast<int>(g_downloadPackages.size()); i++) {
        if (ListView_GetCheckState(g_hDownloadList, i)) {
            checked.push_back(g_downloadPackages[i]);
        }
    }
    if (pCheckedCount) *pCheckedCount = static_cast<int>(checked.size());

    auto expanded = ExpandWithDependencies(checked, catalog);
    int64_t total = 0;
    for (const auto* pkg : expanded) {
        total += pkg->compressedSize;
    }
    if (pTotalBytes) *pTotalBytes = total;
    return expanded;
}

void UpdateDownloadButtonText() {
    int64_t totalBytes = 0;
    int checkedCount = 0;
    GetCheckedDownloadPackages(&totalBytes, &checkedCount);

    wchar_t buf[128] = {0};
    if (checkedCount == 0) {
        swprintf_s(buf, L"&Download selected voices, 0.0 MB");
    } else {
        double mb = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
        swprintf_s(buf, L"&Download selected voices, %.1f MB", mb);
    }
    SetWindowTextW(g_hBtnDownload, buf);
}

void RefreshButtons() {
    bool notBusy = !g_busy;
    bool hasInstalled = !g_installedPackages.empty();
    bool hasDownload = !g_downloadPackages.empty();

    EnableWindow(g_hBtnRefresh, notBusy);
    EnableWindow(g_hBtnFolder, notBusy);
    EnableWindow(g_hInstalledList, notBusy);
    EnableWindow(g_hDownloadList, notBusy);

    EnableWindow(g_hInstalledSelectAll, notBusy && hasInstalled);
    EnableWindow(g_hDownloadSelectAll, notBusy && hasDownload);
    EnableWindow(g_hBtnRemove, notBusy && hasInstalled);
    EnableWindow(g_hBtnDownload, notBusy && hasDownload);
}

void RefreshLists() {
    google_tts::VoiceCatalog& catalog = google_tts::VoiceCatalog::Instance();
    catalog.Load();

    g_installedPackages.clear();
    g_downloadPackages.clear();

    const auto& all = catalog.GetAllPackages();
    for (const auto& pkg : all) {
        if (catalog.IsPackageUsable(pkg)) {
            g_installedPackages.push_back(&pkg);
        } else {
            g_downloadPackages.push_back(&pkg);
        }
    }

    g_updatingChecks = true;

    // 1. Populate Installed List
    ListView_DeleteAllItems(g_hInstalledList);
    for (int i = 0; i < static_cast<int>(g_installedPackages.size()); i++) {
        const auto* pkg = g_installedPackages[i];
        InsertListViewRow(
            g_hInstalledList,
            i,
            Utf8ToWide(pkg->language),
            Utf8ToWide(pkg->id),
            Utf8ToWide(SpeakerNames(*pkg)),
            FormatSize(pkg->compressedSize),
            L"installed"
        );
    }
    SendMessage(g_hInstalledSelectAll, BM_SETCHECK, BST_UNCHECKED, 0);

    // 2. Populate Download List
    ListView_DeleteAllItems(g_hDownloadList);
    for (int i = 0; i < static_cast<int>(g_downloadPackages.size()); i++) {
        const auto* pkg = g_downloadPackages[i];
        InsertListViewRow(
            g_hDownloadList,
            i,
            Utf8ToWide(pkg->language),
            Utf8ToWide(pkg->id),
            Utf8ToWide(SpeakerNames(*pkg)),
            FormatSize(pkg->compressedSize)
        );
    }
    SendMessage(g_hDownloadSelectAll, BM_SETCHECK, BST_UNCHECKED, 0);

    g_updatingChecks = false;

    // Update status text: "{installed} installed; {total} available."
    wchar_t statusBuf[128] = {0};
    swprintf_s(statusBuf, L"%zu installed; %zu available.", g_installedPackages.size(), all.size());
    SetWindowTextW(g_hStatus, statusBuf);

    SendMessage(g_hProgress, PBM_SETPOS, 0, 0);

    UpdateDownloadButtonText();
    RefreshButtons();
}

void OnTabChanged() {
    int sel = TabCtrl_GetCurSel(g_hTab);
    if (sel == 0) {
        // Show Installed controls
        ShowWindow(g_hInstalledSelectAll, SW_SHOW);
        ShowWindow(g_hInstalledList, SW_SHOW);
        ShowWindow(g_hBtnRemove, SW_SHOW);

        // Hide Download controls
        ShowWindow(g_hDownloadSelectAll, SW_HIDE);
        ShowWindow(g_hDownloadList, SW_HIDE);
        ShowWindow(g_hBtnDownload, SW_HIDE);

        SetFocus(g_hInstalledList);
    } else {
        // Hide Installed controls
        ShowWindow(g_hInstalledSelectAll, SW_HIDE);
        ShowWindow(g_hInstalledList, SW_HIDE);
        ShowWindow(g_hBtnRemove, SW_HIDE);

        // Show Download controls
        ShowWindow(g_hDownloadSelectAll, SW_SHOW);
        ShowWindow(g_hDownloadList, SW_SHOW);
        ShowWindow(g_hBtnDownload, SW_SHOW);

        SetFocus(g_hDownloadList);
    }
}

void OnInstalledListCheckChanged() {
    if (g_updatingChecks) return;
    int count = ListView_GetItemCount(g_hInstalledList);
    if (count == 0) return;

    bool allChecked = true;
    for (int i = 0; i < count; i++) {
        if (!ListView_GetCheckState(g_hInstalledList, i)) {
            allChecked = false;
            break;
        }
    }
    g_updatingChecks = true;
    SendMessage(g_hInstalledSelectAll, BM_SETCHECK, allChecked ? BST_CHECKED : BST_UNCHECKED, 0);
    g_updatingChecks = false;
}

void OnDownloadListCheckChanged() {
    if (g_updatingChecks) return;
    int count = ListView_GetItemCount(g_hDownloadList);
    if (count == 0) return;

    bool allChecked = true;
    for (int i = 0; i < count; i++) {
        if (!ListView_GetCheckState(g_hDownloadList, i)) {
            allChecked = false;
            break;
        }
    }
    g_updatingChecks = true;
    SendMessage(g_hDownloadSelectAll, BM_SETCHECK, allChecked ? BST_CHECKED : BST_UNCHECKED, 0);
    g_updatingChecks = false;

    UpdateDownloadButtonText();
}

void OnInstalledSelectAllClicked() {
    bool check = (SendMessage(g_hInstalledSelectAll, BM_GETCHECK, 0, 0) == BST_CHECKED);
    int count = ListView_GetItemCount(g_hInstalledList);
    g_updatingChecks = true;
    for (int i = 0; i < count; i++) {
        ListView_SetCheckState(g_hInstalledList, i, check);
    }
    g_updatingChecks = false;
}

void OnDownloadSelectAllClicked() {
    bool check = (SendMessage(g_hDownloadSelectAll, BM_GETCHECK, 0, 0) == BST_CHECKED);
    int count = ListView_GetItemCount(g_hDownloadList);
    g_updatingChecks = true;
    for (int i = 0; i < count; i++) {
        ListView_SetCheckState(g_hDownloadList, i, check);
    }
    g_updatingChecks = false;
    UpdateDownloadButtonText();
}

void DoDownload() {
    int64_t totalBytes = 0;
    int checkedCount = 0;
    auto packages = GetCheckedDownloadPackages(&totalBytes, &checkedCount);

    if (packages.empty()) {
        SetWindowTextW(g_hStatus, L"No voice packages selected.");
        return;
    }

    g_busy = true;
    RefreshButtons();

    int totalCount = static_cast<int>(packages.size());

    std::thread([packages, totalCount]() {
        int succeeded = 0;
        std::vector<std::string> failed;

        for (int i = 0; i < totalCount; i++) {
            const auto* pkg = packages[i];
            if (DownloadVoicePackage(*pkg, g_hProgress, g_hStatus, i, totalCount)) {
                succeeded++;
            } else {
                failed.push_back(pkg->id);
            }
        }

        SetWindowTextW(g_hStatus, L"Registering voices in SAPI...");
        RegisterAllInstalledVoices();

        g_busy = false;

        // Notify main thread
        PostMessage(g_hWnd, WM_USER + 1, static_cast<WPARAM>(succeeded), static_cast<LPARAM>(totalCount));
    }).detach();
}

void DoRemove() {
    std::vector<const google_tts::VoicePackage*> checked;
    for (int i = 0; i < static_cast<int>(g_installedPackages.size()); i++) {
        if (ListView_GetCheckState(g_hInstalledList, i)) {
            checked.push_back(g_installedPackages[i]);
        }
    }

    if (checked.empty()) {
        SetWindowTextW(g_hStatus, L"No installed voice packages selected.");
        return;
    }

    std::wstring confirmMsg;
    if (checked.size() == 1) {
        confirmMsg = L"Remove " + Utf8ToWide(checked[0]->id) + L"?";
    } else {
        confirmMsg = L"Remove " + std::to_wstring(checked.size()) + L" voice packages?\n";
        for (size_t i = 0; i < checked.size(); i++) {
            if (i > 0) confirmMsg += L", ";
            confirmMsg += Utf8ToWide(checked[i]->id);
            if (i >= 8 && checked.size() > 10) {
                confirmMsg += L"...";
                break;
            }
        }
    }

    if (MessageBoxW(g_hWnd, confirmMsg.c_str(), L"Google TTS Voice Manager", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    google_tts::VoiceCatalog& catalog = google_tts::VoiceCatalog::Instance();
    int count = 0;
    for (const auto* pkg : checked) {
        std::wstring pkgPath;
        if (catalog.IsPackageInstalled(*pkg, &pkgPath)) {
            DeleteFileW(pkgPath.c_str());
        }
        UnregisterPackageTokens(pkg->id);
        count++;
    }

    wchar_t statusBuf[128] = {0};
    swprintf_s(statusBuf, L"Removed %d voice packages.", count);
    SetWindowTextW(g_hStatus, statusBuf);

    RefreshLists();
}

void Relayout(int W, int H) {
    if (W <= 0 || H <= 0 || !g_hTab) return;

    // Bottom buttons
    int btnY = H - 45;
    MoveWindow(g_hBtnRefresh, 15, btnY, 110, 32, TRUE);
    MoveWindow(g_hBtnFolder, 135, btnY, 185, 32, TRUE);
    MoveWindow(g_hBtnClose, W - 135, btnY, 120, 32, TRUE);

    // Status row
    int statusY = H - 80;
    int progressW = 210;
    int statusW = W - progressW - 45;
    MoveWindow(g_hStatus, 15, statusY, statusW, 22, TRUE);
    MoveWindow(g_hProgress, W - progressW - 15, statusY, progressW, 20, TRUE);

    // Tab control
    int tabW = W - 20;
    int tabH = H - 95;
    MoveWindow(g_hTab, 10, 10, tabW, tabH, TRUE);

    // Controls inside active tab area
    int listX = 20;
    int listY = 70;
    int listW = W - 40;
    int listH = tabH - 110;
    if (listH < 100) listH = 100;

    int actBtnY = listY + listH + 8;

    MoveWindow(g_hInstalledSelectAll, 22, 42, 250, 24, TRUE);
    MoveWindow(g_hInstalledList, listX, listY, listW, listH, TRUE);
    MoveWindow(g_hBtnRemove, listX, actBtnY, 220, 32, TRUE);

    MoveWindow(g_hDownloadSelectAll, 22, 42, 250, 24, TRUE);
    MoveWindow(g_hDownloadList, listX, listY, listW, listH, TRUE);
    MoveWindow(g_hBtnDownload, listX, actBtnY, 340, 32, TRUE);
}

void ApplyFontToControls() {
    HWND controls[] = {
        g_hTab,
        g_hInstalledSelectAll,
        g_hInstalledList,
        g_hBtnRemove,
        g_hDownloadSelectAll,
        g_hDownloadList,
        g_hBtnDownload,
        g_hStatus,
        g_hBtnRefresh,
        g_hBtnFolder,
        g_hBtnClose
    };
    for (HWND h : controls) {
        if (h) SendMessage(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_PROGRESS_CLASS | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES };
            InitCommonControlsEx(&icex);

            LOGFONTW lf = {};
            lf.lfHeight = -13; // ~10pt
            lf.lfWeight = FW_NORMAL;
            wcscpy_s(lf.lfFaceName, L"Segoe UI");
            g_hFont = CreateFontIndirectW(&lf);

            // Tab Control
            g_hTab = CreateWindowExW(
                0,
                WC_TABCONTROLW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                10, 10, 840, 480,
                hWnd, MakeHMenu(IDC_TAB), nullptr, nullptr
            );

            TCITEMW tci = {};
            tci.mask = TCIF_TEXT;
            tci.pszText = const_cast<LPWSTR>(L"Installed");
            TabCtrl_InsertItem(g_hTab, 0, &tci);
            tci.pszText = const_cast<LPWSTR>(L"Download");
            TabCtrl_InsertItem(g_hTab, 1, &tci);

            // --- Installed Tab Controls ---
            g_hInstalledSelectAll = CreateWindowW(
                L"BUTTON",
                L"Select &all voices",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                22, 42, 250, 24,
                hWnd, MakeHMenu(IDC_INSTALLED_SELECT_ALL), nullptr, nullptr
            );

            g_hInstalledList = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                WC_LISTVIEWW,
                L"Installed voice packages",
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                20, 70, 820, 360,
                hWnd, MakeHMenu(IDC_INSTALLED_LIST), nullptr, nullptr
            );
            ListView_SetExtendedListViewStyle(g_hInstalledList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            SetupListViewColumns(g_hInstalledList, true);

            g_hBtnRemove = CreateWindowW(
                L"BUTTON",
                L"&Remove checked voices",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                20, 440, 220, 32,
                hWnd, MakeHMenu(IDC_BTN_REMOVE), nullptr, nullptr
            );

            // --- Download Tab Controls ---
            g_hDownloadSelectAll = CreateWindowW(
                L"BUTTON",
                L"Select &all voices",
                WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                22, 42, 250, 24,
                hWnd, MakeHMenu(IDC_DOWNLOAD_SELECT_ALL), nullptr, nullptr
            );

            g_hDownloadList = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                WC_LISTVIEWW,
                L"Downloadable voice packages",
                WS_CHILD | WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                20, 70, 820, 360,
                hWnd, MakeHMenu(IDC_DOWNLOAD_LIST), nullptr, nullptr
            );
            ListView_SetExtendedListViewStyle(g_hDownloadList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            SetupListViewColumns(g_hDownloadList, false);

            g_hBtnDownload = CreateWindowW(
                L"BUTTON",
                L"&Download selected voices, 0.0 MB",
                WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                20, 440, 340, 32,
                hWnd, MakeHMenu(IDC_BTN_DOWNLOAD), nullptr, nullptr
            );

            // --- Status & Progress ---
            g_hStatus = CreateWindowW(
                L"STATIC",
                L"Ready.",
                WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                15, 510, 600, 22,
                hWnd, MakeHMenu(IDC_STATUS_TEXT), nullptr, nullptr
            );

            g_hProgress = CreateWindowExW(
                0,
                PROGRESS_CLASSW,
                nullptr,
                WS_CHILD | WS_VISIBLE,
                630, 510, 210, 20,
                hWnd, MakeHMenu(IDC_PROGRESS_BAR), nullptr, nullptr
            );
            SendMessage(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

            // --- Bottom Buttons ---
            g_hBtnRefresh = CreateWindowW(
                L"BUTTON",
                L"&Refresh",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                15, 545, 110, 32,
                hWnd, MakeHMenu(IDC_BTN_REFRESH), nullptr, nullptr
            );

            g_hBtnFolder = CreateWindowW(
                L"BUTTON",
                L"&Open voices folder",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                135, 545, 185, 32,
                hWnd, MakeHMenu(IDC_BTN_OPEN_FOLDER), nullptr, nullptr
            );

            g_hBtnClose = CreateWindowW(
                L"BUTTON",
                L"Close",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                725, 545, 120, 32,
                hWnd, MakeHMenu(IDC_BTN_CLOSE), nullptr, nullptr
            );

            ApplyFontToControls();
            RefreshLists();
            if (g_startOnDownloadTab) {
                TabCtrl_SetCurSel(g_hTab, 1);
                OnTabChanged();
                SetWindowTextW(g_hStatus, L"No voices installed. Go to Voice Manager to download some.");
                SetFocus(g_hDownloadList);
            } else {
                OnTabChanged();
            }
            break;
        }

        case WM_SIZE: {
            int W = LOWORD(lParam);
            int H = HIWORD(lParam);
            Relayout(W, H);
            break;
        }

        case WM_GETMINMAXINFO: {
            LPMINMAXINFO mmi = reinterpret_cast<LPMINMAXINFO>(lParam);
            mmi->ptMinTrackSize.x = 720;
            mmi->ptMinTrackSize.y = 520;
            return 0;
        }

        case WM_NOTIFY: {
            NMHDR* pnmh = reinterpret_cast<NMHDR*>(lParam);
            if (!pnmh) break;

            if (pnmh->idFrom == IDC_TAB && pnmh->code == TCN_SELCHANGE) {
                OnTabChanged();
                break;
            }

            if (pnmh->idFrom == IDC_DOWNLOAD_LIST && pnmh->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* pnmv = reinterpret_cast<NMLISTVIEW*>(lParam);
                if ((pnmv->uChanged & LVIF_STATE) &&
                    ((pnmv->uOldState & LVIS_STATEIMAGEMASK) != (pnmv->uNewState & LVIS_STATEIMAGEMASK))) {
                    OnDownloadListCheckChanged();
                }
                break;
            }

            if (pnmh->idFrom == IDC_INSTALLED_LIST && pnmh->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* pnmv = reinterpret_cast<NMLISTVIEW*>(lParam);
                if ((pnmv->uChanged & LVIF_STATE) &&
                    ((pnmv->uOldState & LVIS_STATEIMAGEMASK) != (pnmv->uNewState & LVIS_STATEIMAGEMASK))) {
                    OnInstalledListCheckChanged();
                }
                break;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (code == BN_CLICKED) {
                switch (id) {
                    case IDC_INSTALLED_SELECT_ALL:
                        OnInstalledSelectAllClicked();
                        break;
                    case IDC_DOWNLOAD_SELECT_ALL:
                        OnDownloadSelectAllClicked();
                        break;
                    case IDC_BTN_DOWNLOAD:
                        DoDownload();
                        break;
                    case IDC_BTN_REMOVE:
                        DoRemove();
                        break;
                    case IDC_BTN_REFRESH:
                        RefreshLists();
                        break;
                    case IDC_BTN_OPEN_FOLDER: {
                        std::wstring dir = google_tts::VoiceCatalog::GetDefaultVoiceDir();
                        ShellExecuteW(hWnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        break;
                    }
                    case IDC_BTN_CLOSE:
                        SendMessage(hWnd, WM_CLOSE, 0, 0);
                        break;
                }
            }
            break;
        }

        case WM_USER + 1: {
            int succeeded = static_cast<int>(wParam);
            int total = static_cast<int>(lParam);

            RefreshLists();

            wchar_t msgBuf[128] = {0};
            if (succeeded == total) {
                if (succeeded == 1) {
                    swprintf_s(msgBuf, L"Downloaded 1 voice package.");
                } else {
                    swprintf_s(msgBuf, L"Downloaded %d voice packages.", succeeded);
                }
            } else {
                swprintf_s(msgBuf, L"Downloaded %d of %d voice packages.", succeeded, total);
            }
            SetWindowTextW(g_hStatus, msgBuf);
            break;
        }

        case WM_CLOSE:
            if (g_busy) {
                MessageBoxW(hWnd, L"A voice operation is still running.", L"Google TTS Voice Manager", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            if (g_hFont) {
                DeleteObject(g_hFont);
                g_hFont = nullptr;
            }
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK NoVoicesWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HWND hText = CreateWindowW(
                L"STATIC",
                L"No voices installed. Go to Voice Manager to download some.",
                WS_CHILD | WS_VISIBLE,
                25, 25, 410, 45,
                hWnd, nullptr, nullptr, nullptr
            );

            HWND hBtnOpen = CreateWindowW(
                L"BUTTON",
                L"&Open Voice Manager",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                110, 80, 165, 34,
                hWnd, MakeHMenu(IDOK), nullptr, nullptr
            );

            HWND hBtnClose = CreateWindowW(
                L"BUTTON",
                L"&Close",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                285, 80, 110, 34,
                hWnd, MakeHMenu(IDCANCEL), nullptr, nullptr
            );

            if (g_hFont) {
                SendMessage(hText, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
                SendMessage(hBtnOpen, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
                SendMessage(hBtnClose, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
            }
            SetFocus(hBtnOpen);
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDOK) {
                g_openVoiceManagerClicked = true;
                DestroyWindow(hWnd);
            } else if (id == IDCANCEL) {
                g_openVoiceManagerClicked = false;
                DestroyWindow(hWnd);
            }
            break;
        }

        case WM_CLOSE:
            g_openVoiceManagerClicked = false;
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

bool ShowNoVoicesPrompt(HINSTANCE hInstance) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = NoVoicesWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"GoogleTtsNoVoicesDialog";

    RegisterClassExW(&wc);

    int width = 450;
    int height = 175;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        wc.lpszClassName,
        L"Google TTS Voice Manager",
        WS_VISIBLE | WS_CAPTION | WS_SYSMENU,
        x, y, width, height,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hDlg) return false;

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return g_openVoiceManagerClicked;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    std::wstring cmd = lpCmdLine ? lpCmdLine : L"";

    // CLI mode: --register
    if (cmd.find(L"--register") != std::wstring::npos) {
        int registered = RegisterAllInstalledVoices();
        std::wcout << L"Registered " << registered << L" Google TTS voices into SAPI." << std::endl;
        return 0;
    }

    // Initialize default UI font early for all windows
    LOGFONTW lf = {};
    lf.lfHeight = -13; // ~10pt
    lf.lfWeight = FW_NORMAL;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g_hFont = CreateFontIndirectW(&lf);

    // Check if any voices are currently installed
    google_tts::VoiceCatalog& catalog = google_tts::VoiceCatalog::Instance();
    catalog.Load();
    auto installed = catalog.GetInstalledPackages();

    bool forceNoVoicesTest = (cmd.find(L"--test-no-voices") != std::wstring::npos);
    if (installed.empty() || forceNoVoicesTest) {
        if (!ShowNoVoicesPrompt(hInstance)) {
            if (g_hFont) {
                DeleteObject(g_hFont);
                g_hFont = nullptr;
            }
            return 0; // User clicked Close
        }
        g_startOnDownloadTab = true;
    }

    // GUI mode
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"GoogleTtsVoiceManager";

    RegisterClassExW(&wc);

    // Initial size: 880x640 (matching python dialog)
    int width = 880;
    int height = 640;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    g_hWnd = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        wc.lpszClassName,
        L"Google TTS Voice Manager",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, width, height,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hWnd) return 1;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(g_hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}
