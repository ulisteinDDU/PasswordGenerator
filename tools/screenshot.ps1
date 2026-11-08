param([int]$W = 0, [int]$H = 0, [string]$Out = "shot.png")
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [Out] char[] buf, int max);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr FindWindowW(string cls, string title);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
}
"@
# PowerShell is a DPI-unaware process here: window coordinates get virtualized
# and FindWindowW returns 0 for every class (even Progman). Switch this thread
# to PerMonitorV2, then locate the app by window class via EnumWindows +
# GetClassNameW (class-based lookup, avoids the Chinese-title encoding issue).
# NOTE: keep the window handle in $hwndApp - PS variables are case-insensitive,
# so a plain $h would clobber the -H parameter.
[Win32]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$hits = New-Object System.Collections.ArrayList
$cb = [Win32+EnumProc]{ param($wh, $l)
  $b = New-Object char[] 64
  $n = [Win32]::GetClassNameW($wh, $b, 64)
  if ((New-Object string $b,0,$n) -eq "CrtCipherPwdGen") { $hits.Add($wh) | Out-Null }
  return $true
}
[Win32]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
$hwndApp = ($hits | Select-Object -First 1)
if (-not $hwndApp -or $hwndApp -eq [IntPtr]::Zero) { throw "window not found: run PasswordGenerator.exe first" }
if ($W -gt 0 -and $H -gt 0) {
  # SWP_NOMOVE|SWP_NOZORDER (0x0006): resize only, keep position.
  [Win32]::SetWindowPos($hwndApp, [IntPtr]::Zero, 0, 0, $W, $H, 0x0006) | Out-Null
  Start-Sleep -Milliseconds 400
}
$cr = New-Object Win32+RECT
[Win32]::GetClientRect($hwndApp, [ref]$cr) | Out-Null
# PrintWindow renders the FULL window (title bar included), so size the bitmap
# from the window rect to avoid cropping the bottom controls.
$wr = New-Object Win32+RECT
[Win32]::GetWindowRect($hwndApp, [ref]$wr) | Out-Null
$w = $wr.R - $wr.L; $ht = $wr.B - $wr.T
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[Win32]::PrintWindow($hwndApp, $dc, 2) | Out-Null   # PW_RENDERFULLCONTENT
$g.ReleaseHdc($dc); $g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host ("saved $Out (window ${w}x${ht}, client " + ($cr.R-$cr.L) + "x" + ($cr.B-$cr.T) + ")")
