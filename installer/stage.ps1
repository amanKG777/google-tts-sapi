# Staging script for Google TTS SAPI installer
$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot | Split-Path -Parent
$staging = Join-Path $root "dist\staging"

Write-Host "Creating staging directory: $staging"
if (Test-Path $staging) {
    Remove-Item -Recurse -Force $staging
}
New-Item -ItemType Directory -Force -Path $staging | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $staging "native") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $staging "WasmTtsEngine\20260625.1") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $staging "voices") | Out-Null

# 1. DLLs
Copy-Item (Join-Path $root "build\x64\Release\google_tts_sapi.dll") (Join-Path $staging "google_tts_sapi_x64.dll") -Force
Copy-Item (Join-Path $root "build\x86\Release\google_tts_sapi.dll") (Join-Path $staging "google_tts_sapi_x86.dll") -Force

# 2. Config GUI / CLI
Copy-Item (Join-Path $root "build\x64\Release\google_tts_config.exe") (Join-Path $staging "google_tts_config.exe") -Force

# 3. Native server
$serverCandidates = @(
    (Join-Path $root "sample - nvda add-on\googleTtsRs\synthDrivers\googleTtsRs\native\server.exe"),
    (Join-Path $env:APPDATA "nvda\addons\googleTtsRs\synthDrivers\googleTtsRs\native\server.exe"),
    (Join-Path $root "dist\staging\native\server.exe"),
    (Join-Path $root "dist\staging\server.exe")
)
$serverExe = $serverCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $serverExe) {
    throw "server.exe not found in any expected location."
}
Copy-Item $serverExe (Join-Path $staging "server.exe") -Force
Copy-Item $serverExe (Join-Path $staging "native\server.exe") -Force

# 4. Wasm engine & catalog
$wasmCandidates = @(
    (Join-Path $root "sample - nvda add-on\googleTtsRs\synthDrivers\googleTtsRs\WasmTtsEngine\20260625.1"),
    (Join-Path $env:APPDATA "nvda\addons\googleTtsRs\synthDrivers\googleTtsRs\WasmTtsEngine\20260625.1"),
    (Join-Path $root "dist\staging\WasmTtsEngine\20260625.1")
)
$wasmDir = $wasmCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $wasmDir) {
    throw "WasmTtsEngine directory not found in any expected location."
}
Copy-Item (Join-Path $wasmDir "bindings_main.wasm") (Join-Path $staging "WasmTtsEngine\20260625.1\bindings_main.wasm") -Force
Copy-Item (Join-Path $wasmDir "voices.json") (Join-Path $staging "WasmTtsEngine\20260625.1\voices.json") -Force
Copy-Item (Join-Path $wasmDir "bindings_main.wasm") (Join-Path $staging "bindings_main.wasm") -Force
Copy-Item (Join-Path $wasmDir "voices.json") (Join-Path $staging "voices.json") -Force

# 5. Voice packages are now downloaded on-demand from GitHub/CDN during setup
# to keep the installer lightweight (~12 MB instead of ~34 MB).

Write-Host "Staging completed successfully in $staging"
