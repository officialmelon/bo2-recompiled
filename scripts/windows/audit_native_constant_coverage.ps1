param(
  [string]$Capture = "native_captures/live_d3d12_mp_028/events.jsonl",
  [string]$SemanticDir = "shader_work/cache/disasm",
  [int]$Top = 20
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$capturePath = if ([System.IO.Path]::IsPathRooted($Capture)) {
  $Capture
} else {
  Join-Path $repoRoot $Capture
}
$semanticRoot = if ([System.IO.Path]::IsPathRooted($SemanticDir)) {
  $SemanticDir
} else {
  Join-Path $repoRoot $SemanticDir
}

if (!(Test-Path $capturePath)) {
  throw "Capture not found: $capturePath"
}
if (!(Test-Path $semanticRoot)) {
  throw "Semantic directory not found: $semanticRoot"
}

function Convert-BitmapWordsToRegisters {
  param([string[]]$Words)

  $registers = New-Object System.Collections.Generic.List[int]
  for ($wordIndex = 0; $wordIndex -lt $Words.Count; $wordIndex++) {
    $wordText = $Words[$wordIndex].Trim()
    if ($wordText.StartsWith("0x")) {
      $wordText = $wordText.Substring(2)
    }
    if ($wordText.Length -eq 0) {
      continue
    }
    $word = [Convert]::ToUInt64($wordText, 16)
    for ($bit = 0; $bit -lt 64; $bit++) {
      if (($word -band ([uint64]1 -shl $bit)) -ne 0) {
        $registers.Add(($wordIndex * 64) + $bit)
      }
    }
  }
  return $registers.ToArray()
}

$shaderConstants = @{}
Get-ChildItem -Path $semanticRoot -Filter "*.xenos.semantic.txt" -File |
  ForEach-Object {
    $nameMatch = [regex]::Match(
      $_.Name,
      '^(?<stage>[PV]S)_0x(?<hash>[0-9A-Fa-f]+)\.xenos\.semantic\.txt$')
    if (!$nameMatch.Success) {
      return
    }
    $content = Get-Content $_.FullName -Raw
    $bitmapMatch = [regex]::Match(
      $content,
      'float_constant_bitmap=([^\r\n]+)')
    if (!$bitmapMatch.Success) {
      return
    }
    $words = $bitmapMatch.Groups[1].Value -split ',' |
      ForEach-Object { $_.Trim() } |
      Where-Object { $_.Length -gt 0 }
    $registers = Convert-BitmapWordsToRegisters -Words $words
    $key = ("0x{0}" -f $nameMatch.Groups["hash"].Value.ToUpperInvariant())
    $shaderConstants[$key] = [pscustomobject]@{
      Stage = $nameMatch.Groups["stage"].Value.ToUpperInvariant()
      Hash = $key
      Registers = $registers
      Path = $_.FullName
    }
  }

function New-RangeSet {
  return New-Object "System.Collections.Generic.HashSet[int]"
}

function Normalize-Hash {
  param($Value)

  if ($null -eq $Value) {
    return $null
  }
  $text = ([string]$Value).Trim()
  if ($text.Length -eq 0 -or $text -eq "0") {
    return $null
  }
  if (!$text.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
    $text = "0x$text"
  }
  return $text.ToUpperInvariant()
}

function Add-ConstantRange {
  param(
    [System.Collections.Generic.HashSet[int]]$Set,
    [int]$Index,
    [int]$DwordCount
  )

  if (($Index -band 7) -ne 0) {
    return
  }
  $first = [int]($Index / 8)
  $count = [Math]::Max(1, [int][Math]::Ceiling($DwordCount / 4.0))
  for ($i = 0; $i -lt $count; $i++) {
    [void]$Set.Add($first + $i)
  }
}

$boundFloatConstants = New-RangeSet
$currentVs = $null
$currentPs = $null
$drawIndex = 0
$missingByShader = @{}
$missingByPair = @{}
$drawFindings = New-Object System.Collections.Generic.List[object]
$lineNumber = 0

Get-Content $capturePath | ForEach-Object {
  $lineNumber++
  try {
    if ([string]::IsNullOrWhiteSpace($_)) {
      return
    }
    $event = $_ | ConvertFrom-Json
    switch ($event.type) {
    "pm4_shader" {
      $hash = Normalize-Hash $event.shader_hash
      if ($event.shader_type -eq 0) {
        $currentVs = $hash
      } elseif ($event.shader_type -eq 1) {
        $currentPs = $hash
      }
    }
    "render_command" {
      if ($event.command -eq "BindShader") {
        $hash = $null
        if ($event.arg1 -ne $null -or $event.arg2 -ne $null) {
          $hash = ("0x{0:X8}{1:X8}" -f [uint32]$event.arg1, [uint32]$event.arg2)
        }
        if ($event.arg0 -eq 0) {
          $currentVs = $hash
        } elseif ($event.arg0 -eq 1) {
          $currentPs = $hash
        }
      }
    }
    "pm4_constants" {
      if ($event.constant_type -eq 0) {
        Add-ConstantRange -Set $boundFloatConstants `
          -Index ([int]$event.index) `
          -DwordCount ([int]$event.dword_count)
      }
    }
    "pm4_draw" {
      $vs = $currentVs
      $ps = $currentPs
      $eventVs = Normalize-Hash $event.vertex_shader_hash
      $eventPs = Normalize-Hash $event.pixel_shader_hash
      if ($eventVs) {
        $vs = $eventVs
      }
      if ($eventPs) {
        $ps = $eventPs
      }
      foreach ($hash in @($vs, $ps)) {
        if (!$hash -or !$shaderConstants.ContainsKey($hash)) {
          continue
        }
        $needed = $shaderConstants[$hash].Registers
        if (!$needed -or $needed.Count -eq 0) {
          continue
        }
        $missing = @($needed | Where-Object { !$boundFloatConstants.Contains([int]$_) })
        if ($missing.Count -eq 0) {
          continue
        }
        if (!$missingByShader.ContainsKey($hash)) {
          $missingByShader[$hash] = [pscustomobject]@{
            Shader = $hash
            Stage = $shaderConstants[$hash].Stage
            Draws = 0
            Missing = New-Object "System.Collections.Generic.SortedSet[int]"
          }
        }
        $missingByShader[$hash].Draws++
        foreach ($reg in $missing) {
          [void]$missingByShader[$hash].Missing.Add([int]$reg)
        }

        $pairKey = "$vs/$ps"
        if (!$missingByPair.ContainsKey($pairKey)) {
          $missingByPair[$pairKey] = [pscustomobject]@{
            Pair = $pairKey
            Draws = 0
            Missing = New-Object "System.Collections.Generic.SortedSet[string]"
          }
        }
        $missingByPair[$pairKey].Draws++
        foreach ($reg in $missing) {
          [void]$missingByPair[$pairKey].Missing.Add("${hash}:c$reg")
        }

        if ($drawFindings.Count -lt $Top) {
          $drawFindings.Add([pscustomobject]@{
            Draw = $drawIndex
            VS = $vs
            PS = $ps
            Shader = $hash
            Missing = (($missing | ForEach-Object { "c$_" }) -join ",")
          })
        }
      }
      $drawIndex++
    }
  }
  } catch {
    throw "Failed while processing capture line ${lineNumber}: $($_.Exception.Message)"
  }
}

Write-Host "Native constant coverage audit"
Write-Host "  capture: $capturePath"
Write-Host "  semantic_dir: $semanticRoot"
Write-Host "  semantic_shaders: $($shaderConstants.Count)"
Write-Host ""

Write-Host "Top shader missing-constant groups:"
$missingByShader.Values |
  Sort-Object -Property Draws -Descending |
  Select-Object -First $Top |
  ForEach-Object {
    $regs = ($_.Missing | ForEach-Object { "c$_" }) -join ","
    Write-Host ("  {0} {1} draws={2} missing={3}" -f $_.Stage, $_.Shader, $_.Draws, $regs)
  }

Write-Host ""
Write-Host "Top shader-pair missing-constant groups:"
$missingByPair.Values |
  Sort-Object -Property Draws -Descending |
  Select-Object -First $Top |
  ForEach-Object {
    $regs = ($_.Missing | Select-Object -First 24) -join ","
    Write-Host ("  {0} draws={1} missing={2}" -f $_.Pair, $_.Draws, $regs)
  }

Write-Host ""
Write-Host "First draw findings:"
$drawFindings | ForEach-Object {
  Write-Host ("  draw={0} VS={1} PS={2} shader={3} missing={4}" -f $_.Draw, $_.VS, $_.PS, $_.Shader, $_.Missing)
}
