#include "bridge.h"
#include "protocol.h"
#include "catalog.h"
#include "json.h"

#include <shlobj.h>
#include <chrono>
#include <thread>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

namespace google_tts {

namespace {

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

NativeTtsBridge& NativeTtsBridge::Instance() {
    static NativeTtsBridge instance;
    return instance;
}

NativeTtsBridge::NativeTtsBridge() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
        wsaInitialized_ = true;
    }
}

NativeTtsBridge::~NativeTtsBridge() {
    Terminate();
    if (wsaInitialized_) {
        WSACleanup();
        wsaInitialized_ = false;
    }
}

bool NativeTtsBridge::IsConnected() const {
    if (hProcess_ == nullptr || mainSock_ == INVALID_SOCKET) {
        return false;
    }
    DWORD exitCode = 0;
    if (GetExitCodeProcess(hProcess_, &exitCode) && exitCode != STILL_ACTIVE) {
        return false;
    }
    return true;
}

std::wstring NativeTtsBridge::FindServerExe() const {
    wchar_t envBuf[MAX_PATH] = {0};
    if (GetEnvironmentVariableW(L"GTTS_NATIVE_SERVER_EXE", envBuf, MAX_PATH) > 0 && FileExists(envBuf)) {
        return std::wstring(envBuf);
    }

    std::wstring installDir = VoiceCatalog::GetInstallDir();
    std::wstring moduleDir = VoiceCatalog::GetModuleDir();

    std::vector<std::wstring> candidates = {
        installDir + L"\\server.exe",
        installDir + L"\\native\\server.exe",
        moduleDir + L"\\server.exe",
        moduleDir + L"\\native\\server.exe",
        moduleDir + L"\\..\\..\\..\\sample - nvda add-on\\googleTtsRs\\synthDrivers\\googleTtsRs\\native\\server.exe",
        moduleDir + L"\\..\\sample - nvda add-on\\googleTtsRs\\synthDrivers\\googleTtsRs\\native\\server.exe",
        installDir + L"\\..\\..\\..\\sample - nvda add-on\\googleTtsRs\\synthDrivers\\googleTtsRs\\native\\server.exe"
    };

    for (const auto& path : candidates) {
        if (FileExists(path)) {
            return path;
        }
    }
    return L"";
}

std::wstring NativeTtsBridge::FindWasmPath() const {
    wchar_t envBuf[MAX_PATH] = {0};
    if (GetEnvironmentVariableW(L"GTTS_WASM_PATH", envBuf, MAX_PATH) > 0 && FileExists(envBuf)) {
        return std::wstring(envBuf);
    }

    std::wstring installDir = VoiceCatalog::GetInstallDir();
    std::wstring moduleDir = VoiceCatalog::GetModuleDir();

    std::vector<std::wstring> candidates = {
        installDir + L"\\WasmTtsEngine\\20260625.1\\bindings_main.wasm",
        installDir + L"\\bindings_main.wasm",
        moduleDir + L"\\WasmTtsEngine\\20260625.1\\bindings_main.wasm",
        moduleDir + L"\\bindings_main.wasm",
        moduleDir + L"\\..\\..\\..\\sample - nvda add-on\\googleTtsRs\\synthDrivers\\googleTtsRs\\WasmTtsEngine\\20260625.1\\bindings_main.wasm",
        moduleDir + L"\\..\\sample - nvda add-on\\googleTtsRs\\synthDrivers\\googleTtsRs\\WasmTtsEngine\\20260625.1\\bindings_main.wasm",
        installDir + L"\\..\\..\\..\\sample - nvda add-on\\googleTtsRs\\synthDrivers\\googleTtsRs\\WasmTtsEngine\\20260625.1\\bindings_main.wasm"
    };

    for (const auto& path : candidates) {
        if (FileExists(path)) {
            return path;
        }
    }
    return L"";
}

std::wstring NativeTtsBridge::GetVfsRoot() const {
    wchar_t localAppData[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        std::wstring root = std::wstring(localAppData) + L"\\GoogleTTS\\vfs";
        CreateDirectoryW((std::wstring(localAppData) + L"\\GoogleTTS").c_str(), nullptr);
        CreateDirectoryW(root.c_str(), nullptr);
        return root;
    }
    wchar_t tempPath[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"\\google_tts_vfs";
}

bool NativeTtsBridge::EnsureConnected() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (IsConnected()) {
        return true;
    }
    Terminate();
    return StartServer();
}

