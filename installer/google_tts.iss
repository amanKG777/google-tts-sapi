; Google TTS SAPI 5 Windows Installer
; Built with Inno Setup 6

#define AppName "Google TTS SAPI"
#define AppVersion "1.0.0"
#define AppPublisher "Google TTS Contributors"
#define AppURL "https://github.com"

[Setup]
AppId={{99D20C28-65ED-4A06-9A4B-23E47EA38F9F}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
DefaultDirName={autopf}\Google TTS SAPI
DefaultGroupName={#AppName}
OutputDir=..\dist
OutputBaseFilename=GoogleTTS-SAPI-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; Support 64-bit and 32-bit SAPI applications
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Registering COM classes and SAPI tokens writes to HKLM
PrivilegesRequired=admin
DisableWelcomePage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "voice_en_us_multi"; Description: "Install US English Chrome OS voice pack (Standard, 17 voices)"; GroupDescription: "Starter Voice Packages to Install:"
Name: "voice_en_us_seanet"; Description: "Install US English Natural voice pack (Neural, 17 voices)"; GroupDescription: "Starter Voice Packages to Install:"

[Files]
; 64-bit SAPI COM engine
Source: "..\dist\staging\google_tts_sapi_x64.dll"; DestDir: "{app}"; DestName: "google_tts_sapi.dll"; \
    Flags: ignoreversion regserver 64bit

; 32-bit SAPI COM engine
Source: "..\dist\staging\google_tts_sapi_x86.dll"; DestDir: "{app}"; DestName: "google_tts_sapi_32.dll"; \
    Flags: ignoreversion regserver 32bit

; Voice Manager GUI & CLI
Source: "..\dist\staging\google_tts_config.exe"; DestDir: "{app}"; Flags: ignoreversion

; Native Rust server
Source: "..\dist\staging\server.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\staging\native\server.exe"; DestDir: "{app}\native"; Flags: ignoreversion

; Wasm TTS Engine & Voice Catalog
Source: "..\dist\staging\WasmTtsEngine\*"; DestDir: "{app}\WasmTtsEngine"; \
    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\dist\staging\bindings_main.wasm"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\staging\voices.json"; DestDir: "{app}"; Flags: ignoreversion

; Optional starter voice packages
Source: "..\dist\staging\voices\en-us-x-multi.zvoice"; DestDir: "{app}\voices"; \
    Flags: ignoreversion skipifsourcedoesntexist; Tasks: voice_en_us_multi voice_en_us_seanet
Source: "..\dist\staging\voices\en-us-x-multi-seanet.zvoice"; DestDir: "{app}\voices"; \
    Flags: ignoreversion skipifsourcedoesntexist; Tasks: voice_en_us_seanet

[Registry]
Root: HKLM; Subkey: "Software\GoogleTTS"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\WOW6432Node\GoogleTTS"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey

; 64-bit COM CLSID
Root: HKLM64; Subkey: "Software\Classes\CLSID\{{99D20C28-65ED-4A06-9A4B-23E47EA38F9F}"; ValueType: string; ValueData: "Google TTS SAPI Engine"; Flags: uninsdeletekey; Check: Is64BitInstallMode
Root: HKLM64; Subkey: "Software\Classes\CLSID\{{99D20C28-65ED-4A06-9A4B-23E47EA38F9F}\InprocServer32"; ValueType: string; ValueData: "{app}\google_tts_sapi.dll"; Flags: uninsdeletekey; Check: Is64BitInstallMode
Root: HKLM64; Subkey: "Software\Classes\CLSID\{{99D20C28-65ED-4A06-9A4B-23E47EA38F9F}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Both"; Flags: uninsdeletekey; Check: Is64BitInstallMode

Root: HKLM64; Subkey: "Software\Classes\CLSID\{{53AFEF50-7107-43E3-A958-C19DF7666D70}"; ValueType: string; ValueData: "Google TTS Settings"; Flags: uninsdeletekey; Check: Is64BitInstallMode
Root: HKLM64; Subkey: "Software\Classes\CLSID\{{53AFEF50-7107-43E3-A958-C19DF7666D70}\InprocServer32"; ValueType: string; ValueData: "{app}\google_tts_sapi.dll"; Flags: uninsdeletekey; Check: Is64BitInstallMode
Root: HKLM64; Subkey: "Software\Classes\CLSID\{{53AFEF50-7107-43E3-A958-C19DF7666D70}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Both"; Flags: uninsdeletekey; Check: Is64BitInstallMode

; 32-bit COM CLSID (WOW64)
Root: HKLM32; Subkey: "Software\Classes\CLSID\{{99D20C28-65ED-4A06-9A4B-23E47EA38F9F}"; ValueType: string; ValueData: "Google TTS SAPI Engine"; Flags: uninsdeletekey
Root: HKLM32; Subkey: "Software\Classes\CLSID\{{99D20C28-65ED-4A06-9A4B-23E47EA38F9F}\InprocServer32"; ValueType: string; ValueData: "{app}\google_tts_sapi_32.dll"; Flags: uninsdeletekey
Root: HKLM32; Subkey: "Software\Classes\CLSID\{{99D20C28-65ED-4A06-9A4B-23E47EA38F9F}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Both"; Flags: uninsdeletekey

Root: HKLM32; Subkey: "Software\Classes\CLSID\{{53AFEF50-7107-43E3-A958-C19DF7666D70}"; ValueType: string; ValueData: "Google TTS Settings"; Flags: uninsdeletekey
Root: HKLM32; Subkey: "Software\Classes\CLSID\{{53AFEF50-7107-43E3-A958-C19DF7666D70}\InprocServer32"; ValueType: string; ValueData: "{app}\google_tts_sapi_32.dll"; Flags: uninsdeletekey
Root: HKLM32; Subkey: "Software\Classes\CLSID\{{53AFEF50-7107-43E3-A958-C19DF7666D70}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Both"; Flags: uninsdeletekey

[Icons]
Name: "{group}\Google TTS Voice Manager"; Filename: "{app}\google_tts_config.exe"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Google TTS Voice Manager"; Filename: "{app}\google_tts_config.exe"; Tasks: desktopicon

[Run]
; Auto-register voices upon installation
Filename: "{app}\google_tts_config.exe"; Parameters: "--register"; Flags: runhidden
; Optional launch of Voice Manager GUI
Filename: "{app}\google_tts_config.exe"; Description: "Launch Google TTS Voice Manager"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "regsvr32.exe"; Parameters: "/u /s ""{app}\google_tts_sapi.dll"""; Flags: runhidden
Filename: "regsvr32.exe"; Parameters: "/u /s ""{app}\google_tts_sapi_32.dll"""; Flags: runhidden

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\GoogleTTS\vfs"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if DirExists(ExpandConstant('{commonappdata}\GoogleTTS\voices')) then
    begin
      if MsgBox('Would you also like to remove downloaded Google TTS voices?' + #13#10 +
                'Choose No to preserve them for future installations.',
                mbConfirmation, MB_YESNO) = IDYES then
      begin
        DelTree(ExpandConstant('{commonappdata}\GoogleTTS'), True, True, True);
      end;
    end;
  end;
end;
