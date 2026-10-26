#include "catalog.h"
#include "json.h"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

extern HMODULE g_module;

namespace google_tts {

namespace {

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

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

} // namespace

VoiceCatalog& VoiceCatalog::Instance() {
    static VoiceCatalog instance;
    return instance;
}

std::wstring VoiceCatalog::GetModuleDir() {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
    }
    return std::wstring(path);
}

std::wstring VoiceCatalog::GetInstallDir() {
    // Check registry first
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\GoogleTTS", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH] = {0};
        DWORD size = sizeof(buf);
        if (RegQueryValueExW(key, L"InstallDir", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) {
            RegCloseKey(key);
            if (buf[0] != L'\0') {
                return std::wstring(buf);
            }
        }
        RegCloseKey(key);
    }
    return GetModuleDir();
}

std::wstring VoiceCatalog::GetDefaultVoiceDir() {
    wchar_t appData[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, appData))) {
        std::wstring dir = std::wstring(appData) + L"\\GoogleTTS\\voices";
        CreateDirectoryW((std::wstring(appData) + L"\\GoogleTTS").c_str(), nullptr);
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return GetInstallDir() + L"\\voices";
}

std::vector<std::wstring> VoiceCatalog::GetVoiceSearchDirs() {
    std::vector<std::wstring> dirs;

    // 1. {InstallDir}\voices
    dirs.push_back(GetInstallDir() + L"\\voices");

    // 2. {ModuleDir}\voices
    dirs.push_back(GetModuleDir() + L"\\voices");

    // 3. %ProgramData%\GoogleTTS\voices
    wchar_t commonAppData[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, commonAppData))) {
        dirs.push_back(std::wstring(commonAppData) + L"\\GoogleTTS\\voices");
    }

    // 4. %APPDATA%\nvda\googleTtsRs\voices and googleTtsForNvda\voices
    wchar_t userAppData[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, userAppData))) {
        dirs.push_back(std::wstring(userAppData) + L"\\nvda\\googleTtsRs\\voices");
        dirs.push_back(std::wstring(userAppData) + L"\\nvda\\googleTtsForNvda\\voices");
        dirs.push_back(std::wstring(userAppData) + L"\\googleTtsRs\\voices");
    }

    // 5. %LOCALAPPDATA%\googleTtsRs\voices
    wchar_t localAppData[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        dirs.push_back(std::wstring(localAppData) + L"\\googleTtsRs\\voices");
        dirs.push_back(std::wstring(localAppData) + L"\\GoogleTTS\\voices");
    }

    return dirs;
}

std::string VoiceCatalog::PackageIdToLanguage(const std::string& packageId) {
    // E.g. "en-us-x-multi" -> "en-US", "cmn-tw-x-multi" -> "cmn-TW", "hi-in-..." -> "hi-IN"
    size_t first = packageId.find('-');
    if (first == std::string::npos) return packageId;
    size_t second = packageId.find('-', first + 1);
    std::string langPart = packageId.substr(0, first);
    std::string countryPart = (second == std::string::npos) ? packageId.substr(first + 1) : packageId.substr(first + 1, second - first - 1);

    for (char& c : langPart) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : countryPart) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    return langPart + "-" + countryPart;
}

DWORD VoiceCatalog::LanguageToLCID(const std::string& language) {
    std::wstring wLang = Utf8ToWide(language);
    LCID lcid = LocaleNameToLCID(wLang.c_str(), 0);
    if (lcid != 0) return static_cast<DWORD>(lcid);

    // Fallbacks for common languages
    if (language == "en-US") return 0x0409;
    if (language == "en-GB") return 0x0809;
    if (language == "en-IN") return 0x4009;
    if (language == "hi-IN") return 0x0439;
    if (language == "es-ES") return 0x0c0a;
    if (language == "es-US") return 0x540a;
    if (language == "fr-FR") return 0x040c;
    if (language == "de-DE") return 0x0407;
    if (language == "it-IT") return 0x0410;
    if (language == "ja-JP") return 0x0411;
    if (language == "ko-KR") return 0x0412;
    if (language == "pt-BR") return 0x0416;
    if (language == "ru-RU") return 0x0419;
    if (language == "zh-CN" || language == "cmn-CN") return 0x0804;
    if (language == "zh-TW" || language == "cmn-TW") return 0x0404;

    return 0x0409; // Default to US English
}

std::wstring VoiceCatalog::LCIDToHexString(DWORD lcid) {
    wchar_t buf[16] = {0};
    wsprintfW(buf, L"%x", lcid);
    return std::wstring(buf);
}

