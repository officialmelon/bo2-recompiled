# Sends key taps to the ReXGlue main window (the one that owns input focus).
# Usage: send_game_keys.ps1 -Keys "DOWN,DOWN,SPACE" [-HoldMs 80] [-GapMs 350]
param(
  [string]$Keys = "SPACE",
  [int]$HoldMs = 80,
  [int]$GapMs = 350,
  [string]$TitleMatch = "rexglue"
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class GameInput {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
  public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder sb, int max);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint uCode, uint uMapType);
}
"@

$vkMap = @{ "DOWN" = 0x28; "UP" = 0x26; "LEFT" = 0x25; "RIGHT" = 0x27; "SPACE" = 0x20; "ENTER" = 0x0D; "ESC" = 0x1B; "TAB" = 0x09 }

$target = [IntPtr]::Zero
$callback = {
  param($hWnd, $lParam)
  if ([GameInput]::IsWindowVisible($hWnd)) {
    $sb = New-Object System.Text.StringBuilder 256
    [GameInput]::GetWindowText($hWnd, $sb, 256) | Out-Null
    if ($sb.ToString() -match $script:TitleMatch) {
      $script:target = $hWnd
      return $false
    }
  }
  return $true
}
[GameInput]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null
if ($target -eq [IntPtr]::Zero) { Write-Output "window not found for '$TitleMatch'"; exit 1 }
[GameInput]::SetForegroundWindow($target) | Out-Null
Start-Sleep -Milliseconds 600

foreach ($name in $Keys -split ",") {
  $key = $name.Trim().ToUpper()
  if (-not $vkMap.ContainsKey($key)) { Write-Output "unknown key $key"; continue }
  $vk = [byte]$vkMap[$key]
  $scan = [byte][GameInput]::MapVirtualKey($vk, 0)
  [GameInput]::keybd_event($vk, $scan, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds $HoldMs
  [GameInput]::keybd_event($vk, $scan, 2, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds $GapMs
  Write-Output "tapped $key"
}
