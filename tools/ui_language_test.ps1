<#
.SYNOPSIS
    Drives the real spectra3d.exe window through the language dropdown and checks
    what the UI actually does, using injected Windows input.

.DESCRIPTION
    Choosing a language used to need no second click: the press that opened the
    dropdown was also delivered to a row inside it, so the language changed to
    whatever sat under the pointer. A screenshot cannot catch that - it shows the
    dropdown open, which looks correct. Only settings.ini before and after can
    tell whether opening cost one click or two.

    So this checks, in order:
      1. opening the dropdown does NOT change the language
      2. hovering a row does NOT change it
      3. scrolling does NOT change it
      4. clicking a row DOES change it
      5. reopening afterwards changes nothing again

    It restores the original language at the end, so a run leaves no trace.

    The launcher lays out in client pixels, and every click is computed from the
    constants in Launcher.cpp rather than from a fraction of the window: a guess
    that lands on one machine and misses a 28-pixel gear on the next is not a
    test, it is a coincidence.

.PARAMETER Shots
    Directory for screenshots. Purely for evidence after a failure.

.PARAMETER KeepLanguage
    Do not restore the original language, e.g. -KeepLanguage uk.

.EXAMPLE
    .\tools\ui_language_test.ps1
    .\tools\ui_language_test.ps1 -Shots out\shots
#>
[CmdletBinding()]
param(
    [string] $Shots,
    [string] $KeepLanguage
)

$ErrorActionPreference = 'Stop'

