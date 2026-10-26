# Google TTS SAPI 5 for Windows

Google's Chrome OS text-to-speech voices as a normal SAPI 5 voice on Windows. Works with NVDA, Narrator, JAWS, Balabolka and anything else that speaks SAPI.

Standard and Natural (neural) voices, 150+ voices in 50+ languages. Everything runs locally once a voice pack is downloaded. No account, no telemetry, no internet needed for speech.

Early software. It works well in daily use, but expect rough edges.

## What it is

A SAPI 5 COM driver (C++17) that sends text to a small local engine, `server.exe`, which runs Google's WebAssembly TTS engine and streams audio back. The engine host comes from [google-tts-rs](https://github.com/aryanchoudharypro/google-tts-rs); this project is the SAPI front end and a voice manager on top of it.

- 64-bit and 32-bit SAPI support, so older 32-bit apps see the voices too
- Voice Manager built with plain Win32 controls, fully usable with a keyboard and a screen reader
- Voices stored on disk as `.zvoice` packs, downloaded once
- Rate and pitch work through normal SAPI controls

## Install

1. Download `GoogleTTS-SAPI-1.0.0-Setup.exe` from Releases.
2. Run it as administrator (it registers the SAPI engine).
3. Optionally tick a starter voice pack (US English Standard or Natural). If you skip this, the Voice Manager opens after install so you can pick voices.

## Use

**NVDA:** `NVDA+Ctrl+S`, choose *Windows SAPI version 5*, then `NVDA+Ctrl+V` and pick a Google voice.

**Narrator:** `Win+Ctrl+N`, open the *Narrator's voice* dropdown, pick a Google voice.

**Other apps:** pick the voice in the app, or set it as the default in `sapi.cpl`. 32-bit apps use `C:\Windows\SysWOW64\Speech\SpeechUX\sapi.cpl`.

## Voice Manager

Start Menu → *Google TTS Voice Manager*.

- **Installed Voices:** see what you have, remove packs you don't need.
- **Download Voices:** tick the languages you want and download. The button shows the total size.

| Key | Action |
| :--- | :--- |
| `Ctrl+Tab` | Switch tabs |
| `Alt+A` | Select / deselect all |
| `Alt+D` | Download selected |
| `Alt+R` | Refresh |
| `Alt+O` | Open voices folder |
| `Alt+C` / `Esc` | Close |

## Voice tiers

| Tier | Package | Notes |
| :--- | :--- | :--- |
| Standard | `*-x-multi` | Small and fast. Good for everyday screen reading. |
| Natural | `*-x-multi-seanet` | Neural, smoother. Better for books and long text. Needs the matching Standard pack, which the Voice Manager installs for you. |

## Building

You need Windows 10/11, Visual Studio 2022 (MSVC v143, C++17), CMake 3.20+ and Inno Setup 6.2+. `server.exe` and `bindings_main.wasm` come from google-tts-rs.

```powershell
git clone https://github.com/amanKG777/google-tts-sapi.git
cd google-tts-sapi

# 64-bit
cmake -B build/x64 -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build/x64 --config Release

# 32-bit
cmake -B build/x86 -A Win32 -DCMAKE_BUILD_TYPE=Release
cmake --build build/x86 --config Release

# installer
powershell -ExecutionPolicy Bypass -File installer/stage.ps1
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" "installer\google_tts.iss"
```

The installer ends up in `dist\`.

## FAQ

**No sound after installing?** You probably have no voice pack yet. Open the Voice Manager, go to Download, pick a language.

**Voice missing in one app?** Check whether the app is 32-bit. If so, re-register the 32-bit driver:

```cmd
regsvr32 "%ProgramFiles%\Google TTS SAPI\google_tts_sapi_32.dll"
"%ProgramFiles%\Google TTS SAPI\google_tts_config.exe" --register
```

## Credits

- **Aryan Choudhary** — Author of [google-tts-rs](https://github.com/aryanchoudharypro/google-tts-rs), providing the native Rust WebAssembly backend architecture and voice manifest.
- **Google ChromeOS Team** — Authors and creators of the Chrome OS WebAssembly TTS engine models.

## License

MIT, see [LICENSE](LICENSE). Voices and WebAssembly binaries are proprietary assets of Google LLC, provided under Chrome OS terms of service.
