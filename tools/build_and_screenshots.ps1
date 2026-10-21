# Lab 5 (TPP), variant 23: build SFML_Lab with CMake + vcpkg (Visual Studio C++),
# run the computational experiment and take screenshots for the report.
#
#   -BuildOnly   only configure and build
#   -NoShots     build + experiment, no screenshots
#   -ShotsOnly   only screenshots (the program must already be built)
#
# First build: vcpkg downloads and compiles SFML and its dependencies (5-15 min).
# During the screenshot part (~2.5 min) do not touch the mouse or keyboard.

param([switch]$BuildOnly, [switch]$NoShots, [switch]$ShotsOnly)

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$docs  = Join-Path $root 'docs'
$shots = Join-Path $docs 'screenshots'
New-Item -ItemType Directory -Force $docs, $shots | Out-Null
$log = Join-Path $docs ($(if ($ShotsOnly) { 'screenshots_log.txt' } else { 'build_log.txt' }))
Set-Content -Path $log -Value "start $(Get-Date -Format s)" -Encoding UTF8
function Log([string]$m) { Write-Host $m; Add-Content -Path $log -Value $m -Encoding UTF8 }

# ------------------------------------------------------------------ 1. configure + build
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found - is Visual Studio installed?' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with C++ tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
Log "Visual Studio: $vs"