Add-Type -Namespace S3DUi -Name Win32 -MemberDefinition @'
    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr FindWindow(string cls, string title);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr param);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowTextW(IntPtr hWnd, System.Text.StringBuilder s, int max);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);

    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr param);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int cmd);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y,
                                          int cx, int cy, uint flags);

    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT p);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, ref RECT r);

    [DllImport("user32.dll")]
    public static extern uint SendInput(uint n, INPUT[] inputs, int size);

    [DllImport("user32.dll")]
    public static extern int GetSystemMetrics(int index);

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X; public int Y; }

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT {
        public int dx, dy;
        public uint mouseData, dwFlags, time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Explicit)]
    public struct INPUTUNION {
        [FieldOffset(0)] public MOUSEINPUT mi;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT {
        public uint type;
        public INPUTUNION u;
    }
'@

# --- layout constants, copied from Launcher.cpp -----------------------------
# A click target that drifts when the engine's row pitch changes would turn this
# into a test that silently stops testing, so the numbers are named after the
# C++ constants they mirror.
$script:TopBar    = 46    # kTopBarHeight
$script:Nav       = 28    # kNavHeight
$script:Sidebar   = 212   # kSidebarWidth
$script:Row       = 44    # kRowHeight
$script:Gap       = 6
$script:Field     = 22    # kFieldHeight
$script:ListRow   = $Row + 2
$script:TextLine  = 15    # kTextHeight / theme::textLineHeight
$script:Section   = 22    # kSectionHeader
$script:Content   = 8     # kContentInset
# The select row is taller and lower than the plain fields above it: the field is
# drawn kTextHeight + 4 down from the row and is kFieldHeight + 6 tall. A test that
# assumes the plain 22 pixel field lands a pixel above the widget it means to press,
# and then every check passes for the wrong reason - the dropdown never opened.
$script:SelectDown = $TextLine + 4
$script:SelectTall = $Field + 6

$SW_RESTORE   = 9
$NOZORDER     = 0x0004
$NOSIZE       = 0x0001
$NOACTIVATE   = 0x0010
$TOPMOST      = [IntPtr](-1)
$NOTOPMOST    = [IntPtr](-2)

$MOUSEEVENTF_MOVE          = 0x0001
$MOUSEEVENTF_ABSOLUTE      = 0x8000
$MOUSEEVENTF_LEFTDOWN      = 0x0002
$MOUSEEVENTF_LEFTUP        = 0x0004
$MOUSEEVENTF_WHEEL         = 0x0800
$INPUT_MOUSE               = 0
$WHEEL_DELTA               = 120

$root    = Split-Path -Parent $PSScriptRoot
$exe     = Join-Path $root 'build\spectra3d.exe'
$settings= Join-Path $root 'build\settings.ini'

function Move-Cursor([int] $x, [int] $y) {
    $sw = [S3DUi.Win32]::GetSystemMetrics(0); if ($sw -le 0) { $sw = 1 }
    $sh = [S3DUi.Win32]::GetSystemMetrics(1); if ($sh -le 0) { $sh = 1 }
    $nx = [int]($x * 65535 / ($sw - 1))
    $ny = [int]($y * 65535 / ($sh - 1))
    $i = New-Object S3DUi.Win32+INPUT
    $i.type = $INPUT_MOUSE
    $i.u.mi.dx = $nx; $i.u.mi.dy = $ny
    $i.u.mi.dwFlags = $MOUSEEVENTF_MOVE -bor $MOUSEEVENTF_ABSOLUTE
    [void][S3DUi.Win32]::SendInput(1, @($i), [Runtime.InteropServices.Marshal]::SizeOf($i))
}

function Send-Click {
    param([int] $X, [int] $Y, [double] $Settle = 0.35)
    Move-Cursor $X $Y
    Start-Sleep -Milliseconds 60
    foreach ($f in @($MOUSEEVENTF_LEFTDOWN, $MOUSEEVENTF_LEFTUP)) {
        $i = New-Object S3DUi.Win32+INPUT
        $i.type = $INPUT_MOUSE
        $i.u.mi.dwFlags = $f
        [void][S3DUi.Win32]::SendInput(1, @($i), [Runtime.InteropServices.Marshal]::SizeOf($i))
        Start-Sleep -Milliseconds 40
    }
    Start-Sleep -Milliseconds ([int]($Settle * 1000))
}

function Send-Wheel {
    param([int] $X, [int] $Y, [int] $Notches)
    Move-Cursor $X $Y
    Start-Sleep -Milliseconds 60
    $dir = if ($Notches -lt 0) { -1 } else { 1 }
    # A wheel down is a negative delta, and mouseData is a DWORD: the bits have to
    # be built by hand. [uint32](-120) does not wrap in PowerShell, it throws, and
    # the throw lands in the middle of the scroll check with no test result.
    $delta = $dir * $WHEEL_DELTA
    $bits = if ($delta -lt 0) { [uint32](4294967296 + $delta) } else { [uint32] $delta }
    for ($n = 0; $n -lt [Math]::Abs($Notches); $n++) {
        $i = New-Object S3DUi.Win32+INPUT
        $i.type = $INPUT_MOUSE
        $i.u.mi.dwFlags = $MOUSEEVENTF_WHEEL
        $i.u.mi.mouseData = $bits
        [void][S3DUi.Win32]::SendInput(1, @($i), [Runtime.InteropServices.Marshal]::SizeOf($i))
        Start-Sleep -Milliseconds 60
    }
    Start-Sleep -Milliseconds 200
}

function Save-Shot([string] $name) {
    if (-not $Shots) { return }
    try {
        New-Item -ItemType Directory -Force -Path $Shots | Out-Null
        Add-Type -AssemblyName System.Windows.Forms, System.Drawing
        $b  = [System.Windows.Forms.SystemInformation]::VirtualScreen
        $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
        $g   = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size)
        $bmp.Save((Join-Path $Shots $name), [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose()
        Write-Host "    shot: $(Join-Path $Shots $name)"
    } catch {
        Write-Host "    (no screenshot: $($_.Exception.Message))"
    }
}

function Find-AppWindow([int] $ProcId) {
    # FindWindow is no use here: it matches on window class as well as title, and
    # GLFW registers its own class, so it returns 0 for a window that is plainly
    # there. Enumerating and matching the title against OUR process id is exact -
    # and the pid check matters, because the title "Spectra3D" is not unique and a
    # stale instance from an earlier run would otherwise answer for this one.
    $found = [IntPtr]::Zero
    $cb = [S3DUi.Win32+EnumWindowsProc] {
        param([IntPtr] $h, [IntPtr] $p)
        # $pid is a PowerShell automatic variable; assigning to it throws.
        $owner = [uint32] 0
        [void][S3DUi.Win32]::GetWindowThreadProcessId($h, [ref] $owner)
        if ($owner -ne $ProcId) { return $true }
        if (-not [S3DUi.Win32]::IsWindowVisible($h)) { return $true }
        $sb = New-Object System.Text.StringBuilder 256
        [void][S3DUi.Win32]::GetWindowTextW($h, $sb, 256)
        if ($sb.ToString() -eq 'Spectra3D') { $script:found = $h; return $false }
        return $true
    }
    $script:found = [IntPtr]::Zero
    [void][S3DUi.Win32]::EnumWindows($cb, [IntPtr]::Zero)
    return $script:found
}

function Get-Language {
    if (-not (Test-Path $settings)) { return $null }
    foreach ($line in [IO.File]::ReadAllLines($settings)) {
        $t = $line.Trim()
        if ($t.StartsWith('language=')) { return $t.Substring(9).Trim() }
    }
    return $null
}

function Set-Language([string] $value) {
    $lines = @()
    if (Test-Path $settings) { $lines = [IO.File]::ReadAllLines($settings) }
    $out = @(); $seen = $false
    foreach ($l in $lines) {
        if ($l.Trim().StartsWith('language=')) { $out += "language=$value"; $seen = $true }
        else { $out += $l }
    }
    if (-not $seen) { $out += "language=$value" }
    [IO.File]::WriteAllLines($settings, $out)
}

# --- run --------------------------------------------------------------------

if (-not (Test-Path $exe)) {
    Write-Host "FAIL: $exe not found - build first" -ForegroundColor Red
    exit 2
}

$before = Get-Language
Write-Host "language before: $before"

Write-Host "starting $exe"
$proc = Start-Process -FilePath $exe -WorkingDirectory (Join-Path $root 'build') -PassThru
$failed = $false

try {
    $hwnd = [IntPtr]::Zero
    for ($i = 0; $i -lt 100; $i++) {
        Start-Sleep -Milliseconds 100
        if ($proc.HasExited) {
            Write-Host "FAIL: the app exited immediately (code $($proc.ExitCode))" -ForegroundColor Red
            exit 2
        }
        $hwnd = Find-AppWindow $proc.Id
        if ($hwnd -ne [IntPtr]::Zero) { break }
    }
    if ($hwnd -eq [IntPtr]::Zero) {
        Write-Host 'FAIL: window "Spectra3D" never appeared' -ForegroundColor Red
        exit 2
    }

    # Park the window at the screen origin and pin it on top for the length of
    # the run. Two separate problems, one fix: a 1280x720 client plus a frame
    # does not fit above a 768-pixel taskbar, so the sidebar's bottom row - the
    # gear - lands underneath it; and without being foreground the clicks go to
    # whatever else is on screen. Clicking into the taskbar instead of the app
    # looks exactly like "the test does nothing".
    [void][S3DUi.Win32]::ShowWindow($hwnd, $SW_RESTORE)
    [void][S3DUi.Win32]::SetWindowPos($hwnd, $TOPMOST, 0, 0, 0, 0,
        $NOZORDER -bor $NOSIZE -bor $NOACTIVATE)
    Start-Sleep -Milliseconds 250
    [void][S3DUi.Win32]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 400

    $r = New-Object S3DUi.Win32+RECT
    [void][S3DUi.Win32]::GetClientRect($hwnd, [ref] $r)
    $cw = $r.Right - $r.Left
    $ch = $r.Bottom - $r.Top
    $o = New-Object S3DUi.Win32+POINT
    [void][S3DUi.Win32]::ClientToScreen($hwnd, [ref] $o)
    Write-Host "client ${cw}x${ch} at $($o.X),$($o.Y)"

    # Client pixels -> screen pixels. Kept as two plain ints and combined at the
    # call site: a helper returning an array gets unrolled by PowerShell the
    # moment it holds one element, and the failure then surfaces three lines
    # later as an arithmetic error with no useful location.
    $originX = [int] $o.X
    $originY = [int] $o.Y

    Save-Shot '01-launched.png'

    # The gear sits at the right-hand end of the sidebar's bottom row:
    # side.x + side.width - gearSize - 6, with gearSize == kNavHeight.
    $gearX = $Sidebar - $Nav - 6 + [int]($Nav / 2)
    $gearY = $ch - $Nav - 10 + [int]($Nav / 2)
    Write-Host "gear at client $gearX,$gearY"
    $sx = $originX + $gearX
    $sy = $originY + $gearY
    Send-Click $sx $sy 0.5
    Save-Shot '02-settings.png'

    # Settings body: the section header and its inset, then the three rows above the
    # language select - caption + root field, Apply/Reset - then the select's own
    # caption and the middle of its field.
    $bodyX = $Sidebar + 24
    $bodyY = $TopBar + $Section + $Content
    $langY = $bodyY + ($TextLine + 2) + $Field + 6 + $Row + 6 + $SelectDown + [int]($SelectTall / 2)
    Write-Host "language field at client $($bodyX + 40),$langY"
    $fx = $originX + $bodyX + 40
    $fy = $originY + $langY
    Send-Click $fx $fy 0.5
    Save-Shot '03-dropdown-open.png'

    $afterOpen = Get-Language
    if ($afterOpen -ne $before) {
        Write-Host "FAIL: opening the dropdown changed the language ($before -> $afterOpen)" -ForegroundColor Red
        $failed = $true
    } else {
        Write-Host 'ok: opening the dropdown left the language alone' -ForegroundColor Green
    }

    # A row inside the open list, the third one down. The list is drawn over its own
    # field when it is taller than the panel below it - which at 720p it always is,
    # with twenty-three languages - so the list's top edge is the field's top edge.
    $listTop = $langY - [int]($SelectTall / 2)
    $rowY = $listTop + $ListRow * 2 + [int]($ListRow / 2)
    $rx = $originX + $bodyX + 40
    $ry = $originY + $rowY
    Write-Host "list row at client $rx,$rowY"

    Move-Cursor $rx $ry
    Start-Sleep -Milliseconds 500
    Save-Shot '04-hovering.png'
    if ((Get-Language) -ne $before) {
        Write-Host "FAIL: hovering a row changed the language ($before -> $(Get-Language))" -ForegroundColor Red
        $failed = $true
    } else {
        Write-Host 'ok: hovering a row left the language alone' -ForegroundColor Green
    }

    # Twenty-three languages do not fit, so the tail is only reachable by scroll.
    Send-Wheel $rx $ry -3
    Save-Shot '05-scrolled.png'
    if ((Get-Language) -ne $before) {
        Write-Host "FAIL: scrolling changed the language ($before -> $(Get-Language))" -ForegroundColor Red
        $failed = $true
    } else {
        Write-Host 'ok: scrolling left the language alone' -ForegroundColor Green
    }

    Send-Click $rx $ry 0.7
    Save-Shot '06-clicked.png'

    $afterClick = Get-Language
    if ($afterClick -eq $before) {
        Write-Host "FAIL: clicking a row did not change the language (still $afterClick)" -ForegroundColor Red
        $failed = $true
    } else {
        Write-Host "ok: clicking a row changed the language ($before -> $afterClick)" -ForegroundColor Green
    }

    # Reopening must not choose anything by itself - the regression that started
    # all of this.
    Send-Click $sx $sy 0.4
    Send-Click $fx $fy 0.5
    Save-Shot '07-reopened.png'
    $afterReopen = Get-Language
    if ($afterReopen -ne $afterClick) {
        Write-Host "FAIL: reopening changed the language again ($afterClick -> $afterReopen)" -ForegroundColor Red
        $failed = $true
    } else {
        Write-Host 'ok: reopening the dropdown changed nothing' -ForegroundColor Green
    }
}
finally {
    if ($proc -and -not $proc.HasExited) {
        $proc.CloseMainWindow() | Out-Null
        if (-not $proc.WaitForExit(5000)) { $proc.Kill() }
    }
    if (-not $KeepLanguage -and $null -ne $before) {
        Set-Language $before
        Write-Host "restored language=$before"
    }
}

if ($failed) { exit 1 }
Write-Host ''
Write-Host 'PASS' -ForegroundColor Green
exit 0