bool NativeTtsBridge::StartServer() {
    std::wstring serverExe = FindServerExe();
    if (serverExe.empty()) {
        return false;
    }
    std::wstring wasmPath = FindWasmPath();
    if (wasmPath.empty()) {
        return false;
    }
    std::wstring vfsRoot = GetVfsRoot();

    // Set process environment variables
    SetEnvironmentVariableW(L"GTTS_WASM_PATH", wasmPath.c_str());
    SetEnvironmentVariableW(L"GTTS_VFS_ROOT", vfsRoot.c_str());

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hStdOutRead = nullptr;
    HANDLE hStdOutWrite = nullptr;

    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) {
        return false;
    }
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdOutWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = { 0 };

    std::wstring cmdLine = L"\"" + serverExe + L"\"";
    std::vector<wchar_t> cmdLineBuf(cmdLine.begin(), cmdLine.end());
    cmdLineBuf.push_back(L'\0');

    BOOL created = CreateProcessW(
        nullptr,
        cmdLineBuf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    CloseHandle(hStdOutWrite);

    if (!created) {
        CloseHandle(hStdOutRead);
        return false;
    }

    hProcess_ = pi.hProcess;
    hThread_ = pi.hThread;

    // Read the READY line from stdout with a timeout
    std::string line;
    char ch = 0;
    DWORD bytesRead = 0;
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        DWORD avail = 0;
        if (PeekNamedPipe(hStdOutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            if (ReadFile(hStdOutRead, &ch, 1, &bytesRead, nullptr) && bytesRead > 0) {
                if (ch == '\n') {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.rfind("READY", 0) == 0) {
                        break;
                    }
                    line.clear();
                } else {
                    line += ch;
                }
            }
        } else {
            // Check if process terminated
            DWORD exitCode = 0;
            if (GetExitCodeProcess(hProcess_, &exitCode) && exitCode != STILL_ACTIVE) {
                CloseHandle(hStdOutRead);
                KillProcess();
                return false;
            }
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count();
            if (elapsed > 30) { // 30 sec timeout
                CloseHandle(hStdOutRead);
                KillProcess();
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    CloseHandle(hStdOutRead);

    // Parse "READY <mainPort> <ctrlPort>"
    std::stringstream ss(line);
    std::string tagStr;
    int mainPort = 0;
    int ctrlPort = 0;
    ss >> tagStr >> mainPort >> ctrlPort;

    if (tagStr != "READY" || mainPort <= 0 || ctrlPort <= 0) {
        KillProcess();
        return false;
    }

    mainPort_ = mainPort;
    ctrlPort_ = ctrlPort;

    // Connect main socket
    mainSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (mainSock_ == INVALID_SOCKET) {
        KillProcess();
        return false;
    }

    sockaddr_in mainAddr = {};
    mainAddr.sin_family = AF_INET;
    mainAddr.sin_port = htons(static_cast<u_short>(mainPort_));
    inet_pton(AF_INET, "127.0.0.1", &mainAddr.sin_addr);

    if (connect(mainSock_, reinterpret_cast<sockaddr*>(&mainAddr), sizeof(mainAddr)) != 0) {
        CloseSockets();
        KillProcess();
        return false;
    }

    // Set TCP_NODELAY
    int flag = 1;
    setsockopt(mainSock_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));

    // Connect control socket
    ctrlSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctrlSock_ != INVALID_SOCKET) {
        sockaddr_in ctrlAddr = {};
        ctrlAddr.sin_family = AF_INET;
        ctrlAddr.sin_port = htons(static_cast<u_short>(ctrlPort_));
        inet_pton(AF_INET, "127.0.0.1", &ctrlAddr.sin_addr);

        if (connect(ctrlSock_, reinterpret_cast<sockaddr*>(&ctrlAddr), sizeof(ctrlAddr)) == 0) {
            setsockopt(ctrlSock_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
        } else {
            closesocket(ctrlSock_);
            ctrlSock_ = INVALID_SOCKET;
        }
    }

    currentPackageId_.clear();
    return true;
}

void NativeTtsBridge::CloseSockets() {
    if (mainSock_ != INVALID_SOCKET) {
        closesocket(mainSock_);
        mainSock_ = INVALID_SOCKET;
    }
    if (ctrlSock_ != INVALID_SOCKET) {
        closesocket(ctrlSock_);
        ctrlSock_ = INVALID_SOCKET;
    }
}

void NativeTtsBridge::KillProcess() {
    if (hProcess_ != nullptr) {
        TerminateProcess(hProcess_, 0);
        WaitForSingleObject(hProcess_, 1000);
        CloseHandle(hProcess_);
        hProcess_ = nullptr;
    }
    if (hThread_ != nullptr) {
        CloseHandle(hThread_);
        hThread_ = nullptr;
    }
    currentPackageId_.clear();
}

void NativeTtsBridge::Terminate() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    CloseSockets();
    KillProcess();
}

