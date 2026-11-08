<#
.SYNOPSIS
  Run BO1Zombies windowless (bo1_headless) with caller-supplied + commands, wait, and report.

.DESCRIPTION
  Launches build\Release\BO1Zombies.exe with the working directory build\Release, fs_b/fs_h pinned there,
  net_ip 127.0.0.1, logfile 2 and +set bo1_headless 1, followed by -Commands. The exe itself guarantees no
  windows in that mode (see src/win32/win_main.cpp); this script adds a second layer and checks it:
    - the process starts on a private desktop (WinSta0\bo1_headless) with SW_HIDE, and inherits an error
      mode without loader / crash dialogs, so even a window the code failed to prevent is never on the user's
      desktop;
    - every 200 ms it lists visible top-level windows of the process on the user's desktop and on the private
      one, and new WerFault.exe processes; any window -> the process is killed and the run reported;
    - a timeout kills the process.
  Then it prints the exit code, the decisive log lines (Com_ERROR, BO1_HEADLESS, errors), the log tail,
  Application Error events (1000) since launch with the fault offset resolved by llvm-symbolizer, and checks
  that nothing under the Steam install was written.
  Exit codes: 0 quit normally; 12 the game crashed, asserted or hit a fatal error; 20 a window appeared;
  21 timed out (10 refused, above).

  Refuses to start (exit 10) if BlackOps or BlackOpsMP is running, if this run folder's BO1Zombies already runs, or
  if all -MaxSlots headless slots are taken (default 6; -WaitSec N keeps retrying for N s first). Each slot has its own
  private desktop (bo1_headless / bo1_headless1..) and its own net_port (29060 + 100 * slot; 28960 stays free for
  old copies of this script). Slot mutexes are shared with older copies (they take slots 0..3 / 0..1 only). The game runs
  at -Priority BelowNormal (default) so the desktop user keeps the CPU.

  -RunDir <dir> (L7): run the build's exe from a run folder instead of build\Release, so several runs of ONE build can
  go at once (one run per run folder). Relative paths are under the worktree root (e.g. build\run\gates). The folder is
  made / refreshed on each run like the main session's build\Play: BO1Zombies.exe, .pdb and the root .dll/.txt/.bmp files
  copied from build\Release when missing or different (a rebuild reaches it), zone\, mods\ and main\video junctioned
  (the exe loads zone\ relative to its own folder, videos relative to its cwd), own main\ (logs, emergency / thread
  logs, screenshots) and players\ (the build's players\*.cfg are copied in fresh each run). fs_b stays build\Release
  (iwds, main\*.cfg), fs_h and the cwd are the run folder. Without -RunDir everything is as before (build\Release).

  -Client (+set bo1_headless_client 1): a listen server with a local client instead of a dedicated server, so the
  renderer creates the game window - on the private desktop only. The exe forces it windowed and keeps it off the
  user's display, audio and input devices (see src/win32/win_main.cpp). The window check changes like this:
    - a visible window of class 'CoDBlackOps' (the game window, created only by R_CreateGameWindow) is allowed ON
      THE PRIVATE DESKTOP ONLY and reported once;
    - any other visible window of the process (message box, console, dialog) on the private desktop -> kill;
    - any visible window of the process on any other desktop (the user's, or any desktop of WinSta0) -> kill;
    - the input desktop (what the monitor shows) must never be the private one -> kill, and switch back to Default.
  Without -Client every visible window anywhere kills the run, as before. Screenshots: pass
  "+screenshotJpeg name" through -Commands (or -ShotsAtMs); they read the game's back buffer (never the screen) and
  land in build\Release\main\screenshots\ (the run folder's main\screenshots\ with -RunDir). -ShotsAtMs "20000 40000" takes them at those ms after init
  (+set bo1_shots, which also logs the client state per shot).

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\headless.ps1 -Commands "+devmap mp_mountain" -AutoQuitMs 10000
.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\headless.ps1 -Commands "+loadzone zombie_pentagon +quit"
.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\headless.ps1 -Zombies -Commands "+set QuitOnError 1 +devmap zombie_pentagon" -AutoQuitMs 20000
#>
param(
    # + commands for the exe, e.g. "+set developer 1 +devmap mp_mountain"
    [Parameter(Mandatory = $true)][string]$Commands,
    # kill the process after this many seconds
    [int]$TimeoutSec = 240,
    # +set bo1_autoquit <ms>: the exe quits this long after its init (map loads from + commands included)
    [int]$AutoQuitMs = 0,
    # +set bo1_zombies 1: boot with the SP code zones
    [switch]$Zombies,
    # log lines to print from the end
    [int]$TailLines = 40,
    # copy main\console_mp.log here after the run
    [string]$SaveLog = '',
    # listen server + local client whose window lives on the private desktop (see .DESCRIPTION)
    [switch]$Client,
    # -Client: window / back buffer size and frame cap (the GPU is shared with the user's desktop). -Client also passes
    # +set r_vsync 0 unless -Commands sets r_vsync: the hidden desktop's vsync pins the client at ~30 fps (q1 chunk 13),
    # so without it -ClientMaxFps above 30 was not the real rate.
    [string]$ClientMode = '1280x720',
    [int]$ClientMaxFps = 30,
    # -Client: back buffer screenshots at these ms after init, e.g. "20000 40000"
    [string]$ShotsAtMs = '',
    # -Client, performance runs only: keep multisampling at this sample count (2..16; the player's r_aaSamples). The swap
    # chain is then DISCARD, so it cannot be combined with -ShotsAtMs / screenshots.
    [int]$ClientAA = 1,
    # -Client, performance runs only: open the sound device and run the sound system as in real play, with the mastering
    # voice at volume 0 (snd_driver_xaudio2.cpp) so nothing is heard
    [switch]$ClientSound,
    # hang evidence: +set bo1_threaddump <ms> - all threads' stacks this long after PROCESS START (the map load counts;
    # bo1_autoquit counts after init) and 10 s later (symbolized)
    [int]$ThreadDumpMs = 0,
    [int]$ThreadDumpFrames = 14,
    # run from this run folder (see .DESCRIPTION); '' = build\Release
    [string]$RunDir = '',
    # machine-wide cap on headless runs (slots 0..MaxSlots-1)
    [ValidateRange(1, 16)][int]$MaxSlots = 6,
    # when this run folder or every slot is busy, retry for up to this many seconds before refusing (exit 10)
    [int]$WaitSec = 0,
    # CPU priority class of the game process
    [ValidateSet('BelowNormal', 'Normal', 'Idle')][string]$Priority = 'BelowNormal'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$R = Join-Path $repo 'build\Release'   # the build: fs_b, and the source of a run folder
$H = $R                                 # where the exe runs: cwd, fs_h, logs
if ($RunDir) {
    $H = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($RunDir)) { $RunDir } else { Join-Path $repo $RunDir })).TrimEnd('\')
    if ($H -ieq $R) { $H = $R }
    # build\Play is a player's own folder: never a headless run folder
    if ((Split-Path -Leaf $H) -ieq 'Play') { Write-Output "REFUSED: $H is a play folder, not a run folder"; exit 11 }
}
$exe = Join-Path $H 'BO1Zombies.exe'
# The filesystem opens console_mp.log under fs_game. Match the last command-line assignment,
# including a quoted mod path; stock runs keep main. This also drives freshness checks and -SaveLog.
$logGame = 'main'
$gameArgs = [regex]::Matches($Commands, '(?i)(?:^|\s)\+set\s+fs_game\s+(?:"([^"]*)"|([^\s+]+))')
if ($gameArgs.Count) {
    $gameArg = $gameArgs[$gameArgs.Count - 1]
    $value = if ($gameArg.Groups[1].Success) { $gameArg.Groups[1].Value } else { $gameArg.Groups[2].Value }
    if ($value) { $logGame = $value }
}
$log = Join-Path (Join-Path $H $logGame) 'console_mp.log'
# the original install (setup.ps1 records it in gamedir.txt): the game reads main\*.iwd from there (fs_cdpath) and zone\
# through a junction. READ ONLY: only listed
$gameDirFile = Join-Path $repo 'gamedir.txt'
if (-not (Test-Path $gameDirFile)) { throw 'gamedir.txt is missing: run setup.ps1 first' }
$steamGame = (Get-Content $gameDirFile -TotalCount 1).Trim()
$symbolizer = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x86\llvm-symbolizer.exe'

if (-not (Test-Path (Join-Path $R 'BO1Zombies.exe'))) { throw "not built: $(Join-Path $R 'BO1Zombies.exe')" }
# One run per run folder (its main\ log and players\ config are shared) and at most -MaxSlots runs machine-wide. Named
# mutexes are held by this PowerShell process until it exits (the OS releases them if it dies; abandoned = acquired).
function Get-BO1Mutex([string]$name) {
    $m = New-Object System.Threading.Mutex($false, $name)
    try { if ($m.WaitOne(0)) { return $m } } catch [System.Threading.AbandonedMutexException] { return $m }
    $m.Dispose(); return $null
}
$waitUntil = (Get-Date).AddSeconds($WaitSec)
$slot = -1; $slotMutex = $null; $dirMutex = $null
while ($true) {
    $running = Get-Process -Name BlackOps, BlackOpsMP -ErrorAction SilentlyContinue
    if ($running) {
        $running | ForEach-Object { Write-Output ("REFUSED: {0} (pid {1}) is running" -f $_.ProcessName, $_.Id) }
        exit 10
    }
    $busy = ''
    $dirMutex = Get-BO1Mutex ('Local\bo1_headless_dir_' + (($H.ToLowerInvariant()) -replace '[\\/:]', '_'))
    if (-not $dirMutex) { $busy = "a headless run from $H is already running" }
    else {
        foreach ($i in 0..($MaxSlots - 1)) { $slotMutex = Get-BO1Mutex "Local\bo1_headless_slot$i"; if ($slotMutex) { $slot = $i; break } }
        if ($slot -ge 0) { break }
        $dirMutex.ReleaseMutex(); $dirMutex.Dispose(); $dirMutex = $null
        $busy = "all $MaxSlots headless slots are taken"
    }
    if ((Get-Date) -ge $waitUntil) { Write-Output "REFUSED: $busy$(if ($WaitSec -gt 0) { " (waited $WaitSec s)" })"; exit 10 }
    Start-Sleep -Milliseconds (2000 + (Get-Random -Maximum 3000))
}
# a BO1Zombies from this folder started outside this script (or a leftover) would share the log
$same = @(Get-Process -Name BO1Zombies -ErrorAction SilentlyContinue | Where-Object { $_.Path -and ($_.Path -ieq $exe) })
if ($same.Count) { Write-Output ("REFUSED: {0} (pid {1}) is running" -f $exe, $same[0].Id); exit 10 }
if ($H -ne $R) {
    # make / refresh the run folder (the dir mutex is held: nothing runs from it)
    foreach ($d in 'main', 'players') { New-Item -ItemType Directory -Force (Join-Path $H $d) | Out-Null }
    foreach ($j in 'zone', 'mods', 'main\video') {
        $t = Join-Path $R $j; $l = Join-Path $H $j
        if ((Test-Path $t) -and -not (Test-Path $l)) { New-Item -ItemType Junction -Path $l -Target $t | Out-Null }
    }
    # the same root files a build\Release run sees (setup.ps1 put them there), the exe and pdb refreshed after a rebuild
    Get-ChildItem $R -File | Where-Object { $_.Name -in 'BO1Zombies.exe', 'BO1Zombies.pdb' -or $_.Extension -in '.dll', '.txt', '.bmp' } | ForEach-Object {
        $c = Join-Path $H $_.Name
        $old = Get-Item $c -ErrorAction SilentlyContinue
        if (-not $old -or $old.Length -ne $_.Length -or $old.LastWriteTimeUtc -ne $_.LastWriteTimeUtc) { Copy-Item $_.FullName $c -Force }
    }
    # the build's config, fresh each run (a run writes its own copy back on quit)
    Get-ChildItem (Join-Path $H 'players') -File -Filter '*.cfg' | Remove-Item -Force
    Get-ChildItem (Join-Path $R 'players') -File -Filter '*.cfg' -ErrorAction SilentlyContinue | Copy-Item -Destination (Join-Path $H 'players') -Force
}
if (-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) {
    Write-Output 'NOTE: the Steam client is not running; the game runs without Steam (no-steam fallbacks, notes/L6-report.md)'
}

if (-not ('BO1Headless' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class BO1Headless
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO
    {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2;
        public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
    delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
                                      IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern uint SetErrorMode(uint mode);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr h, out uint code);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr h, uint code);
    [StructLayout(LayoutKind.Sequential)]
    struct SECURITY_ATTRIBUTES { public int nLength; public IntPtr lpSecurityDescriptor; public bool bInheritHandle; }
    delegate bool EnumDesktopsProc(string name, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateDesktopW(string name, IntPtr device, IntPtr devmode, uint flags, uint access, ref SECURITY_ATTRIBUTES sa);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr OpenDesktopW(string name, uint flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern bool SwitchDesktop(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetProcessWindowStation();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool EnumDesktopsW(IntPtr winsta, EnumDesktopsProc cb, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern bool GetUserObjectInformationW(IntPtr h, int index, StringBuilder buf, int len, out int needed);
    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool ConvertStringSecurityDescriptorToSecurityDescriptorW(string sddl, uint rev, out IntPtr sd, IntPtr size);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr h);
    [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr desk, EnumWindowsProc cb, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);

    public static string DesktopName = "bo1_headless";   // slots 1..: bo1_headless1.. (set before Launch)
    public static uint PriorityClass = 0x4000;               // BELOW_NORMAL_PRIORITY_CLASS (set before Launch)
    static IntPtr desktop = IntPtr.Zero;
    static IntPtr process = IntPtr.Zero;
    public static int Pid;

    public static void Launch(string exe, string args, string cwd)
    {
        // The private desktop's DACL grants this user and SYSTEM everything except DESKTOP_SWITCHDESKTOP (0x100):
        // nobody, the game included, can make it the input desktop, so the monitor never shows it.
        string sid = System.Security.Principal.WindowsIdentity.GetCurrent().User.Value;
        IntPtr sd;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW("D:P(A;;0xF00FF;;;" + sid + ")(A;;0xF00FF;;;SY)", 1, out sd, IntPtr.Zero))
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "ConvertStringSecurityDescriptor");
        SECURITY_ATTRIBUTES sa = new SECURITY_ATTRIBUTES();
        sa.nLength = Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES));
        sa.lpSecurityDescriptor = sd;
        try { desktop = CreateDesktopW(DesktopName, IntPtr.Zero, IntPtr.Zero, 0, 0xF00FF /* DESKTOP_ALL_ACCESS minus SWITCHDESKTOP */, ref sa); }
        finally { LocalFree(sd); }
        if (desktop == IntPtr.Zero)
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "CreateDesktop");
        STARTUPINFO si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        si.lpDesktop = "WinSta0\\" + DesktopName;
        si.dwFlags = 1;          // STARTF_USESHOWWINDOW
        si.wShowWindow = 0;      // SW_HIDE
        StringBuilder cmd = new StringBuilder("\"" + exe + "\" " + args);
        PROCESS_INFORMATION pi;
        // inherited by the child: no loader error box (missing DLL) and no crash box before WinMain resets it
        uint old = SetErrorMode(0x8003); // SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX
        bool ok;
        try { ok = CreateProcessW(exe, cmd, IntPtr.Zero, IntPtr.Zero, false, PriorityClass, IntPtr.Zero, cwd, ref si, out pi); }
        finally { SetErrorMode(old); }
        if (!ok)
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "CreateProcess");
        CloseHandle(pi.hThread);
        process = pi.hProcess;
        Pid = pi.dwProcessId;
    }

    public static bool HasExited() { return WaitForSingleObject(process, 0) == 0; }
    public static long ExitCode() { uint c; GetExitCodeProcess(process, out c); return c; }
    public static void Kill() { TerminateProcess(process, 0xDEAD); WaitForSingleObject(process, 10000); }

    static string Describe(IntPtr h, string where)
    {
        StringBuilder t = new StringBuilder(256), c = new StringBuilder(256);
        GetWindowTextW(h, t, 256);
        GetClassNameW(h, c, 256);
        return where + " hwnd=0x" + h.ToString("X") + " class='" + c + "' title='" + t + "'";
    }

    // visible top-level windows of a process (the game, or a WerFault reporting on it): on the user's desktop
    // (EnumWindows = the caller's desktop), on every other desktop of this window station that can be opened
    // (named "DESKTOP '<name>'"), and on the private one ("hidden-desktop" - the only place -Client allows the
    // game window)
    public static string[] VisibleWindows(int ofPid)
    {
        List<string> found = new List<string>();
        uint pid = (uint)ofPid;
        string where = "USER-DESKTOP";
        EnumWindowsProc onWindow = delegate(IntPtr h, IntPtr lp)
        {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid && IsWindowVisible(h) && !found.Contains(Describe(h, where))) found.Add(Describe(h, where));
            return true;
        };
        EnumWindows(onWindow, IntPtr.Zero);
        List<string> names = new List<string>();
        EnumDesktopsProc onDesktop = delegate(string name, IntPtr lp) { names.Add(name); return true; };
        EnumDesktopsW(GetProcessWindowStation(), onDesktop, IntPtr.Zero);
        foreach (string name in names)
        {
            if (name == DesktopName) continue;
            IntPtr d = OpenDesktopW(name, 0, false, 0x41 /* DESKTOP_READOBJECTS | DESKTOP_ENUMERATE */);
            if (d == IntPtr.Zero) continue;   // Winlogon / Disconnect: not openable, and the game cannot reach them
            where = "DESKTOP '" + name + "'";
            EnumDesktopWindows(d, onWindow, IntPtr.Zero);
            CloseDesktop(d);
        }
        where = "hidden-desktop";
        if (desktop != IntPtr.Zero) EnumDesktopWindows(desktop, onWindow, IntPtr.Zero);
        GC.KeepAlive(onWindow); GC.KeepAlive(onDesktop);
        return found.ToArray();
    }

    // name of the input desktop (what the monitor shows); "" when it cannot be opened (e.g. the secure desktop)
    public static string InputDesktopName()
    {
        IntPtr d = OpenInputDesktop(0, false, 0x1 /* DESKTOP_READOBJECTS */);
        if (d == IntPtr.Zero) return "";
        StringBuilder b = new StringBuilder(256);
        int needed;
        GetUserObjectInformationW(d, 2 /* UOI_NAME */, b, 512, out needed);
        CloseDesktop(d);
        return b.ToString();
    }

    // last resort if the private desktop were ever the input desktop: show the user's desktop again
    public static bool SwitchToDefault()
    {
        IntPtr d = OpenDesktopW("Default", 0, false, 0x100 /* DESKTOP_SWITCHDESKTOP */);
        if (d == IntPtr.Zero) return false;
        bool ok = SwitchDesktop(d);
        CloseDesktop(d);
        return ok;
    }

    public static void Cleanup()
    {
        if (process != IntPtr.Zero) { CloseHandle(process); process = IntPtr.Zero; }
        if (desktop != IntPtr.Zero) { CloseDesktop(desktop); desktop = IntPtr.Zero; }
    }
}
'@
}

