#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <windows.h>

namespace google_tts {

struct Speaker {
    std::string id;         // e.g. "en-us-x-multi:hol"
    std::string name;       // e.g. "Google US English 1 (Natural)"
    std::string language;   // e.g. "en-US"
    std::string packageId;  // e.g. "en-us-x-multi-seanet"
    std::string speaker;    // e.g. "hol"
    std::string gender;     // e.g. "female" or "male"
};

struct VoicePackage {
    std::string id;
    std::string fileId;
    std::string url;
    std::string sha256Checksum;
    int64_t compressedSize = 0;
    bool remote = true;
    std::string dependentVoiceId;
    std::string language;
    std::vector<Speaker> speakers;

    std::string fileName() const {
        return id + ".zvoice";
    }
};

class VoiceCatalog {
public:
    static VoiceCatalog& Instance();

    bool Load(const std::wstring& customCatalogPath = L"");

    const std::vector<VoicePackage>& GetAllPackages() const { return packages_; }
    const std::vector<Speaker>& GetAllSpeakers() const { return allSpeakers_; }

    const VoicePackage* FindPackageById(const std::string& packageId) const;
    const Speaker* FindSpeakerById(const std::string& speakerId) const;
    const VoicePackage* FindPackageForSpeaker(const std::string& speakerId) const;

    // Checks if the .zvoice file exists in any of the search locations
    bool IsPackageInstalled(const VoicePackage& pkg, std::wstring* outPath = nullptr) const;
    bool IsPackageUsable(const VoicePackage& pkg, std::wstring* outPkgPath = nullptr, std::wstring* outDepPath = nullptr) const;

    // List of packages whose .zvoice files are currently on disk
    std::vector<const VoicePackage*> GetInstalledPackages() const;
    std::vector<const Speaker*> GetInstalledSpeakers() const;

    // Standard directory resolution
    static std::wstring GetModuleDir();
    static std::wstring GetInstallDir();
    static std::wstring GetDefaultVoiceDir();
    static std::vector<std::wstring> GetVoiceSearchDirs();

    // Helper functions
    static std::string PackageIdToLanguage(const std::string& packageId);
    static DWORD LanguageToLCID(const std::string& language);
    static std::wstring LCIDToHexString(DWORD lcid);

private:
    VoiceCatalog() = default;
    void BuildSpeakers();

    bool loaded_ = false;
    std::vector<VoicePackage> packages_;
    std::vector<Speaker> allSpeakers_;
    std::map<std::string, size_t> packageIndexById_;
    std::map<std::string, size_t> speakerIndexById_;
};

} // namespace google_tts
