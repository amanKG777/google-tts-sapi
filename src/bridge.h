#pragma once

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sapi.h>
#include <sapiddk.h>

#include <string>
#include <vector>
#include <mutex>
#include <functional>

namespace google_tts {

class NativeTtsBridge {
public:
    static NativeTtsBridge& Instance();

    bool EnsureConnected();
    void Terminate();

    bool EnsureVoiceReady(
        const std::string& packageId,
        const std::string& fileId,
        const std::wstring& zvoicePath,
        const std::string& depPackageId = "",
        const std::string& depFileId = "",
        const std::wstring& depZvoicePath = L""
    );

    HRESULT Speak(
        const std::string& utf8Text,
        float rate,
        float pitch,
        const std::string& speakerCode,
        float gain,
        ISpTTSEngineSite* site,
        ULONGLONG* pAudioBytesWritten,
        bool* pAborted
    );

    void CancelCurrent();

    bool IsConnected() const;

private:
    NativeTtsBridge();
    ~NativeTtsBridge();

    bool StartServer();
    void CloseSockets();
    void KillProcess();

    bool SendFrame(SOCKET sock, uint8_t tag, const std::string& payload);
    bool RecvFrame(SOCKET sock, uint8_t& tag, std::vector<uint8_t>& payload, DWORD timeoutMs = 15000);
    bool RecvExact(SOCKET sock, uint8_t* buffer, size_t length, DWORD timeoutMs = 15000);

    std::wstring FindServerExe() const;
    std::wstring FindWasmPath() const;
    std::wstring GetVfsRoot() const;

    std::recursive_mutex mutex_;
    HANDLE hProcess_ = nullptr;
    HANDLE hThread_ = nullptr;
    SOCKET mainSock_ = INVALID_SOCKET;
    SOCKET ctrlSock_ = INVALID_SOCKET;
    int mainPort_ = 0;
    int ctrlPort_ = 0;
    std::string currentPackageId_;
    bool runtimeBusy_ = false;
    bool wsaInitialized_ = false;

    // High-speed RAM cache for single characters and short utterances (0ms response)
    static constexpr size_t kMaxCacheEntries = 512;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> pcmCache_;
};

} // namespace google_tts
