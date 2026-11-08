param([int]$W = 0, [int]$H = 0, [int]$Steps = 1, [string]$Out = "shot_screen.png")
# Screen-capture variant of screenshot.ps1: BitBlt the REAL screen pixels of the
# app window (PrintWindow sends WM_PRINT and re-renders everything, which masks
# invalid-region artifacts). -Steps > 1 resizes incrementally to mimic dragging.
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
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
}
"@
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

# Raise the window above whatever is occluding it (topmost round-trip trick),
# otherwise CopyFromScreen captures the overlapping window's pixels.
[Win32]::SetWindowPos($hwndApp, [IntPtr](-1), 0, 0, 0, 0, 0x0003) | Out-Null  # HWND_TOPMOST | NOMOVE|NOSIZE
[Win32]::SetWindowPos($hwndApp, [IntPtr](-2), 0, 0, 0, 0, 0x0003) | Out-Null  # HWND_NOTOPMOST
Start-Sleep -Milliseconds 200

$wr = New-Object Win32+RECT
[Win32]::GetWindowRect($hwndApp, [ref]$wr) | Out-Null
$curW = $wr.R - $wr.L

if ($W -gt 0 -and $H -gt 0) {
  if ($Steps -le 1) {
    [Win32]::SetWindowPos($hwndApp, [IntPtr]::Zero, 0, 0, $W, $H, 0x0006) | Out-Null
    Start-Sleep -Milliseconds 400
  } else {
    for ($i = 1; $i -le $Steps; $i++) {
      $sw = $curW + [int](($W - $curW) * $i / $Steps)
      [Win32]::SetWindowPos($hwndApp, [IntPtr]::Zero, 0, 0, $sw, $H, 0x0006) | Out-Null
      Start-Sleep -Milliseconds 25
    }
    Start-Sleep -Milliseconds 400
  }
}

[Win32]::GetWindowRect($hwndApp, [ref]$wr) | Out-Null
$w = $wr.R - $wr.L; $ht = $wr.B - $wr.T
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($wr.L, $wr.T, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "saved $Out (screen capture ${w}x${ht})"
