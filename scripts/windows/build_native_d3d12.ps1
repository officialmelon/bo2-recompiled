param(
  [string]$Target = "native_render_replay",
  [int]$Jobs = 1,
  [string]$Configuration = "win-amd64-clangmsvc-debug"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$buildDir = Join-Path $repoRoot "default\out\build\$Configuration"
$vsRoot = "C:\Program Files\Microsoft Visual Studio\18\Community"
$vsDevCmd = Join-Path $vsRoot "Common7\Tools\VsDevCmd.bat"
$vsCMakeBin = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$vsNinja = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if (!(Test-Path $vsDevCmd)) {
  throw "VsDevCmd.bat not found at $vsDevCmd"
}
if (!(Test-Path $vsNinja)) {
  throw "Visual Studio ninja.exe not found at $vsNinja"
}
if (!(Test-Path $buildDir)) {
  throw "Build directory not found at $buildDir"
}

$batchPath = Join-Path $env:TEMP ("bo2-native-d3d12-build-{0}.cmd" -f ([guid]::NewGuid().ToString("N")))
$batch = @"
@echo off
call "$vsDevCmd" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
set "PATH=$vsCMakeBin;%PATH%"
"$vsNinja" -C "$buildDir" -j$Jobs $Target
exit /b %errorlevel%
"@

try {
  Set-Content -LiteralPath $batchPath -Value $batch -Encoding ASCII
  cmd.exe /d /s /c "`"$batchPath`""
  if ($LASTEXITCODE -ne 0) {
    throw "native D3D12 build failed for target '$Target' with exit code $LASTEXITCODE"
  }
} finally {
  Remove-Item -LiteralPath $batchPath -Force -ErrorAction SilentlyContinue
}