bool NativeTtsBridge::SendFrame(SOCKET sock, uint8_t tag, const std::string& payload) {
    if (sock == INVALID_SOCKET) return false;
    uint32_t len = static_cast<uint32_t>(payload.size());

    std::vector<uint8_t> buffer(5 + len);
    buffer[0] = tag;
    buffer[1] = static_cast<uint8_t>(len & 0xFF);
    buffer[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
    buffer[3] = static_cast<uint8_t>((len >> 16) & 0xFF);
    buffer[4] = static_cast<uint8_t>((len >> 24) & 0xFF);

    if (len > 0) {
        memcpy(&buffer[5], payload.data(), len);
    }

    int totalSent = 0;
    int toSend = static_cast<int>(buffer.size());
    while (totalSent < toSend) {
        int sent = send(sock, reinterpret_cast<const char*>(buffer.data() + totalSent), toSend - totalSent, 0);
        if (sent <= 0) return false;
        totalSent += sent;
    }
    return true;
}

bool NativeTtsBridge::RecvExact(SOCKET sock, uint8_t* buffer, size_t length, DWORD timeoutMs) {
    size_t totalReceived = 0;
    while (totalReceived < length) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);

        timeval tv;
        tv.tv_sec = static_cast<long>(timeoutMs / 1000);
        tv.tv_usec = static_cast<long>((timeoutMs % 1000) * 1000);

        int sel = select(0, &readSet, nullptr, nullptr, &tv);
        if (sel <= 0) {
            return false;
        }

        int recvd = recv(sock, reinterpret_cast<char*>(buffer + totalReceived), static_cast<int>(length - totalReceived), 0);
        if (recvd <= 0) {
            return false;
        }
        totalReceived += recvd;
    }
    return true;
}

bool NativeTtsBridge::RecvFrame(SOCKET sock, uint8_t& tag, std::vector<uint8_t>& payload, DWORD timeoutMs) {
    uint8_t header[5] = {0};
    if (!RecvExact(sock, header, 5, timeoutMs)) {
        return false;
    }
    tag = header[0];
    uint32_t length = static_cast<uint32_t>(header[1]) |
                      (static_cast<uint32_t>(header[2]) << 8) |
                      (static_cast<uint32_t>(header[3]) << 16) |
                      (static_cast<uint32_t>(header[4]) << 24);

    payload.resize(length);
    if (length > 0) {
        if (!RecvExact(sock, payload.data(), length, timeoutMs)) {
            return false;
        }
    }
    return true;
}

bool NativeTtsBridge::EnsureVoiceReady(
    const std::string& packageId,
    const std::string& fileId,
    const std::wstring& zvoicePath,
    const std::string& depPackageId,
    const std::string& depFileId,
    const std::wstring& depZvoicePath
) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!EnsureConnected()) {
        return false;
    }

    if (currentPackageId_ == packageId) {
        return true;
    }

    pcmCache_.clear();

    // If there is a dependency, install it first
    if (!depPackageId.empty() && !depZvoicePath.empty()) {
        std::string depPayload = "{\"voice_dir\":\"/voices/" + depFileId + "\",\"zvoice_path\":\"" +
                                 JsonValue::escape_string(WideToUtf8(depZvoicePath)) + "\"}";
        if (!SendFrame(mainSock_, TAG_INSTALL_VOICE, depPayload)) return false;
        uint8_t tag = 0;
        std::vector<uint8_t> resp;
        if (!RecvFrame(mainSock_, tag, resp) || tag != TAG_ACK) return false;
    }

    // Install this voice package
    std::string pkgPayload = "{\"voice_dir\":\"/voices/" + fileId + "\",\"zvoice_path\":\"" +
                             JsonValue::escape_string(WideToUtf8(zvoicePath)) + "\"}";
    if (!SendFrame(mainSock_, TAG_INSTALL_VOICE, pkgPayload)) return false;
    uint8_t tag = 0;
    std::vector<uint8_t> resp;
    if (!RecvFrame(mainSock_, tag, resp) || tag != TAG_ACK) return false;

    // Initialize voice
    std::string initPayload = "{\"voice_dir\":\"/voices/" + fileId + "\"}";
    if (!SendFrame(mainSock_, TAG_INIT, initPayload)) return false;
    if (!RecvFrame(mainSock_, tag, resp) || tag != TAG_ACK) return false;

    currentPackageId_ = packageId;
    return true;
}

void NativeTtsBridge::CancelCurrent() {
    if (ctrlSock_ == INVALID_SOCKET) return;
    char cancelByte = 0x01;
    send(ctrlSock_, &cancelByte, 1, 0);
}

