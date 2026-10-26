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
$serverExe = Join-Path $root "sample - nvda add-on\googleTtsRs\synthDrivers\googleTtsRs\native\server.exe"
Copy-Item $serverExe (Join-Path $staging "server.exe") -Force
Copy-Item $serverExe (Join-Path $staging "native\server.exe") -Force

# 4. Wasm engine & catalog
$wasmDir = Join-Path $root "sample - nvda add-on\googleTtsRs\synthDrivers\googleTtsRs\WasmTtsEngine\20260625.1"
Copy-Item (Join-Path $wasmDir "bindings_main.wasm") (Join-Path $staging "WasmTtsEngine\20260625.1\bindings_main.wasm") -Force
Copy-Item (Join-Path $wasmDir "voices.json") (Join-Path $staging "WasmTtsEngine\20260625.1\voices.json") -Force
# Also put voices.json and wasm at root of staging for convenience
Copy-Item (Join-Path $wasmDir "bindings_main.wasm") (Join-Path $staging "bindings_main.wasm") -Force
Copy-Item (Join-Path $wasmDir "voices.json") (Join-Path $staging "voices.json") -Force

# 5. Bundle default US English voices so it works out of the box
$v1 = "C:\Users\nab5\AppData\Roaming\nvda\googleTtsForNvda\voices\en-us-x-multi.zvoice"
$v2 = "C:\Users\nab5\AppData\Roaming\nvda\googleTtsForNvda\voices\en-us-x-multi-seanet.zvoice"
if (Test-Path $v1) {
    Copy-Item $v1 (Join-Path $staging "voices\en-us-x-multi.zvoice") -Force
}
if (Test-Path $v2) {
    Copy-Item $v2 (Join-Path $staging "voices\en-us-x-multi-seanet.zvoice") -Force
}

Write-Host "Staging completed successfully in $staging"