$bat = @"
@echo off
call "$vcvars" >nul || exit /b 1
if not defined VCPKG_ROOT if exist "$vs\VC\vcpkg\vcpkg.exe" set "VCPKG_ROOT=$vs\VC\vcpkg"
if not defined VCPKG_ROOT (echo VCPKG_ROOT is not set: install the "vcpkg package manager" component & exit /b 2)
echo VCPKG_ROOT=%VCPKG_ROOT%
cmake --version
"%VCPKG_ROOT%\vcpkg.exe" --version
cd /d "$root"
findstr /c:"builtin-baseline" vcpkg.json >nul
if errorlevel 1 (
  echo ---- vcpkg x-update-baseline --add-initial-baseline ----
  "%VCPKG_ROOT%\vcpkg.exe" x-update-baseline --add-initial-baseline || exit /b 3
)
echo ---- vcpkg.json ----
type vcpkg.json
echo.
echo ---- cmake --preset default ----
cmake --preset default || exit /b 4
echo ---- cmake --build --preset default ----
cmake --build --preset default || exit /b 5
"@
$buildDir = Join-Path $root 'build'
if (-not $ShotsOnly) {
$batPath = Join-Path $env:TEMP 'sfml_lab_build.bat'
Set-Content -Path $batPath -Value $bat -Encoding ASCII
# stderr is merged inside cmd (2>&1), so PowerShell does not turn CMake errors into exceptions
$ErrorActionPreference = 'Continue'
& cmd.exe /c "`"$batPath`" 2>&1" | ForEach-Object { Log "  $_" }
$buildCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($buildCode -ne 0) { Log "BUILD FAILED (code $buildCode)"; throw 'Build failed' }
}

$exe = Get-ChildItem $buildDir -Recurse -Filter 'SFML_Lab.exe' |
       Where-Object { $_.FullName -notmatch 'vcpkg' } |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw 'SFML_Lab.exe not found after build' }
Log "BUILD OK: $($exe.FullName)"
if ($BuildOnly) { return }

# ------------------------------------------------------------------ 2. experiment
if (-not $ShotsOnly) {
Log 'Experiment: SFML_Lab --experiment'
$csv = Join-Path $docs 'experiment_results.csv'
Push-Location $exe.DirectoryName
$out = & $exe.FullName --experiment $csv
Pop-Location
$out | Set-Content -Path (Join-Path $docs 'experiment_console.txt') -Encoding UTF8
Log "  $($out[-1])"
}
if ($NoShots) { return }

# ------------------------------------------------------------------ 3. screenshots
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Win {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool altTab);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool attach);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out RECT r, int size);
}
"@
[Win]::SetProcessDPIAware() | Out-Null

function Get-Rect([IntPtr]$h) {
    $r = New-Object Win+RECT
    [Win]::DwmGetWindowAttribute($h, 9, [ref]$r, 16) | Out-Null   # DWMWA_EXTENDED_FRAME_BOUNDS (no shadow)
    return $r
}

function Foreground-Pid {
    [uint32]$fgPid = 0
    [Win]::GetWindowThreadProcessId([Win]::GetForegroundWindow(), [ref]$fgPid) | Out-Null
    return $fgPid
}

function Center-Window([IntPtr]$h) {
    $r = Get-Rect $h
    $wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    $x = [Math]::Max($wa.Left, $wa.Left + [int](($wa.Width - ($r.Right - $r.Left)) / 2))
    $y = [Math]::Max($wa.Top, $wa.Top + [int](($wa.Height - ($r.Bottom - $r.Top)) / 2))
    [Win]::SetWindowPos($h, [IntPtr]::Zero, $x, $y, 0, 0, 0x0001 -bor 0x0004) | Out-Null   # NOSIZE | NOZORDER
    Start-Sleep -Milliseconds 300
}

function Ensure-Front($app) {
    for ($i = 0; $i -lt 6; $i++) {
        if ((Foreground-Pid) -eq $app.P.Id) { return }
        [Win]::ShowWindow($app.H, 9) | Out-Null                    # SW_RESTORE
        # attach to the input queue of the current foreground window, then activate ours
        [uint32]$tmp = 0
        $fgThread = [Win]::GetWindowThreadProcessId([Win]::GetForegroundWindow(), [ref]$tmp)
        $me = [Win]::GetCurrentThreadId()
        [Win]::AttachThreadInput($me, $fgThread, $true) | Out-Null
        [Win]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)            # Alt tap
        [Win]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
        [Win]::BringWindowToTop($app.H) | Out-Null
        [Win]::SetForegroundWindow($app.H) | Out-Null
        [Win]::AttachThreadInput($me, $fgThread, $false) | Out-Null
        if ($i -ge 2) { [Win]::SwitchToThisWindow($app.H, $true) }
        Start-Sleep -Milliseconds 600
        if ($i -ge 3 -and (Foreground-Pid) -ne $app.P.Id) {
            # last resort: a plain click on the program's own title bar activates it
            $r = Get-Rect $app.H
            [Win]::SetCursorPos($r.Left + 300, $r.Top + 14) | Out-Null
            [Win]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
            [Win]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 500
            $wa1 = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
            [Win]::SetCursorPos($wa1.Right - 3, $wa1.Top + 3) | Out-Null
        }
    }
    if ((Foreground-Pid) -ne $app.P.Id) {
        [uint32]$fg = Foreground-Pid
        $name = (Get-Process -Id $fg -ErrorAction SilentlyContinue).ProcessName
        throw "program window is not in the foreground (foreground: $name, pid $fg) - stopped so input does not go elsewhere"
    }
}

# Start SFML_Lab with arguments in the current console and find its SFML window
function Start-Lab([string]$arguments) {
    $p = Start-Process -FilePath $exe.FullName -ArgumentList $arguments -WorkingDirectory $exe.DirectoryName -NoNewWindow -PassThru
    for ($i = 0; $i -lt 80; $i++) {
        $h = [Win]::FindWindow('SFML_Window', [NullString]::Value)
        if ($h -ne [IntPtr]::Zero) {
            [uint32]$wpid = 0
            [Win]::GetWindowThreadProcessId($h, [ref]$wpid) | Out-Null
            if ($wpid -eq $p.Id) {
                Start-Sleep -Milliseconds 700
                $app = @{ P = $p; H = $h }
                Center-Window $h
                try { Ensure-Front $app } catch { Stop-Lab $app; throw }
                return $app
            }
        }
        Start-Sleep -Milliseconds 250
    }
    throw "SFML window did not appear ($arguments)"
}

function Stop-Lab($app) {
    if (-not $app.P.HasExited) {
        [Win]::PostMessage($app.H, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_CLOSE
        $app.P.WaitForExit(3000) | Out-Null
    }
    if (-not $app.P.HasExited) { $app.P.Kill() }
    Start-Sleep -Milliseconds 400
}

# Keys go only to our program (never to VS Code or other windows)
function K($app, [string]$keys, [int]$ms = 250) {
    if ((Foreground-Pid) -ne $app.P.Id) { throw "focus lost before keys '$keys' - stopped" }
    [System.Windows.Forms.SendKeys]::SendWait($keys)
    Start-Sleep -Milliseconds $ms
}

function Move-To($app, [int]$x, [int]$y) {
    $pt = New-Object Win+POINT
    $pt.X = $x; $pt.Y = $y
    [Win]::ClientToScreen($app.H, [ref]$pt) | Out-Null
    [Win]::SetCursorPos($pt.X, $pt.Y) | Out-Null
    [Win]::mouse_event(0x0001, 0, 0, 0, [UIntPtr]::Zero)      # MOUSEEVENTF_MOVE (0,0) -> WM_MOUSEMOVE
    Start-Sleep -Milliseconds 40
}

function Drag($app, [int]$x1, [int]$y1, [int]$x2, [int]$y2) {
    if ((Foreground-Pid) -ne $app.P.Id) { throw 'focus lost before drag - stopped' }
    Move-To $app $x1 $y1; Start-Sleep -Milliseconds 150
    [Win]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)      # LEFTDOWN
    for ($i = 1; $i -le 10; $i++) {
        Move-To $app ([int]($x1 + ($x2 - $x1) * $i / 10)) ([int]($y1 + ($y2 - $y1) * $i / 10))
    }
    [Win]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)      # LEFTUP
    Start-Sleep -Milliseconds 250
}

function Right-Click($app, [int]$x, [int]$y) {
    if ((Foreground-Pid) -ne $app.P.Id) { throw 'focus lost before right click - stopped' }
    Move-To $app $x $y; Start-Sleep -Milliseconds 100
    [Win]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero)      # RIGHTDOWN
    [Win]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero)      # RIGHTUP
    Start-Sleep -Milliseconds 250
}

function Shot($app, [string]$name) {
    Start-Sleep -Milliseconds 500
    $r = Get-Rect $app.H
    $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $bmp.Save((Join-Path $shots "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Log "  screenshot $name.png ($w x $hh)"
}

# Run with arguments, wait, take one screenshot, close
function Scenario([string]$name, [string]$arguments, [int]$waitMs) {
    Log "Scenario $name : $arguments"
    $app = Start-Lab $arguments
    try {
        Start-Sleep -Milliseconds $waitMs
        Ensure-Front $app
        Shot $app $name
    } finally { Stop-Lab $app }
}

# close copies of the program left from an interrupted run
Get-Process SFML_Lab -ErrorAction SilentlyContinue | ForEach-Object {
    $_.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 800
    if (-not $_.HasExited) { $_.Kill() }
}
$cursor = New-Object Win+POINT
[Win]::GetCursorPos([ref]$cursor) | Out-Null
# Move the cursor to the top-right corner so no tooltip (Explorer, taskbar) covers the program window
$wa0 = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
[Win]::SetCursorPos($wa0.Right - 3, $wa0.Top + 3) | Out-Null
Start-Sleep -Milliseconds 800

try {
# Layout numbers in --layout: 1 none, 2 chess, 3 zigzag, 4 funnel
Scenario 'w01_ready'         '--layout 2'                                         1200
Scenario 'w02_chess_pause'   '--layout 2 --angle 25 --mu 0.05 --start --pause-at 3' 4500
Scenario 'w03_chess_finish'  '--layout 2 --angle 25 --mu 0.05 --start'             8500
Scenario 'w04_none_finish'   '--layout 1 --angle 25 --mu 0.10 --start'             3500
Scenario 'w05_zigzag_finish' '--layout 3 --angle 25 --mu 0.05 --start'            14500
Scenario 'w06_zigzag_stop'   '--layout 3 --angle 25 --mu 0.20 --start'             6000
Scenario 'w07_funnel_finish' '--layout 4 --angle 25 --mu 0.10 --start'             7000
Scenario 'w08_no_motion'     '--layout 1 --angle 25 --mu 0.50 --start'             3000

# Keyboard: change angle and friction, three runs -> results table
Log 'Scenario w09: keyboard'
$app = Start-Lab '--layout 2 --angle 25 --mu 0.05'
try {
    1..5 | ForEach-Object { K $app '{UP}' 120 }        # alpha 25 -> 30
    1..5 | ForEach-Object { K $app '{RIGHT}' 120 }     # mu 0.05 -> 0.10
    Shot $app 'w09a_keys_params'
    K $app ' ' 8000                                    # run 1 (stops on a peg)
    1..10 | ForEach-Object { K $app '{RIGHT}' 100 }    # mu 0.10 -> 0.20
    K $app 'r' 300
    K $app ' ' 10500                                   # run 2
    K $app '4' 300                                     # funnel layout
    K $app ' ' 8000                                    # run 3
    Shot $app 'w09b_keys_results'
} finally { Stop-Lab $app }

# Mouse: move a wall, add pegs, run
Log 'Scenario w10: mouse'
$app = Start-Lab '--layout 3 --angle 25 --mu 0.05 --pause-at 6'
try {
    Drag $app 269 587 369 587          # third wall 1 m to the right
    Right-Click $app 554 342           # three new pegs
    Right-Click $app 250 300
    Right-Click $app 150 470
    Move-To $app 1000 760
    Shot $app 'w10a_mouse_layout'
    K $app ' ' 8000
    Shot $app 'w10b_mouse_run'
} finally { Stop-Lab $app }

} catch {
    Log "SCREENSHOTS FAILED: $($_.Exception.Message)"
    [Win]::SetCursorPos($cursor.X, $cursor.Y) | Out-Null
    throw
}
[Win]::SetCursorPos($cursor.X, $cursor.Y) | Out-Null
Log "DONE $(Get-Date -Format s). Screenshots: $shots"
