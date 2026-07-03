# Captures every visible top-level window of a process to PNG files.
# Usage: capture_process_windows.ps1 -ProcessName default_mp -OutDir logs/live -Tag menu
param(
  [string]$ProcessName = "default_mp",
  [string]$OutDir = "logs/live",
  [string]$Tag = "capture"
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Capture {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
  public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder sb, int max);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

New-Item -ItemType Directory -Force $OutDir | Out-Null
$procs = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
if (-not $procs) { Write-Output "no process named $ProcessName"; exit 1 }
$pids = @($procs | ForEach-Object { $_.Id })
$index = 0
$captured = 0
$callback = {
  param($hWnd, $lParam)
  $winPid = 0
  [Win32Capture]::GetWindowThreadProcessId($hWnd, [ref]$winPid) | Out-Null
  if ($pids -contains [int]$winPid -and [Win32Capture]::IsWindowVisible($hWnd)) {
    $rect = New-Object Win32Capture+RECT
    [Win32Capture]::GetWindowRect($hWnd, [ref]$rect) | Out-Null
    $w = $rect.Right - $rect.Left; $h = $rect.Bottom - $rect.Top
    if ($w -gt 100 -and $h -gt 100) {
      $sb = New-Object System.Text.StringBuilder 256
      [Win32Capture]::GetWindowText($hWnd, $sb, 256) | Out-Null
      $title = ($sb.ToString() -replace '[^A-Za-z0-9 _-]', '') -replace ' +', '_'
      if (-not $title) { $title = "window" }
      $bmp = New-Object System.Drawing.Bitmap $w, $h
      $gfx = [System.Drawing.Graphics]::FromImage($bmp)
      $hdc = $gfx.GetHdc()
      # PW_RENDERFULLCONTENT = 2 captures DX swapchain content.
      [Win32Capture]::PrintWindow($hWnd, $hdc, 2) | Out-Null
      $gfx.ReleaseHdc($hdc)
      $gfx.Dispose()
      $path = Join-Path $script:OutDir ("{0}-{1}-{2}.png" -f $script:Tag, $script:index, $title)
      $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
      $bmp.Dispose()
      Write-Output ("captured {0} ({1}x{2}) -> {3}" -f $title, $w, $h, $path)
      $script:index++
      $script:captured++
    }
  }
  return $true
}
[Win32Capture]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null
Write-Output "windows captured: $captured"