bool VoiceCatalog::Load(const std::wstring& customCatalogPath) {
    if (loaded_ && customCatalogPath.empty()) return true;

    std::wstring catalogPath = customCatalogPath;
    if (catalogPath.empty()) {
        std::vector<std::wstring> candidates = {
            GetInstallDir() + L"\\WasmTtsEngine\\20260625.1\\voices.json",
            GetInstallDir() + L"\\voices.json",
            GetModuleDir() + L"\\WasmTtsEngine\\20260625.1\\voices.json",
            GetModuleDir() + L"\\voices.json",
            GetModuleDir() + L"\\..\\..\\..\\sample - nvda add-on\\googleTtsRs\\synthDrivers\\googleTtsRs\\WasmTtsEngine\\20260625.1\\voices.json",
            GetModuleDir() + L"\\..\\sample - nvda add-on\\googleTtsRs\\synthDrivers\\googleTtsRs\\WasmTtsEngine\\20260625.1\\voices.json",
            GetInstallDir() + L"\\..\\..\\..\\sample - nvda add-on\\googleTtsRs\\synthDrivers\\googleTtsRs\\WasmTtsEngine\\20260625.1\\voices.json"
        };
        for (const auto& path : candidates) {
            if (FileExists(path)) {
                catalogPath = path;
                break;
            }
        }
    }

    if (catalogPath.empty() || !FileExists(catalogPath)) {
        return false;
    }

    std::ifstream file(catalogPath);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();

    JsonValue root = JsonValue::parse(text);
    if (!root.is_array()) return false;

    packages_.clear();
    packageIndexById_.clear();

    for (const auto& item : root.as_array()) {
        if (!item.is_object()) continue;

        VoicePackage pkg;
        pkg.id = item["id"].as_string();
        pkg.fileId = item["fileId"].as_string();
        pkg.url = item["url"].as_string();
        pkg.sha256Checksum = item["sha256Checksum"].as_string();
        pkg.compressedSize = item["compressedSize"].as_int64();
        pkg.remote = item["remote"].as_bool(true);
        pkg.dependentVoiceId = item["dependentVoiceId"].as_string();
        pkg.language = PackageIdToLanguage(pkg.id);

        if (item.contains("speakers") && item["speakers"].is_array()) {
            for (const auto& spkItem : item["speakers"].as_array()) {
                if (!spkItem.is_object()) continue;
                Speaker spk;
                spk.speaker = spkItem["speaker"].as_string();
                spk.name = spkItem["name"].as_string();
                spk.gender = spkItem["gender"].as_string();
                spk.language = pkg.language;
                spk.packageId = pkg.id;
                spk.id = pkg.id + ":" + (spk.speaker.empty() ? spk.name : spk.speaker);
                pkg.speakers.push_back(spk);
            }
        }

        packageIndexById_[pkg.id] = packages_.size();
        packages_.push_back(pkg);
    }

    BuildSpeakers();
    loaded_ = true;
    return true;
}

void VoiceCatalog::BuildSpeakers() {
    allSpeakers_.clear();
    speakerIndexById_.clear();

    for (const auto& pkg : packages_) {
        for (const auto& spk : pkg.speakers) {
            speakerIndexById_[spk.id] = allSpeakers_.size();
            allSpeakers_.push_back(spk);
        }
    }
}

const VoicePackage* VoiceCatalog::FindPackageById(const std::string& packageId) const {
    auto it = packageIndexById_.find(packageId);
    if (it != packageIndexById_.end() && it->second < packages_.size()) {
        return &packages_[it->second];
    }
    return nullptr;
}

const Speaker* VoiceCatalog::FindSpeakerById(const std::string& speakerId) const {
    auto it = speakerIndexById_.find(speakerId);
    if (it != speakerIndexById_.end() && it->second < allSpeakers_.size()) {
        return &allSpeakers_[it->second];
    }
    return nullptr;
}

const VoicePackage* VoiceCatalog::FindPackageForSpeaker(const std::string& speakerId) const {
    const Speaker* spk = FindSpeakerById(speakerId);
    if (!spk) return nullptr;
    return FindPackageById(spk->packageId);
}

bool VoiceCatalog::IsPackageInstalled(const VoicePackage& pkg, std::wstring* outPath) const {
    std::wstring fileName = Utf8ToWide(pkg.fileName());
    for (const auto& dir : GetVoiceSearchDirs()) {
        std::wstring candidate = dir + L"\\" + fileName;
        if (FileExists(candidate)) {
            if (outPath) *outPath = candidate;
            return true;
        }
    }
    return false;
}

bool VoiceCatalog::IsPackageUsable(const VoicePackage& pkg, std::wstring* outPkgPath, std::wstring* outDepPath) const {
    if (!IsPackageInstalled(pkg, outPkgPath)) {
        return false;
    }
    if (!pkg.dependentVoiceId.empty()) {
        const VoicePackage* depPkg = FindPackageById(pkg.dependentVoiceId);
        if (!depPkg || !IsPackageInstalled(*depPkg, outDepPath)) {
            return false;
        }
    }
    return true;
}

std::vector<const VoicePackage*> VoiceCatalog::GetInstalledPackages() const {
    std::vector<const VoicePackage*> list;
    for (const auto& pkg : packages_) {
        if (IsPackageUsable(pkg)) {
            list.push_back(&pkg);
        }
    }
    return list;
}

std::vector<const Speaker*> VoiceCatalog::GetInstalledSpeakers() const {
    std::vector<const Speaker*> list;
    for (const auto& pkg : packages_) {
        if (IsPackageUsable(pkg)) {
            for (const auto& spk : pkg.speakers) {
                list.push_back(&spk);
            }
        }
    }
    return list;
}

} // namespace google_tts
