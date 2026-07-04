# Measures live guest frame rate of the BO2 MP app under different renderer
# modes by letting the game run for a fixed duration and parsing the native
# renderer frame telemetry from the log file.
#
#   .\scripts\windows\benchmark_live_fps.ps1 -Mode native_d3d12 -DurationSec 90
#   .\scripts\windows\benchmark_live_fps.ps1 -Mode native -DurationSec 90   # hooks only, emulated presents
#
# The native_d3d12 run measures the native pipeline; the "native" run leaves
# emulated rendering in charge (hooks trace frames) and is the emulated
# baseline. FPS = (last_frame_index - first_frame_index) / elapsed between the
# corresponding log lines.
param(
  [ValidateSet("native_d3d12", "native", "native_null")]
  [string]$Mode = "native_d3d12",
  [string]$LivePipeline = "xenia",
  [int]$DurationSec = 90,
  [int]$WarmupSec = 30,
  [string]$Tag = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$exe = Join-Path $repoRoot "default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget\default_mp.exe"
if (!(Test-Path $exe)) { throw "default_mp.exe not found at $exe" }

if (-not $Tag) { $Tag = "$Mode-$(Get-Date -Format yyyyMMdd-HHmmss)" }
$logDir = Join-Path $repoRoot "logs\bench"
New-Item -ItemType Directory -Force $logDir | Out-Null
$logPath = Join-Path $logDir "$Tag.log"

$args = @(
  "--native_renderer_mode", $Mode,
  "--native_renderer_verbose", "true",
  "--mnk_mode", "true",
  "--log_file", $logPath
)
if ($Mode -eq "native_d3d12") {
  $args += @("--native_renderer_live_pipeline", $LivePipeline)
}

Write-Host "launching $Mode (pipeline=$LivePipeline) for ${DurationSec}s (warmup ${WarmupSec}s)..."
$proc = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $repoRoot -PassThru
try {
  Start-Sleep -Seconds ($WarmupSec + $DurationSec)
} finally {
  if (!$proc.HasExited) { $proc.Kill() }
}
Start-Sleep -Seconds 2

# Frame telemetry lines carry a timestamp and a monotonically increasing frame
# index: "... BO2 native D3D12 frame 1234 end ..." (native) or the null/trace
# backend equivalent "frame 1234 end".
$samples = @()
foreach ($line in Get-Content $logPath) {
  if ($line -match '^\[?(?<ts>[\d\-\.: ]+)\]?.*frame (?<idx>\d+) end') {
    $ts = $null
    foreach ($fmt in @('yyyy-MM-dd HH:mm:ss.fff', 'HH:mm:ss.fff')) {
      try { $ts = [datetime]::ParseExact($Matches.ts.Trim(), $fmt, $null); break } catch {}
    }
    if ($ts) { $samples += [pscustomobject]@{ Time = $ts; Frame = [long]$Matches.idx } }
  }
}
if ($samples.Count -lt 2) {
  Write-Warning "not enough frame telemetry in $logPath (found $($samples.Count) samples)"
  exit 1
}
# Skip warmup: drop samples in the first WarmupSec.
$t0 = $samples[0].Time.AddSeconds($WarmupSec)
$steady = $samples | Where-Object { $_.Time -ge $t0 }
if ($steady.Count -lt 2) { $steady = $samples }
$first = $steady[0]; $last = $steady[-1]
$elapsed = ($last.Time - $first.Time).TotalSeconds
$frames = $last.Frame - $first.Frame
$fps = if ($elapsed -gt 0) { $frames / $elapsed } else { 0 }
[pscustomobject]@{
  Mode = $Mode
  Pipeline = $LivePipeline
  Log = $logPath
  SteadySamples = $steady.Count
  Frames = $frames
  ElapsedSec = [math]::Round($elapsed, 1)
  AvgFps = [math]::Round($fps, 1)
} | Format-List
