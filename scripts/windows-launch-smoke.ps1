# windows-launch-smoke.ps1 — start a PACKAGED notepatra.exe for real (the
# bundled windows platform plugin, not offscreen) and fail if it cannot stay up.
#
#   pwsh scripts/windows-launch-smoke.ps1 -Exe <path\to\notepatra.exe> [-Seconds 8]
#
# Windows counterpart of scripts/macos-launch-smoke.sh and
# scripts/linux-launch-smoke.sh: v0.1.131 shipped macOS DMGs that could not
# start because CI only ran build-tree binaries and `--version` never loads a
# platform plugin. Point this at notepatra.exe inside an EXTRACTED portable zip.
#
# What it does: isolated APPDATA / LOCALAPPDATA / USERPROFILE (USERPROFILE also
# gives the single-instance server its own name), QT_DEBUG_PLUGINS=1 with
# stderr logging forced, critical-error boxes suppressed for the child (a
# missing DLL then exits with 0xC0000135 instead of hanging on a modal box),
# runs Seconds, then kills the process tree by PID.
# The startup update check is pre-disabled only when CI=true: QSettings lives
# in HKCU, which the isolated env vars do not redirect.
#
# Fails (exit 1) when: the process exits before the deadline (any status);
# stderr shows a Qt platform-plugin failure; the process's top-level window is
# a Win32 dialog (#32770: Qt's fatal "no Qt platform plugin" box or another
# modal error) instead of a Qt window; the crash handler wrote
# recovery\.crash_flag. No top-level window at all only warns (liveness only).
# Exit 2: bad arguments or the exe is missing.
# UNEXECUTED when written (authored on Linux); first real run is in CI.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [int]$Seconds = 8
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    Write-Host "ERROR: $Exe does not exist"
    exit 2
}
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$exeDir = Split-Path -Parent $Exe

Add-Type -Namespace NpSmoke -Name Native -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode);
[DllImport("user32.dll", CharSet = CharSet.Unicode)]
public static extern int GetClassName(System.IntPtr hWnd, System.Text.StringBuilder buf, int max);
'@
# SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX; inherited by the child.
[void][NpSmoke.Native]::SetErrorMode(0x0001 -bor 0x0002 -bor 0x8000)

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("np-launch-" + [guid]::NewGuid().ToString('N'))
$appData = Join-Path $work 'appdata'
$localAppData = Join-Path $work 'localappdata'
$profileDir = Join-Path $work 'home'
New-Item -ItemType Directory -Force -Path $appData, $localAppData, $profileDir | Out-Null
$outLog = Join-Path $work 'stdout.log'
$errLog = Join-Path $work 'stderr.log'

if ($env:CI -eq 'true') {
    $key = 'HKCU:\Software\Notepatra\Notepatra\updates'
    New-Item -Path $key -Force | Out-Null
    New-ItemProperty -Path $key -Name 'checkOnStartup' -Value 'false' -PropertyType String -Force | Out-Null
} else {
    Write-Host "note: not on CI (CI!=true); leaving the registry untouched, the startup update check runs silently"
}

$env:APPDATA = $appData
$env:LOCALAPPDATA = $localAppData
$env:USERPROFILE = $profileDir
$env:QT_DEBUG_PLUGINS = '1'
$env:QT_FORCE_STDERR_LOGGING = '1'
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue

Write-Host "launch smoke: $Exe (${Seconds}s, APPDATA=$appData)"
$p = Start-Process -FilePath $Exe -WorkingDirectory $exeDir -PassThru `
    -RedirectStandardOutput $outLog -RedirectStandardError $errLog
$null = $p.Handle # cache the handle, or ExitCode reads back as $null after exit

$failures = New-Object System.Collections.Generic.List[string]
$exitedEarly = $false
$windowClass = ''
$windowTitle = ''
for ($i = 1; $i -le $Seconds; $i++) {
    Start-Sleep -Seconds 1
    $p.Refresh()
    if ($p.HasExited) { $exitedEarly = $true; break }
    if ($p.MainWindowHandle -ne [System.IntPtr]::Zero) {
        $sb = New-Object System.Text.StringBuilder 256
        [void][NpSmoke.Native]::GetClassName($p.MainWindowHandle, $sb, 256)
        $windowClass = $sb.ToString()
        $windowTitle = $p.MainWindowTitle
    }
}

if ($exitedEarly) {
    $code = $p.ExitCode
    $hex = if ($null -ne $code) { '0x{0:X8}' -f $code } else { 'unknown' }
    $failures.Add("process exited on its own with code $code ($hex) after ${i}s; 0xC0000135 = DLL not found, 0xC0000005 = access violation, 3 = abort")
} else {
    & taskkill.exe /PID $p.Id /T /F | Out-Host
    try { $p.WaitForExit(5000) | Out-Null } catch { }
}

$err = ''
if (Test-Path $errLog) { $err = Get-Content -Raw $errLog }
if ($null -eq $err) { $err = '' }
Write-Host "=== stderr (filtered: platform plugin, errors) ==="
($err -split "`n") | Where-Object { $_ -match 'qwindows|Cannot load library|Qt platform plugin|rror' } |
    Select-Object -First 60 | ForEach-Object { Write-Host $_ }
Write-Host "=== end ==="

if ($err -match 'Could not (load|find) the Qt platform plugin|no Qt platform plugin could be initialized') {
    $failures.Add('Qt platform plugin failed to load')
}
if (-not $exitedEarly) {
    if ($windowClass -eq '#32770') {
        $failures.Add("top-level window is a Win32 dialog (#32770) titled '$windowTitle', not a Qt window: an error box is up")
    } elseif ($windowClass -eq '') {
        Write-Host "::warning::launch smoke: no top-level window visible to this session; only liveness was verified"
    } else {
        Write-Host "window: class '$windowClass', title '$windowTitle'"
    }
}
if (Test-Path (Join-Path $appData 'Notepatra\recovery\.crash_flag')) {
    $failures.Add("the app's crash handler fired (recovery\.crash_flag written)")
}

Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
if ($failures.Count -gt 0) {
    foreach ($f in $failures) { Write-Host "::error::windows launch smoke: $f" }
    Write-Host "=== full stderr ==="
    Write-Host $err
    exit 1
}
Write-Host "launch smoke OK: ran ${Seconds}s, no platform-plugin error, no crash"
exit 0
