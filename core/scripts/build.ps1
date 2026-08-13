# Build liveaio.exe (launcher) + liveaio-core.exe (service) into core/dist/
$ErrorActionPreference = "Stop"
$Core = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Root = (Resolve-Path (Join-Path $Core "..")).Path
$OutDir = Join-Path $Core "dist"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$CoreOut = Join-Path $OutDir "liveaio-core.exe"
$MainOut = Join-Path $OutDir "liveaio.exe"
Push-Location $Core
try {
  Write-Host ">>> go mod tidy"
  go mod tidy
  Write-Host ">>> building core service: $CoreOut"
  $env:CGO_ENABLED = "0"
  go build -o $CoreOut ./cmd/liveaio-core
  Write-Host ">>> building launcher: $MainOut"
  go build -o $MainOut ./cmd/liveaio
  Write-Host ">>> building smoke tool"
  go build -o (Join-Path $OutDir "liveaio-smoke.exe") ./cmd/liveaio-smoke
  Copy-Item -Force $MainOut (Join-Path $Root "LiveAIO.exe")
  Write-Host ">>> user entry: $(Join-Path $Root 'LiveAIO.exe')"
  Write-Host ">>> core service: $CoreOut"
} finally {
  Pop-Location
}
