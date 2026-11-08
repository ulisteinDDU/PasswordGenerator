param([int]$Id = 0, [string]$Out = "preset_click.png")
# Click a preset button (WM_COMMAND BN_CLICKED by control id) then screenshot
# the app window. Usage: click_preset.ps1 -Id 1013 -Out shot.png
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
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
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
if ($Id -gt 0) {
  # lParam 0 is fine here: this app dispatches WM_COMMAND on LOWORD(wParam).
  [Win32]::SendMessageW($hwndApp, 0x0111, [IntPtr]$Id, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 400
}
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
Write-Host "clicked id=$Id saved $Out (${w}x${ht})"
