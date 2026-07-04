param(
  [string]$Capture = "native_captures/swap_fetch_capture_001/events.jsonl",
  [int]$Draw = 24,
  [int]$Iterations = 5,
  [string]$Configuration = "win-msvc-native-renderer-debug"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$buildDir = Join-Path $repoRoot "default\out\build\$Configuration"
$replayExe = Join-Path $buildDir "native_render_replay.exe"

if (!(Test-Path $replayExe)) {
  throw "native_render_replay.exe not found at $replayExe (build with scripts/windows/build_native_d3d12.ps1)"
}

$capturePath = Join-Path $repoRoot $Capture
if (!(Test-Path $capturePath)) {
  throw "Capture not found at $capturePath"
}

function Measure-ReplayBackend {
  param(
    [string]$Backend,
    [switch]$SkipUnsupported
  )

  $args = @(
    "--capture", $capturePath,
    "--backend", $Backend,
    "--draw", "$Draw",
    "--shader-cache-root", (Join-Path $repoRoot "shader_work/cache"),
    "--no-summary"
  )
  if ($SkipUnsupported) {
    $args += "--skip-unsupported"
  }

  $times = @()
  for ($i = 0; $i -lt $Iterations; $i++) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $replayExe @args *> $null
    if ($LASTEXITCODE -ne 0) {
      throw "Replay failed backend=$Backend iteration=$i exit=$LASTEXITCODE"
    }
    $sw.Stop()
    $times += $sw.Elapsed.TotalMilliseconds
  }

  $avg = ($times | Measure-Object -Average).Average
  $min = ($times | Measure-Object -Minimum).Minimum
  $max = ($times | Measure-Object -Maximum).Maximum
  return [pscustomobject]@{
    Backend = $Backend
    Iterations = $Iterations
    AvgMs = [math]::Round($avg, 2)
    MinMs = [math]::Round($min, 2)
    MaxMs = [math]::Round($max, 2)
  }
}

Write-Host "Benchmarking native renderer replay on $Capture draw=$Draw iterations=$Iterations"
Write-Host ""

$nullBackend = Measure-ReplayBackend -Backend "null"
$d3d12Backend = Measure-ReplayBackend -Backend "d3d12" -SkipUnsupported

Write-Host "Backend comparison (offline replay tool):"
Write-Host ("  null analysis:  avg={0}ms min={1}ms max={2}ms" -f $nullBackend.AvgMs, $nullBackend.MinMs, $nullBackend.MaxMs)
Write-Host ("  d3d12 real GPU: avg={0}ms min={1}ms max={2}ms" -f $d3d12Backend.AvgMs, $d3d12Backend.MinMs, $d3d12Backend.MaxMs)
Write-Host ""
Write-Host "Live native_d3d12 vs emulated in-game timing:"
Write-Host "  Run default.exe with native_renderer_mode=emulated and native_renderer_mode=native_d3d12"
Write-Host "  Compare frame logs / GPU frame time in your capture tooling."
