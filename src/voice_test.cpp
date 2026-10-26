#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>

#include <iostream>
#include <vector>
#include <string>
#include <fstream>

#include "guids.h"
#include "protocol.h"

namespace {

class TestSite : public ISpTTSEngineSite {
public:
    explicit TestSite(long rate = 0, USHORT volume = 100)
        : rate_(rate), volume_(volume) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (riid == IID_IUnknown || riid == __uuidof(ISpTTSEngineSite)) {
            *object = static_cast<ISpTTSEngineSite*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    // ISpEventSink
    STDMETHODIMP AddEvents(const SPEVENT* events, ULONG count) override {
        for (ULONG i = 0; i < count; ++i) {
            if (events[i].eEventId == SPEI_TTS_BOOKMARK) {
                bookmarks_.push_back(reinterpret_cast<const wchar_t*>(events[i].lParam));
                bookmarkOffsets_.push_back(events[i].ullAudioStreamOffset);
            }
        }
        return S_OK;
    }
    STDMETHODIMP GetEventInterest(ULONGLONG* interest) override {
        *interest = SPFEI_ALL_TTS_EVENTS;
        return S_OK;
    }

    // ISpTTSEngineSite
    STDMETHODIMP_(DWORD) GetActions() override { return abort_ ? SPVES_ABORT : 0; }
    STDMETHODIMP Write(const void* data, ULONG bytes, ULONG* written) override {
        const short* samples = static_cast<const short*>(data);
        audio_.insert(audio_.end(), samples, samples + bytes / sizeof(short));
        if (written) *written = bytes;
        return S_OK;
    }
    STDMETHODIMP GetRate(long* rate) override {
        *rate = rate_;
        return S_OK;
    }
    STDMETHODIMP GetVolume(USHORT* volume) override {
        *volume = volume_;
        return S_OK;
    }
    STDMETHODIMP GetSkipInfo(SPVSKIPTYPE* type, long* items) override {
        *type = SPVST_SENTENCE;
        *items = 0;
        return S_OK;
    }
    STDMETHODIMP CompleteSkip(long skipped) override { return S_OK; }

    const std::vector<short>& audio() const { return audio_; }
    const std::vector<std::wstring>& bookmarks() const { return bookmarks_; }
    void Abort() { abort_ = true; }

private:
    long rate_ = 0;
    USHORT volume_ = 100;
    bool abort_ = false;
    std::vector<short> audio_;
    std::vector<std::wstring> bookmarks_;
    std::vector<ULONGLONG> bookmarkOffsets_;
};

// Mock Token for testing without registry
class MockObjectToken : public ISpObjectToken {
public:
    std::wstring voiceId;
    std::wstring fileId;
    std::wstring speaker;
    std::wstring depVoiceId;
    std::wstring depFileId;
    std::wstring zvoicePath;

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (riid == IID_IUnknown || riid == __uuidof(ISpObjectToken)) {
            *object = static_cast<ISpObjectToken*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP GetStringValue(LPCWSTR valName, LPWSTR* pVal) override {
        if (!pVal) return E_POINTER;
        std::wstring str;
        if (wcscmp(valName, L"VoiceId") == 0) str = voiceId;
        else if (wcscmp(valName, L"FileId") == 0) str = fileId;
        else if (wcscmp(valName, L"Speaker") == 0) str = speaker;
        else if (wcscmp(valName, L"DependentVoiceId") == 0) str = depVoiceId;
        else if (wcscmp(valName, L"DependentFileId") == 0) str = depFileId;
        else if (wcscmp(valName, L"ZVoicePath") == 0) str = zvoicePath;
        else return SPERR_NOT_FOUND;

        size_t bytes = (str.size() + 1) * sizeof(wchar_t);
        *pVal = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
        if (!*pVal) return E_OUTOFMEMORY;
        memcpy(*pVal, str.c_str(), bytes);
        return S_OK;
    }

    // Stubs for remaining unused ISpObjectToken methods
    STDMETHODIMP SetId(LPCWSTR, LPCWSTR, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP GetId(LPWSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP GetCategory(ISpObjectTokenCategory**) override { return E_NOTIMPL; }
    STDMETHODIMP CreateInstance(IUnknown*, DWORD, REFIID, void**) override { return E_NOTIMPL; }
    STDMETHODIMP GetStorageFileName(REFCLSID, LPCWSTR, LPCWSTR, ULONG, LPWSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP RemoveStorageFileName(REFCLSID, LPCWSTR, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP Remove(const GUID*) override { return E_NOTIMPL; }
    STDMETHODIMP IsUISupported(LPCWSTR, void*, ULONG, IUnknown*, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP DisplayUI(HWND, LPCWSTR, LPCWSTR, void*, ULONG, IUnknown*) override { return E_NOTIMPL; }
    STDMETHODIMP MatchesAttributes(LPCWSTR, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP SetData(LPCWSTR, ULONG, const BYTE*) override { return E_NOTIMPL; }
    STDMETHODIMP GetData(LPCWSTR, ULONG*, BYTE*) override { return E_NOTIMPL; }
    STDMETHODIMP SetStringValue(LPCWSTR, LPCWSTR) override { return E_NOTIMPL; }
    STDMETHODIMP SetDWORD(LPCWSTR, DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP GetDWORD(LPCWSTR, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP OpenKey(LPCWSTR, ISpDataKey**) override { return E_NOTIMPL; }
    STDMETHODIMP CreateKey(LPCWSTR, ISpDataKey**) override { return E_NOTIMPL; }
    STDMETHODIMP DeleteKey(LPCWSTR) override { return E_NOTIMPL; }
    STDMETHODIMP DeleteValue(LPCWSTR) override { return E_NOTIMPL; }
    STDMETHODIMP EnumKeys(ULONG, LPWSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP EnumValues(ULONG, LPWSTR*) override { return E_NOTIMPL; }
};

void WriteWav(const std::string& path, const std::vector<short>& samples, int sampleRate) {
    std::ofstream out(path, std::ios::binary);
    int numSamples = static_cast<int>(samples.size());
    int dataBytes = numSamples * sizeof(short);
    int totalBytes = 36 + dataBytes;

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&totalBytes), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    int subchunk1Size = 16;
    short audioFormat = 1; // PCM
    short numChannels = 1; // mono
    int byteRate = sampleRate * numChannels * sizeof(short);
    short blockAlign = numChannels * sizeof(short);
    short bitsPerSample = 16;

    out.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
    out.write(reinterpret_cast<const char*>(&audioFormat), 2);
    out.write(reinterpret_cast<const char*>(&numChannels), 2);
    out.write(reinterpret_cast<const char*>(&sampleRate), 4);
    out.write(reinterpret_cast<const char*>(&byteRate), 4);
    out.write(reinterpret_cast<const char*>(&blockAlign), 2);
    out.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataBytes), 4);
    out.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
}

} // namespace

int main(int argc, char* argv[]) {
    std::wstring dllPath = L"google_tts_sapi.dll";
    if (argc > 1) {
        int len = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
        dllPath.resize(len - 1);
        MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &dllPath[0], len);
    }

    std::wcout << L"Loading " << dllPath << L"..." << std::endl;
    HMODULE hDll = LoadLibraryW(dllPath.c_str());
    if (!hDll) {
        std::cerr << "Failed to load DLL. Error: " << GetLastError() << std::endl;
        return 1;
    }

    typedef HRESULT (STDAPICALLTYPE *DllGetClassObjectFunc)(REFCLSID, REFIID, void**);
    auto pfnDllGetClassObject = reinterpret_cast<DllGetClassObjectFunc>(GetProcAddress(hDll, "DllGetClassObject"));
    if (!pfnDllGetClassObject) {
        std::cerr << "Failed to find DllGetClassObject" << std::endl;
        return 1;
    }

    IClassFactory* pFactory = nullptr;
    HRESULT hr = pfnDllGetClassObject(CLSID_GoogleTtsEngine, IID_IClassFactory, reinterpret_cast<void**>(&pFactory));
    if (FAILED(hr) || !pFactory) {
        std::cerr << "Failed to get IClassFactory. hr=" << std::hex << hr << std::endl;
        return 1;
    }

    ISpTTSEngine* pEngine = nullptr;
    hr = pFactory->CreateInstance(nullptr, __uuidof(ISpTTSEngine), reinterpret_cast<void**>(&pEngine));
    pFactory->Release();
    if (FAILED(hr) || !pEngine) {
        std::cerr << "Failed to create ISpTTSEngine instance. hr=" << std::hex << hr << std::endl;
        return 1;
    }

    ISpObjectWithToken* pWithToken = nullptr;
    hr = pEngine->QueryInterface(__uuidof(ISpObjectWithToken), reinterpret_cast<void**>(&pWithToken));
    std::wstring voiceId = L"en-us-x-multi";
    std::wstring speaker = L"hol";
    if (argc > 3) {
        int len = MultiByteToWideChar(CP_UTF8, 0, argv[3], -1, nullptr, 0);
        voiceId.resize(len - 1);
        MultiByteToWideChar(CP_UTF8, 0, argv[3], -1, &voiceId[0], len);
    }
    if (argc > 4) {
        int len = MultiByteToWideChar(CP_UTF8, 0, argv[4], -1, nullptr, 0);
        speaker.resize(len - 1);
        MultiByteToWideChar(CP_UTF8, 0, argv[4], -1, &speaker[0], len);
    }

    if (SUCCEEDED(hr) && pWithToken) {
        MockObjectToken mockToken;
        mockToken.voiceId = voiceId;
        mockToken.fileId = voiceId + L"-r83";
        mockToken.speaker = speaker;
        if (voiceId.find(L"-seanet") != std::wstring::npos) {
            std::wstring base = voiceId.substr(0, voiceId.find(L"-seanet"));
            mockToken.depVoiceId = base;
            mockToken.depFileId = base + L"-r83";
        }
        pWithToken->SetObjectToken(&mockToken);
        pWithToken->Release();
    }

    TestSite site;
    std::wstring textToSpeak = L"Hello! This is Google Text to Speech running natively on Windows SAPI 5.";

    SPVTEXTFRAG frag = {};
    frag.pNext = nullptr;
    frag.State.eAction = SPVA_Speak;
    frag.State.Volume = 100;
    frag.pTextStart = textToSpeak.c_str();
    frag.ulTextLen = static_cast<ULONG>(textToSpeak.size());

    std::wcout << L"Synthesizing: " << textToSpeak << std::endl;
    hr = pEngine->Speak(0, GUID_NULL, nullptr, &frag, &site);
    if (FAILED(hr)) {
        std::cerr << "Speak failed. hr=" << std::hex << hr << std::endl;
        pEngine->Release();
        return 1;
    }

    std::cout << "Success! Audio sample count: " << site.audio().size()
              << " (" << (site.audio().size() / 24000.0) << " seconds)" << std::endl;

    std::string outWav = "test_output.wav";
    if (argc > 2) {
        outWav = argv[2];
    }
    WriteWav(outWav, site.audio(), 24000);
    std::cout << "Saved audio to " << outWav << std::endl;

    pEngine->Release();
    FreeLibrary(hDll);
    return 0;
}
