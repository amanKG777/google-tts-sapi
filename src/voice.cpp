#include "voice.h"
#include "bridge.h"
#include "catalog.h"
#include "protocol.h"

#include <vector>
#include <algorithm>
#include <sstream>

extern void GoogleTtsObjectReleased();

namespace google_tts {

namespace {

std::string WideToUtf8(const wchar_t* wstr, size_t len) {
    if (!wstr || len == 0) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(len), &out[0], size, nullptr, nullptr);
    return out;
}

std::string WideToUtf8(const std::wstring& str) {
    return WideToUtf8(str.c_str(), str.size());
}

std::wstring GetTokenString(ISpObjectToken* token, const wchar_t* valueName) {
    if (!token) return L"";
    LPWSTR val = nullptr;
    if (SUCCEEDED(token->GetStringValue(valueName, &val)) && val) {
        std::wstring res(val);
        CoTaskMemFree(val);
        return res;
    }
    return L"";
}

template <typename T>
T Clamp(T val, T low, T high) {
    return (val < low) ? low : (val > high ? high : val);
}

// Split long text into clause/sentence units for fast latency
std::vector<std::string> SplitTextForLatency(const std::string& text) {
    std::vector<std::string> segments;
    if (text.size() <= 80) {
        segments.push_back(text);
        return segments;
    }

    size_t start = 0;
    while (start < text.size()) {
        while (start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n')) {
            start++;
        }
        if (start >= text.size()) break;

        size_t remaining = text.size() - start;
        if (remaining <= 80) {
            segments.push_back(text.substr(start));
            break;
        }

        // Look for sentence/clause punctuation between [start + 20, start + 120]
        size_t searchEnd = (start + 120 < text.size()) ? (start + 120) : text.size();
        size_t bestCut = std::string::npos;

        // Try sentence punctuation first: . ? !
        for (size_t i = start + 30; i < searchEnd; i++) {
            char c = text[i];
            if ((c == '.' || c == '?' || c == '!' || c == ':') && (i + 1 == text.size() || text[i + 1] == ' ' || text[i + 1] == '\n')) {
                bestCut = i + 1;
                break;
            }
        }

        // Fall back to comma or semicolon
        if (bestCut == std::string::npos) {
            for (size_t i = start + 30; i < searchEnd; i++) {
                char c = text[i];
                if ((c == ',' || c == ';') && (i + 1 == text.size() || text[i + 1] == ' ')) {
                    bestCut = i + 1;
                    break;
                }
            }
        }

        // Fall back to space
        if (bestCut == std::string::npos) {
            for (size_t i = searchEnd; i > start + 30; i--) {
                if (text[i] == ' ') {
                    bestCut = i;
                    break;
                }
            }
        }

        if (bestCut != std::string::npos && bestCut > start) {
            segments.push_back(text.substr(start, bestCut - start));
            start = bestCut;
        } else {
            segments.push_back(text.substr(start, searchEnd - start));
            start = searchEnd;
        }
    }

    return segments;
}

} // namespace

Voice::Voice() = default;

Voice::~Voice() {
    if (token_) {
        token_->Release();
        token_ = nullptr;
    }
}

STDMETHODIMP Voice::QueryInterface(REFIID riid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (riid == IID_IUnknown || riid == __uuidof(ISpTTSEngine)) {
        *object = static_cast<ISpTTSEngine*>(this);
    } else if (riid == __uuidof(ISpObjectWithToken)) {
        *object = static_cast<ISpObjectWithToken*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) Voice::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&references_));
}

STDMETHODIMP_(ULONG) Voice::Release() {
    LONG count = InterlockedDecrement(&references_);
    if (count == 0) {
        GoogleTtsObjectReleased();
        delete this;
    }
    return static_cast<ULONG>(count);
}