$argLine = "+set fs_b `"$R`" +set fs_h `"$H`" +set fs_cdpath `"$steamGame`" +set bo1_headless 1 +set net_ip 127.0.0.1 +set logfile 2"
if ($Zombies) { $argLine += ' +set bo1_zombies 1' }
if ($AutoQuitMs -gt 0) { $argLine += " +set bo1_autoquit $AutoQuitMs" }
if ($Client) {
    # the exe appends r_fullscreen 0 / r_aaSamples 1 itself (win_main.cpp); these size the window and cap the GPU load
    $argLine += " +set bo1_headless_client 1 +set r_fullscreen 0 +set r_customMode $ClientMode +set vid_xpos 0 +set vid_ypos 0 +set com_maxfps $ClientMaxFps"
    if ($Commands -notmatch '(?i)\br_vsync\b') { $argLine += ' +set r_vsync 0' }
    if ($ShotsAtMs) { $argLine += " +set bo1_shots `"$ShotsAtMs`"" }
    if ($ClientAA -gt 1) {
        if ($ShotsAtMs) { Write-Output 'REFUSED: -ClientAA keeps multisampling; back buffer screenshots (-ShotsAtMs) need -ClientAA 1.'; exit 11 }
        $argLine += " +set bo1_headless_aa $ClientAA"
    }
    if ($ClientSound) { $argLine += ' +set bo1_headless_sound 1' }
}
if ($ThreadDumpMs -gt 0) { $argLine += " +set bo1_threaddump $ThreadDumpMs" }
$argLine += " +set net_port $(29060 + 100 * $slot) $Commands"
if ($slot -gt 0) { [BO1Headless]::DesktopName = "bo1_headless$slot" }
# the exe keeps 1024 bytes of command line (sys_cmdline) and appends its own headless settings (dedicated 1, or
# r_aaSamples 1 with -Client) after it; a longer line used to cut those off (x6: r_aaSamples stayed 4 and every
# back buffer screenshot failed). 900 leaves room for the longest suffix.
if ($argLine.Length -gt 900) {
    Write-Output ("REFUSED: command line is {0} characters; the exe keeps 1024 including its own headless settings (limit here 900). Use fewer -ShotsAtMs." -f $argLine.Length)
    exit 11
}
# c2: Com_ParseCommandLine keeps 32 console lines (the text before the first '+', then one per '+' or newline); the
# rest of the line, including the exe's own suffix, lands in line 32 (c7/c8 thief runs: '+set r_aaSamples 1' became
# part of net_ip, the back buffer was 4x multisampled and every screenshot failed at GetRenderTargetData).
$suffixLines = if ($Client) { 4 } else { 3 }
$plusLines = ($argLine.ToCharArray() | Where-Object { $_ -eq '+' -or $_ -eq "`n" }).Count
# L43: the exe keeps 64 lines since the launcher's horde flags + the harness passed 32 (common.cpp com_consoleLines)
if (1 + $plusLines + $suffixLines -gt 64) {
    Write-Output ("REFUSED: command line has {0} '+' commands; with the exe's {1} headless settings that exceeds the 64 console lines Com_ParseCommandLine keeps (limit here {2}). Put settings in a .cfg and exec it." -f $plusLines, $suffixLines, (63 - $suffixLines))
    exit 11
}
$allowedWindows = [ordered]@{}   # -Client: the game window on the private desktop (reported, not killed)
$inputDesktopAtStart = [BO1Headless]::InputDesktopName()

$t0 = Get-Date
$werBefore = @(Get-Process -Name WerFault -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
Write-Output "launch: BO1Zombies.exe $argLine"
Write-Output "cwd:    $H$(if ($H -ne $R) { " (run folder of $R)" })"
[BO1Headless]::PriorityClass = @{ BelowNormal = 0x4000; Normal = 0x20; Idle = 0x40 }[$Priority]
[BO1Headless]::Launch($exe, $argLine, $H)
$procId = [BO1Headless]::Pid
Write-Output "pid:    $procId (desktop WinSta0\$([BO1Headless]::DesktopName), SW_HIDE, slot $slot of $MaxSlots, priority $Priority)"

$sw = [Diagnostics.Stopwatch]::StartNew()
$windows = [ordered]@{}
$werSeen = @{}
$polls = 0
$timedOut = $false
$killedForWindow = $false
try {
    while (-not [BO1Headless]::HasExited()) {
        $polls++
        # the monitor must never show the private desktop (its DACL already denies DESKTOP_SWITCHDESKTOP)
        if ([BO1Headless]::InputDesktopName() -eq [BO1Headless]::DesktopName) {
            $windows['INPUT DESKTOP is the private one'] = [int]$sw.Elapsed.TotalMilliseconds
            Write-Output ("INPUT DESKTOP became WinSta0\{0} at {1} ms: killing, switching back to Default ({2})" -f [BO1Headless]::DesktopName, $windows['INPUT DESKTOP is the private one'], [BO1Headless]::SwitchToDefault())
        }
        foreach ($w in [BO1Headless]::VisibleWindows($procId)) {
            if ($Client -and $w -like "hidden-desktop hwnd=* class='CoDBlackOps' *") {
                if (-not $allowedWindows.Contains($w)) {
                    $allowedWindows[$w] = [int]$sw.Elapsed.TotalMilliseconds
                    Write-Output ("game window (private desktop, allowed) at {0} ms: {1}" -f $allowedWindows[$w], $w)
                }
                continue
            }
            if (-not $windows.Contains($w)) {
                $windows[$w] = [int]$sw.Elapsed.TotalMilliseconds
                Write-Output ("VISIBLE WINDOW at {0} ms: {1}" -f $windows[$w], $w)
            }
        }
        foreach ($p in @(Get-Process -Name WerFault -ErrorAction SilentlyContinue)) {
            if ($werBefore -notcontains $p.Id -and -not $werSeen.ContainsKey($p.Id)) {
                # L7: with parallel runs a WerFault can report on another process (another run, the desktop user's game):
                # 'WerFault.exe -u -p <pid> ...' names it. Not ours -> ignored; unreadable -> treated as ours.
                $cl = (Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)" -ErrorAction SilentlyContinue).CommandLine
                if ($cl -match '\s-p\s+(\d+)' -and [int]$Matches[1] -ne $procId) { $werBefore += $p.Id; continue }
                $werSeen[$p.Id] = [int]$sw.Elapsed.TotalMilliseconds
                Write-Output ("WerFault.exe started (pid {0}) at {1} ms" -f $p.Id, $werSeen[$p.Id])
            }
        }
        # a crash dialog would belong to WerFault, not to the game: kill both
        foreach ($id in @($werSeen.Keys)) {
            foreach ($w in [BO1Headless]::VisibleWindows($id)) {
                if (-not $windows.Contains("WerFault $w")) {
                    $windows["WerFault $w"] = [int]$sw.Elapsed.TotalMilliseconds
                    Write-Output ("VISIBLE WINDOW at {0} ms: WerFault pid {1} {2}" -f $windows["WerFault $w"], $id, $w)
                    Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
                }
            }
        }
        if ($windows.Count -gt 0) { [BO1Headless]::Kill(); $killedForWindow = $true; break }
        if ($sw.Elapsed.TotalSeconds -ge $TimeoutSec) { $killAt = Get-Date; [BO1Headless]::Kill(); $timedOut = $true; break }
        Start-Sleep -Milliseconds 200
    }
    $exitCode = [BO1Headless]::ExitCode()
}
finally {
    [BO1Headless]::Cleanup()
}
$elapsed = [int]$sw.Elapsed.TotalSeconds

Write-Output ''
Write-Output '=== result'
$meaning = switch ($exitCode) {
    0 { 'quit normally' }
    2 { 'Sys_Error (fatal error; see BO1_HEADLESS line)' }
    3 { 'fatal error without message box (see BO1_HEADLESS line)' }
    0xDEAD { 'killed by this script' }
    default { 'crash / abnormal exit (see Application Error below)' }
}
Write-Output ("exit code 0x{0:X} ({1}) after {2} s; timed out: {3}" -f $exitCode, $meaning, $elapsed, $timedOut)
# x6 c32: a timeout is not always a hang. A long map load (50-60 s under load from other runs) plus bo1_autoquit (counted
# after init) can outlast -TimeoutSec while the game still runs; the log's last write tells the two apart.
if ($timedOut -and (Test-Path $log)) {
    $quiet = ($killAt - (Get-Item $log).LastWriteTime).TotalSeconds
    Write-Output ("  timed out: the log was last written {0:N1} s before the kill - {1}" -f $quiet, $(if ($quiet -lt 3) { 'the game was still running (a slow run, e.g. a long load, not a hang): raise -TimeoutSec' } else { 'the log had gone silent: a hang candidate (-ThreadDumpMs counts from process start, the map load included)' }))
}
Write-Output ("window check: {0} polls every 200 ms; visible windows: {1}; killed for a window: {2}" -f $polls, $windows.Count, $killedForWindow)
foreach ($k in $windows.Keys) { Write-Output "  $k" }
if ($Client) {
    Write-Output ("  -Client: game windows on the private desktop (allowed): {0}; input desktop at start '{1}', at end '{2}'" -f $allowedWindows.Count, $inputDesktopAtStart, [BO1Headless]::InputDesktopName())
    foreach ($k in $allowedWindows.Keys) { Write-Output "    $k" }
    $shots = Join-Path $H 'main\screenshots'
    if (Test-Path $shots) {
        Get-ChildItem $shots -File | Where-Object { $_.LastWriteTime -ge $t0 } | ForEach-Object { Write-Output ("  screenshot: {0} ({1} bytes)" -f $_.FullName, $_.Length) }
    }
}
if ($werSeen.Count) {
    foreach ($id in $werSeen.Keys) {
        $cl = (Get-CimInstance Win32_Process -Filter "ProcessId=$id" -ErrorAction SilentlyContinue).CommandLine
        Write-Output "  WerFault pid $id at $($werSeen[$id]) ms: $cl"
    }
} else {
    Write-Output '  WerFault: none'
}

Write-Output ''
Write-Output "=== log $log"
if ((Test-Path $log) -and (Get-Item $log).LastWriteTime -ge $t0) {
    $pattern = 'Com_ERROR|BO1_HEADLESS|^ERROR|ERROR:|Error:|Exceeded limit|Could not find|Couldn''t find|script compile|Unknown function|unknown builtin|Loaded fastfile|Server:|bo1_autoquit|bo1_shots|bo1_headless_client|back buffer|Unloaded fastfile ''zombie|WARNING: Could|mapkit_selftest|mapkit: check'
    # A VM spin can produce millions of lines. Keep the same checks and report limits
    # without materializing the whole log as PowerShell objects.
    $lineCount = 0
    $decisive = New-Object 'System.Collections.Generic.List[string]'
    $tail = New-Object 'System.Collections.Generic.Queue[string]'
    foreach ($line in [System.IO.File]::ReadLines($log)) {
        ++$lineCount
        if ($decisive.Count -lt 80 -and $line -match $pattern) {
            $decisive.Add(('  {0,6}: {1}' -f $lineCount, $line))
        }
        if ($TailLines -gt 0) {
            $tail.Enqueue($line)
            if ($tail.Count -gt $TailLines) { [void]$tail.Dequeue() }
        }
    }
    Write-Output ("{0} lines. Decisive lines:" -f $lineCount)
    $decisive | ForEach-Object { Write-Output $_ }
    Write-Output "--- last $TailLines lines"
    $tail | ForEach-Object { "  $_" }
    if ($SaveLog) { Copy-Item $log $SaveLog -Force; Write-Output "log saved to $SaveLog" }
} else {
    Write-Output 'no fresh log (the process died before the filesystem opened it)'
}

Write-Output ''
Write-Output '=== Application Error events since launch'
$events = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = $t0 } -ErrorAction SilentlyContinue |
        Where-Object { $_.Message -match 'BO1Zombies' -and -not ($_.Message -match 'Faulting process id: 0x([0-9a-fA-F]+)' -and [Convert]::ToInt32($Matches[1], 16) -ne $procId) })   # L7: only this run's process
if (-not $events.Count) { Write-Output '  none' }
foreach ($e in $events) {
    Write-Output ("  {0}" -f $e.TimeCreated)
    $e.Message -split "`r?`n" | Where-Object { $_ -match 'Faulting module name|Exception code|Fault offset' } | ForEach-Object { "    $_" }
    if ($e.Message -match 'Faulting module name: ([^,\r\n]+)' -and $Matches[1] -match 'BO1Zombies' -and $e.Message -match 'Fault offset: (0x[0-9a-fA-F]+)') {
        $off = $Matches[1]
        if (Test-Path $symbolizer) {
            Write-Output "    symbolized ($off):"
            & $symbolizer "--obj=$exe" --relative-address $off 2>&1 | ForEach-Object { "      $_" }
        }
    }
}
# the exe's emergency lines (asserts, crashes, Sys_Error) survive in their own file; stack offsets symbolized
$emergency = Join-Path $H 'main\bo1_emergency.log'
if ((Test-Path $emergency) -and (Get-Item $emergency).LastWriteTime -ge $t0) {
    Write-Output "  $emergency"
    foreach ($line in [System.IO.File]::ReadAllLines($emergency)) {
        Write-Output ("    {0}" -f ($(if ($line.Length -gt 300) { $line.Substring(0, 300) + ' ...' } else { $line })))
        if ($line -match '^BO1_HEADLESS (STACK|FRAMES):' -and (Test-Path $symbolizer)) {
            $offs = @([regex]::Matches($line, '\+0x[0-9A-Fa-f]+') | ForEach-Object { $_.Value.Substring(1) } | Select-Object -First $ThreadDumpFrames)
            $sym = @($offs | & $symbolizer "--obj=$exe" --relative-address --no-inlines 2>&1)
            for ($i = 0; $i -lt $sym.Count; $i += 3) { Write-Output ('      {0}' -f $sym[$i]) }
        }
    }
}
# the exe's own crash line (module+offset), in case WER did not record one
if ((Test-Path $log) -and (Get-Item $log).LastWriteTime -ge $t0) {
    foreach ($m in (Select-String -Path $log -Pattern 'BO1_HEADLESS CRASH: .*\(BO1Zombies\.exe\+(0x[0-9A-Fa-f]+)\)')) {
        $off = $m.Matches[0].Groups[1].Value
        Write-Output "  exe crash line offset ${off}:"
        & $symbolizer "--obj=$exe" --relative-address $off 2>&1 | ForEach-Object { "      $_" }
    }
    # -ThreadDumpMs: every thread's EIP and stack return addresses (stack scan: stale words can appear), symbolized
    $threadLog = Join-Path $H 'main\bo1_threads.log'
    $threadLines = @(); if ($ThreadDumpMs -gt 0 -and (Test-Path $threadLog)) { $threadLines = @(Select-String -Path $threadLog -Pattern '^BO1_HEADLESS (THREADDUMP|THREAD|THREADFRAMES|THREADWALK) ') }
    if ($ThreadDumpMs -gt 0) { Write-Output ''; Write-Output "  thread dumps ($threadLog): $($threadLines.Count) lines" }
    foreach ($m in $threadLines) {
        $line = $m.Line
        if ($line -match '^BO1_HEADLESS THREADDUMP') { Write-Output ''; Write-Output "  $line"; continue }
        # THREADFRAMES (EBP chain) / THREADWALK (dbghelp StackWalk64 with the PDB's frame data): all frames; THREAD (stack scan): the first -ThreadDumpFrames
        $maxFrames = $(if ($line -match '^BO1_HEADLESS THREAD(FRAMES|WALK)') { 64 } else { $ThreadDumpFrames })
        $offs = @([regex]::Matches($line, '\+0x[0-9A-Fa-f]+') | ForEach-Object { $_.Value.Substring(1) } | Select-Object -First $maxFrames)
        Write-Output ("  {0}" -f ($line -replace ' (stack|ebp|walk) .*', ''))
        if ($offs.Count -and (Test-Path $symbolizer)) {
            $sym = @($offs | & $symbolizer "--obj=$exe" --relative-address --no-inlines 2>&1)
            $names = @(); for ($i = 0; $i -lt $sym.Count; $i += 3) { $names += ('{0} ({1})' -f $sym[$i], (($sym[$i + 1] -split '\\')[-1])) }
            $names | ForEach-Object { "      $_" }
        }
    }
}

Write-Output ''
Write-Output '=== Steam install untouched (files written since launch; must be none)'
$written = @(Get-ChildItem $steamGame -Recurse -Force -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $t0 })
if ($written.Count) { $written | ForEach-Object { "  WRITTEN: $($_.FullName)" } } else { Write-Output '  none' }

if ($windows.Count) { exit 20 }
if ($timedOut) { exit 21 }
# a crash, assert or fatal error is not a pass: callers checking only the exit code must see it
if ($exitCode -ne 0) { exit 12 }
exit 0
