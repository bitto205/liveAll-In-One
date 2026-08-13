# LiveAIO 外部启动链（liveaio.exe）— 双击或开发时运行
$ErrorActionPreference = "Stop"
$Core = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Root = (Resolve-Path (Join-Path $Core "..")).Path
$Exe = Join-Path $Core "dist\liveaio.exe"

if (-not (Test-Path $Exe)) {
  & (Join-Path $PSScriptRoot "build.ps1")
}

Write-Host ">>> LiveAIO launcher: $Exe"
Write-Host ">>> 会拉起 core/dist/liveaio-core.exe；托盘在此进程"
& $Exe --root $Root @args