STDMETHODIMP Voice::SetObjectToken(ISpObjectToken* token) {
    if (!token) return E_INVALIDARG;
    if (token_) token_->Release();
    token_ = token;
    token_->AddRef();

    voiceId_ = WideToUtf8(GetTokenString(token_, L"VoiceId"));
    fileId_ = WideToUtf8(GetTokenString(token_, L"FileId"));
    speaker_ = WideToUtf8(GetTokenString(token_, L"Speaker"));
    dependentVoiceId_ = WideToUtf8(GetTokenString(token_, L"DependentVoiceId"));
    dependentFileId_ = WideToUtf8(GetTokenString(token_, L"DependentFileId"));
    zvoicePath_ = GetTokenString(token_, L"ZVoicePath");

    // Defaults if token did not specify
    if (voiceId_.empty()) voiceId_ = "en-us-x-multi";
    if (fileId_.empty()) fileId_ = "en-us-x-multi-r83";
    if (speaker_.empty()) speaker_ = "hol";

    // Start server and preload voice immediately upon engine initialization
    VoiceCatalog& catalog = VoiceCatalog::Instance();
    catalog.Load();

    std::wstring pkgPath = zvoicePath_;
    std::wstring depPath;
    const VoicePackage* pkg = catalog.FindPackageById(voiceId_);
    if (pkg) {
        catalog.IsPackageUsable(*pkg, &pkgPath, &depPath);
    }
    if (!pkgPath.empty()) {
        std::string depFileId = dependentFileId_;
        if (depFileId.empty() && pkg && !pkg->dependentVoiceId.empty()) {
            const VoicePackage* depPkg = catalog.FindPackageById(pkg->dependentVoiceId);
            if (depPkg) depFileId = depPkg->fileId;
        }
        NativeTtsBridge::Instance().EnsureVoiceReady(
            voiceId_,
            fileId_,
            pkgPath,
            dependentVoiceId_,
            depFileId,
            depPath
        );
    }

    return S_OK;
}

STDMETHODIMP Voice::GetObjectToken(ISpObjectToken** token) {
    if (!token) return E_POINTER;
    *token = token_;
    if (token_) token_->AddRef();
    return token_ ? S_OK : S_FALSE;
}

STDMETHODIMP Voice::GetOutputFormat(
    const GUID* targetFormatId,
    const WAVEFORMATEX* targetWaveFormat,
    GUID* outputFormatId,
    WAVEFORMATEX** outputWaveFormat
) {
    if (!outputFormatId || !outputWaveFormat) return E_POINTER;

    WAVEFORMATEX* pFmt = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!pFmt) return E_OUTOFMEMORY;

    pFmt->wFormatTag = WAVE_FORMAT_PCM;
    pFmt->nChannels = CHANNELS;
    pFmt->nSamplesPerSec = SAMPLE_RATE;
    pFmt->wBitsPerSample = BITS_PER_SAMPLE;
    pFmt->nBlockAlign = (CHANNELS * BITS_PER_SAMPLE) / 8;
    pFmt->nAvgBytesPerSec = pFmt->nSamplesPerSec * pFmt->nBlockAlign;
    pFmt->cbSize = 0;

    *outputFormatId = SPDFID_WaveFormatEx;
    *outputWaveFormat = pFmt;
    return S_OK;
}

float Voice::SapiRateToEngine(long rate) const {
    long clamped = Clamp(rate, -10L, 10L);
    if (clamped == 0) return 1.0f;
    if (clamped > 0) {
        return 1.0f + (static_cast<float>(clamped) / 10.0f) * 2.0f; // 1.0 to 3.0
    }
    return 1.0f + (static_cast<float>(clamped) / 10.0f) * 0.65f;   // 1.0 to 0.35
}

float Voice::SapiPitchToEngine(long pitch) const {
    long clamped = Clamp(pitch, -10L, 10L);
    if (clamped == 0) return 1.0f;
    if (clamped > 0) {
        return 1.0f + (static_cast<float>(clamped) / 10.0f) * 0.4f; // 1.0 to 1.4
    }
    return 1.0f + (static_cast<float>(clamped) / 10.0f) * 0.35f;  // 1.0 to 0.65
}

