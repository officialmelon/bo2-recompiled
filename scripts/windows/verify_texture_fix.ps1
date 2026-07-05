param(
  [int]$WaitSec = 24,
  [string]$Tag = "texfix"
)
$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$exe = Join-Path $repoRoot "default_mp\out\build\win-clang-msvc-amd64-relwithdebinfo-msvctarget\default_mp.exe"
if (!(Test-Path $exe)) { throw "default_mp.exe not found at $exe" }
$outDir = Join-Path $repoRoot "logs\live\verify_$Tag"
New-Item -ItemType Directory -Force $outDir | Out-Null
$logPath = Join-Path $outDir "run.log"

# NOTE: deliberately NO --mnk_mode. The global keyboard hook it installs is the
# main desktop-freeze cause; a visual menu capture does not need input.
$args = @(
  "--native_renderer_mode", "native_d3d12",
  "--native_renderer_live_pipeline", "xenia",
  "--native_renderer_verbose", "true",
  "--log_file", $logPath
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;
public class Cap {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string n);
  [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr c, string cls, string title);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l,t,r,b; }
  public static IntPtr FindByTitleContains(string sub) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h,l)=>{
      int len = GetWindowTextLength(h); if(len<=0) return true;
      var sb = new System.Text.StringBuilder(len+1); GetWindowText(h, sb, sb.Capacity);
      if (sb.ToString().Contains(sub)) { found = h; return false; } return true;
    }, IntPtr.Zero);
    return found;
  }
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int m);
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
  public static void Grab(IntPtr h, string path) {
    RECT r; GetClientRect(h, out r);
    int w = r.r-r.l, ht = r.b-r.t; if (w<=0||ht<=0){w=1280;ht=720;}
    using (var bmp = new Bitmap(w, ht)) {
      using (var g = Graphics.FromImage(bmp)) {
        IntPtr hdc = g.GetHdc();
        PrintWindow(h, hdc, 2); // PW_RENDERFULLCONTENT
        g.ReleaseHdc(hdc);
      }
      bmp.Save(path, ImageFormat.Png);
    }
  }
}
"@ -ReferencedAssemblies System.Drawing

Write-Host "launching (no mnk_mode) for ${WaitSec}s..."
$proc = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $repoRoot -PassThru
$shot = Join-Path $outDir "native_menu.png"
try {
  Start-Sleep -Seconds $WaitSec
  $h = [Cap]::FindByTitleContains("BO2 Native D3D12")
  if ($h -ne [IntPtr]::Zero) {
    [Cap]::Grab($h, $shot)
    Write-Host "captured -> $shot"
  } else {
    Write-Warning "native window not found"
  }
} finally {
  if (!$proc.HasExited) { $proc.Kill(); $proc.WaitForExit(4000) | Out-Null }
}
Write-Host "done. log=$logPath shot=$shot"
