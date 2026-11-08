# tools/acceptance.ps1 - final acceptance driver for PasswordGenerator.exe
# Drives the RUNNING app via user32 messages (no mouse needed):
#   1. find main window by class CrtCipherPwdGen (EnumWindows technique from screenshot.ps1)
#   2. click preset-256 button (WM_COMMAND, ID 1018), wait, screenshot tools/shot_final_256.png
#   3. click the copy button (WM_COMMAND, ID 1010), wait, print clipboard length + first 12 chars
#   4. tier sweep: preset 8 + digits-only (uncheck lower/upper), screenshot tools/shot_final_weak.png
#   5. auto-close the app (Stop-Process PasswordGenerator) once the run completes
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools/acceptance.ps1
# NOTE: the script closes PasswordGenerator.exe when it finishes, so no process is left behind.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int idx);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [Out] char[] buf, int max);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
}
"@

# PowerShell is a DPI-unaware process: FindWindow/coordinates get virtualized.
# Switch this thread to PerMonitorV2, then locate the app by window class.
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
Write-Host ("found window handle: " + $hwndApp)

# full-window screenshot via PrintWindow PW_RENDERFULLCONTENT (2)
function Save-Shot([IntPtr]$hwnd, [string]$path) {
  $wr = New-Object Win32+RECT
  [Win32]::GetWindowRect($hwnd, [ref]$wr) | Out-Null
  $w = $wr.R - $wr.L; $ht = $wr.B - $wr.T
  $bmp = New-Object System.Drawing.Bitmap($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc()
  [Win32]::PrintWindow($hwnd, $dc, 2) | Out-Null
  $g.ReleaseHdc($dc); $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  Write-Host ("saved " + $path + " (window " + $w + "x" + $ht + ")")
}

# --- 1. click preset 256 (IDC_BTN_P256 = 1018) via WM_COMMAND ---
# lParam is 0 here; a real WM_COMMAND notification carries the control HWND in lParam,
# but this app dispatches on LOWORD(wParam) only, so 0 is safe HERE (caution if reusing elsewhere).
$WM_COMMAND = 0x0111
[Win32]::SendMessageW($hwndApp, $WM_COMMAND, [IntPtr]1018, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 300

# --- 2. screenshot the full window ---
Save-Shot $hwndApp (Join-Path $PSScriptRoot 'shot_final_256.png')

# --- 3. click copy (IDC_BTN_COPY = 1010), then read clipboard ---
[Win32]::SendMessageW($hwndApp, $WM_COMMAND, [IntPtr]1010, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 500
$text = Get-Clipboard -Raw
if ($null -eq $text) { throw "clipboard is empty" }
$len = $text.Length
$sample = if ($len -ge 12) { $text.Substring(0, 12) } else { $text }
Write-Host ("CLIPBOARD_LENGTH=" + $len)
Write-Host ("CLIPBOARD_SAMPLE_FIRST12=" + $sample)

# --- 4. tier sweep: preset 8 + digits-only -> weak tier (8*log2(10) ~ 26.6 bit) ---
# WM_COMMAND alone does not flip a checkbox control: BM_SETCHECK the child first
# (on_generate reads the actual check states), then send its WM_COMMAND to regenerate.
$BM_SETCHECK = 0x00F1
$BST_CHECKED = [IntPtr]1
$BST_UNCHECKED = [IntPtr]0
$kids = New-Object System.Collections.ArrayList
$cbKids = [Win32+EnumProc]{ param($wh, $l)
  $kids.Add($wh) | Out-Null
  return $true
}
[Win32]::EnumChildWindows($hwndApp, $cbKids, [IntPtr]::Zero) | Out-Null
$ctl = @{}                                # control id (GWL_ID = -12) -> child hwnd
foreach ($k in $kids) { $ctl[[Win32]::GetWindowLongW($k, -12)] = $k }

[Win32]::SendMessageW($hwndApp, $WM_COMMAND, [IntPtr]1011, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 200
foreach ($id in @(1004, 1005)) {          # uncheck lower + upper -> digits-only pool (10 chars)
  [Win32]::SendMessageW($ctl[$id], $BM_SETCHECK, $BST_UNCHECKED, [IntPtr]::Zero) | Out-Null
  [Win32]::SendMessageW($hwndApp, $WM_COMMAND, [IntPtr]$id, [IntPtr]::Zero) | Out-Null
}
Start-Sleep -Milliseconds 300
Save-Shot $hwndApp (Join-Path $PSScriptRoot 'shot_final_weak.png')

foreach ($id in @(1004, 1005)) {          # restore checkboxes (tidy; app closes anyway)
  [Win32]::SendMessageW($ctl[$id], $BM_SETCHECK, $BST_CHECKED, [IntPtr]::Zero) | Out-Null
  [Win32]::SendMessageW($hwndApp, $WM_COMMAND, [IntPtr]$id, [IntPtr]::Zero) | Out-Null
}

# --- 5. close the app so reruns never leave PasswordGenerator.exe running ---
Stop-Process -Name PasswordGenerator -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500
Write-Host "app closed"