HRESULT NativeTtsBridge::Speak(
    const std::string& utf8Text,
    float rate,
    float pitch,
    const std::string& speakerCode,
    float gain,
    ISpTTSEngineSite* site,
    ULONGLONG* pAudioBytesWritten,
    bool* pAborted
) {
    if (pAborted) *pAborted = false;
    if (utf8Text.empty()) return S_OK;

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!EnsureConnected()) {
        return E_FAIL;
    }

    std::string cacheKey;
    bool eligibleForCache = (utf8Text.size() <= 60);
    if (eligibleForCache) {
        cacheKey = currentPackageId_ + ":" + speakerCode + ":" +
                   std::to_string(static_cast<int>(rate * 100)) + ":" +
                   std::to_string(static_cast<int>(pitch * 100)) + ":" + utf8Text;
        for (const auto& entry : pcmCache_) {
            if (entry.first == cacheKey) {
                if (site && !entry.second.empty()) {
                    ULONG written = 0;
                    site->Write(entry.second.data(), static_cast<ULONG>(entry.second.size()), &written);
                    if (pAudioBytesWritten) {
                        *pAudioBytesWritten += written;
                    }
                }
                return S_OK;
            }
        }
    }

    std::ostringstream ss;
    ss << "{"
       << "\"text\":\"" << JsonValue::escape_string(utf8Text) << "\","
       << "\"rate\":" << rate << ","
       << "\"pitch\":" << pitch << ","
       << "\"speaker\":\"" << JsonValue::escape_string(speakerCode) << "\","
       << "\"gain\":" << gain
       << "}";

    std::string request = ss.str();
    runtimeBusy_ = true;

    if (!SendFrame(mainSock_, TAG_SPEAK, request)) {
        runtimeBusy_ = false;
        Terminate();
        return E_FAIL;
    }

    bool cancelSent = false;
    uint8_t header[5] = {0};
    std::vector<uint8_t> collectedPcm;

    while (true) {
        // Check for SAPI abort signal
        if (site && !cancelSent && (site->GetActions() & SPVES_ABORT)) {
            CancelCurrent();
            cancelSent = true;
            if (pAborted) *pAborted = true;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(mainSock_, &readSet);

        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000; // 1ms poll interval (down from 10ms for instant reactivity)

        int sel = select(0, &readSet, nullptr, nullptr, &tv);
        if (sel < 0) {
            runtimeBusy_ = false;
            Terminate();
            return E_FAIL;
        }
        if (sel == 0) {
            // Timeout elapsed, loop around and check abort again
            continue;
        }

        // Read 5-byte frame header
        if (!RecvExact(mainSock_, header, 5, 5000)) {
            runtimeBusy_ = false;
            Terminate();
            return E_FAIL;
        }

        uint8_t tag = header[0];
        uint32_t length = static_cast<uint32_t>(header[1]) |
                          (static_cast<uint32_t>(header[2]) << 8) |
                          (static_cast<uint32_t>(header[3]) << 16) |
                          (static_cast<uint32_t>(header[4]) << 24);

        std::vector<uint8_t> payload(length);
        if (length > 0) {
            if (!RecvExact(mainSock_, payload.data(), length, 10000)) {
                runtimeBusy_ = false;
                Terminate();
                return E_FAIL;
            }
        }

        if (tag == TAG_AUDIO_CHUNK) {
            if (cancelSent) {
                continue;
            }
            if (site && length > 0) {
                ULONG written = 0;
                site->Write(payload.data(), length, &written);
                if (pAudioBytesWritten) {
                    *pAudioBytesWritten += written;
                }
            }
            if (eligibleForCache && !cancelSent && length > 0) {
                if (collectedPcm.size() + length <= 256 * 1024) {
                    collectedPcm.insert(collectedPcm.end(), payload.begin(), payload.end());
                } else {
                    eligibleForCache = false;
                }
            }
        } else if (tag == TAG_SPEAK_DONE) {
            if (eligibleForCache && !cancelSent && !collectedPcm.empty()) {
                if (pcmCache_.size() >= kMaxCacheEntries) {
                    pcmCache_.erase(pcmCache_.begin());
                }
                pcmCache_.emplace_back(cacheKey, std::move(collectedPcm));
            }
            runtimeBusy_ = false;
            return S_OK;
        } else if (tag == TAG_SPEAK_CANCELLED) {
            runtimeBusy_ = false;
            if (pAborted) *pAborted = true;
            return S_OK;
        } else if (tag == TAG_ERROR) {
            runtimeBusy_ = false;
            return E_FAIL;
        } else {
            // Unexpected tag
            runtimeBusy_ = false;
            return E_FAIL;
        }
    }
}

} // namespace google_tts
