# batch_verify.ps1 - end-to-end check for the batch-generate dialog.
# Run while PasswordGenerator.exe is running. Steps:
#   1. locate main window; verify startup centering (work-area center delta)
#   2. open batch dialog (WM_COMMAND 1021); verify client size ~ 50% of main
#   3. screenshot dialog; fill count=12 and output path via child controls
#   4. post GO (1105); capture the result MessageBox text; dismiss it
# Self-contained; closes nothing (caller closes the app).
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Bv {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct MONITORINFO {
    public int cbSize; public RECT rcMonitor; public RECT rcWork; public uint dwFlags; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [Out] char[] buf, int max);
  [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int idx);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h, uint flags);
  [DllImport("user32.dll")] public static extern bool GetMonitorInfoW(IntPtr h, ref MONITORINFO mi);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessageW2(IntPtr h, uint msg, IntPtr wp, string lp);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, [Out] char[] buf, int max);
  [DllImport("user32.dll")] public static extern bool SetWindowTextW(IntPtr h, string s);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
}
"@
function Find-Windows([string]$cls) {
  $hits = New-Object System.Collections.ArrayList
  $cb = [Bv+EnumProc]{ param($wh, $l)
    $b = New-Object char[] 64; $n = [Bv]::GetClassNameW($wh, $b, 64)
    if ((New-Object string $b,0,$n) -eq $cls) { $hits.Add($wh) | Out-Null }
    return $true }
  [Bv]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return ,$hits
}
[Bv]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null

# --- 1. main window + centering check ---
$main = (Find-Windows "CrtCipherPwdGen") | Select-Object -First 1
if (-not $main) { throw "main window not found" }
$wr = New-Object Bv+RECT; [Bv]::GetWindowRect($main, [ref]$wr) | Out-Null
$mi = New-Object Bv+MONITORINFO; $mi.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf($mi)
[Bv]::GetMonitorInfoW([Bv]::MonitorFromWindow($main, 1), [ref]$mi) | Out-Null
$wc = $wr.L + ($wr.R - $wr.L) / 2; $hc = $wr.T + ($wr.B - $wr.T) / 2
$ec = $mi.rcWork.L + ($mi.rcWork.R - $mi.rcWork.L) / 2
$ecy = $mi.rcWork.T + ($mi.rcWork.B - $mi.rcWork.T) / 2
Write-Host ("CENTERING: win-center=({0},{1}) work-center=({2},{3}) delta=({4},{5})" -f $wc, $hc, $ec, $ecy, ($wc - $ec), ($hc - $ecy))

# --- 2. open batch dialog (POST: the handler runs a modal loop until the
# dialog closes, so a synchronous SendMessage would deadlock the driver) ---
[Bv]::PostMessageW($main, 0x0111, [IntPtr]1021, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 600
$dlg = (Find-Windows "CrtBatchDlg") | Select-Object -First 1
if (-not $dlg) { throw "batch dialog not found" }
$mc = New-Object Bv+RECT; [Bv]::GetClientRect($main, [ref]$mc) | Out-Null
$dc = New-Object Bv+RECT; [Bv]::GetClientRect($dlg, [ref]$dc) | Out-Null
Write-Host ("DIALOG-SIZE: main-client={0}x{1} dialog-client={2}x{3} ratio=({4:F2},{5:F2})" -f `
  ($mc.R-$mc.L), ($mc.B-$mc.T), ($dc.R-$dc.L), ($dc.B-$dc.T), `
  (($dc.R-$dc.L) / ($mc.R-$mc.L)), (($dc.B-$dc.T) / ($mc.B-$mc.T)))

# --- 3. screenshot + fill inputs ---
$dr = New-Object Bv+RECT; [Bv]::GetWindowRect($dlg, [ref]$dr) | Out-Null
$w = $dr.R - $dr.L; $h = $dr.B - $dr.T
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$gdc = $g.GetHdc(); [Bv]::PrintWindow($dlg, $gdc, 2) | Out-Null
$g.ReleaseHdc($gdc); $g.Dispose()
$bmp.Save("tools/batch_dlg.png", [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
Write-Host "saved tools/batch_dlg.png"

$ctl = @{}
$ccb = [Bv+EnumProc]{ param($wh, $l)
  $id = [Bv]::GetWindowLongW($wh, -12)
  if ($id -gt 0) { $script:ctl[[int]$id] = $wh }
  return $true }
[Bv]::EnumChildWindows($dlg, $ccb, [IntPtr]::Zero) | Out-Null
if (-not $ctl[1101] -or -not $ctl[1103]) { throw "dialog controls not found" }
[Bv]::SendMessageW2($ctl[1101], 0x000C, [IntPtr]0, "12") | Out-Null
[Bv]::SendMessageW2($ctl[1103], 0x000C, [IntPtr]0, "D:\data\qx\claude\crt-cipher\tools\batch_test_out.txt") | Out-Null
Start-Sleep -Milliseconds 300

# --- 4. post GO, capture result MessageBox, dismiss ---
[Bv]::PostMessageW($dlg, 0x0111, [IntPtr]1105, [IntPtr]::Zero) | Out-Null
$boxTxt = $null; $boxH = [IntPtr]::Zero
for ($i = 0; $i -lt 50; $i++) {
  Start-Sleep -Milliseconds 100
  $boxes = New-Object System.Collections.ArrayList
  $bcb = [Bv+EnumProc]{ param($wh, $l)
    $b = New-Object char[] 64; $n = [Bv]::GetClassNameW($wh, $b, 64)
    if ((New-Object string $b,0,$n) -eq "#32770") {
      $bp = 0; [Bv]::GetWindowThreadProcessId($wh, [ref]$bp) | Out-Null
      $mp = 0; [Bv]::GetWindowThreadProcessId($main, [ref]$mp) | Out-Null
      if ($bp -eq $mp -and $wh -ne $dlg) { $boxes.Add($wh) | Out-Null } }
    return $true }
  [Bv]::EnumWindows($bcb, [IntPtr]::Zero) | Out-Null
  if ($boxes.Count -gt 0) {
    $boxH = ($boxes | Select-Object -First 1)
    $tb = New-Object char[] 1024
    $n2 = [Bv]::GetWindowTextW($boxH, $tb, 1024)
    $boxTxt = (New-Object string $tb,0,$n2)
    break }
}
if ($boxTxt) {
  # caption length distinguishes outcome: 4 = success, 2 = warning/error
  Write-Host ("MSGBOX-CAPTION-LEN: {0}" -f $boxTxt.Length)
  [Bv]::PostMessageW($boxH, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null  # WM_CLOSE
} else {
  Write-Host "MSGBOX: none found (check file output anyway)"
}
Start-Sleep -Milliseconds 500
Write-Host "batch_verify done"