STDMETHODIMP Voice::Speak(
    DWORD flags,
    REFGUID formatId,
    const WAVEFORMATEX* waveFormat,
    const SPVTEXTFRAG* fragments,
    ISpTTSEngineSite* site
) {
    if (!site) return E_INVALIDARG;

    VoiceCatalog& catalog = VoiceCatalog::Instance();
    catalog.Load();

    std::wstring pkgPath = zvoicePath_;
    std::wstring depPath;

    const VoicePackage* pkg = catalog.FindPackageById(voiceId_);
    if (pkg) {
        catalog.IsPackageUsable(*pkg, &pkgPath, &depPath);
    }

    if (pkgPath.empty()) {
        // Voice package file not found
        return E_FAIL;
    }

    // Ensure server is running and voice is loaded
    std::string depFileId = dependentFileId_;
    if (depFileId.empty() && pkg && !pkg->dependentVoiceId.empty()) {
        const VoicePackage* depPkg = catalog.FindPackageById(pkg->dependentVoiceId);
        if (depPkg) depFileId = depPkg->fileId;
    }

    if (!NativeTtsBridge::Instance().EnsureVoiceReady(
            voiceId_,
            fileId_,
            pkgPath,
            dependentVoiceId_,
            depFileId,
            depPath)) {
        return E_FAIL;
    }

    long siteRate = 0;
    site->GetRate(&siteRate);

    USHORT siteVolume = 100;
    site->GetVolume(&siteVolume);

    ULONGLONG audioBytesWritten = 0;
    bool aborted = false;

    for (const SPVTEXTFRAG* frag = fragments; frag; frag = frag->pNext) {
        if (site->GetActions() & SPVES_ABORT) {
            break;
        }

        switch (frag->State.eAction) {
            case SPVA_Speak:
            case SPVA_SpellOut: {
                if (!frag->pTextStart || frag->ulTextLen == 0) break;

                long combinedRate = siteRate + frag->State.RateAdj;
                long combinedPitch = frag->State.PitchAdj.MiddleAdj;
                int combinedVolume = (static_cast<int>(siteVolume) * static_cast<int>(frag->State.Volume)) / 100;

                float rate = SapiRateToEngine(combinedRate);
                float pitch = SapiPitchToEngine(combinedPitch);
                float gain = Clamp(static_cast<float>(combinedVolume) / 100.0f, 0.0f, 1.0f);

                std::string utf8Text = WideToUtf8(frag->pTextStart, frag->ulTextLen);
                if (utf8Text.empty()) break;

                // Split long text into smaller segments for quick latency
                std::vector<std::string> segments = SplitTextForLatency(utf8Text);
                for (const auto& segment : segments) {
                    if (site->GetActions() & SPVES_ABORT) {
                        aborted = true;
                        break;
                    }
                    HRESULT hr = NativeTtsBridge::Instance().Speak(
                        segment,
                        rate,
                        pitch,
                        speaker_,
                        gain,
                        site,
                        &audioBytesWritten,
                        &aborted
                    );
                    if (FAILED(hr) || aborted) {
                        break;
                    }
                }
                break;
            }

            case SPVA_Silence: {
                // Generate silence samples
                ULONG silenceMs = frag->State.SilenceMSecs;
                if (silenceMs > 0) {
                    ULONG totalBytes = (silenceMs * SAMPLE_RATE * (BITS_PER_SAMPLE / 8)) / 1000;
                    std::vector<char> silenceBuf(4096, 0);
                    while (totalBytes > 0) {
                        if (site->GetActions() & SPVES_ABORT) {
                            aborted = true;
                            break;
                        }
                        ULONG toWrite = (std::min)(totalBytes, static_cast<ULONG>(silenceBuf.size()));
                        ULONG written = 0;
                        site->Write(silenceBuf.data(), toWrite, &written);
                        audioBytesWritten += written;
                        totalBytes -= written;
                    }
                }
                break;
            }

            case SPVA_Bookmark: {
                if (frag->pTextStart && frag->ulTextLen > 0) {
                    std::wstring bookmarkText(frag->pTextStart, frag->ulTextLen);
                    SPEVENT event = {};
                    event.eEventId = SPEI_TTS_BOOKMARK;
                    event.elParamType = SPET_LPARAM_IS_STRING;
                    event.ullAudioStreamOffset = audioBytesWritten;
                    event.lParam = reinterpret_cast<LPARAM>(bookmarkText.c_str());
                    event.wParam = static_cast<WPARAM>(_wtol(bookmarkText.c_str()));
                    site->AddEvents(&event, 1);
                }
                break;
            }

            default:
                break;
        }

        if (aborted) break;
    }

    return S_OK;
}

} // namespace google_tts
