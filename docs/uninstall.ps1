# Notepatra Uninstaller for Windows
# Usage: irm https://notepatra.org/uninstall.ps1 | iex
#
# Removes everything that install.ps1 or notepatra-setup-X.Y.Z.exe would
# have created. Leaves alone: files you edited, Visual C++ Redistributables,
# and Ollama.

$ErrorActionPreference = "SilentlyContinue"

Write-Host ""
Write-Host "  ╔══════════════════════════════════════╗" -ForegroundColor Yellow
Write-Host "  ║       Notepatra Uninstaller          ║" -ForegroundColor Yellow
Write-Host "  ╚══════════════════════════════════════╝" -ForegroundColor Yellow
Write-Host ""

$installDir = "$env:LOCALAPPDATA\Notepatra"
$startMenu  = "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Notepatra.lnk"
$desktop    = "$env:USERPROFILE\Desktop\Notepatra.lnk"
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Notepatra"
$userConfig = "$env:USERPROFILE\.config\notepatra"
$appData    = "$env:APPDATA\Notepatra"

# Check if NSIS uninstaller exists — if so, run it (it cleans up properly)
$nsisUninstaller = "$installDir\uninstall.exe"
if (Test-Path $nsisUninstaller) {
    Write-Host "  Found NSIS uninstaller at $nsisUninstaller" -ForegroundColor Cyan
    Write-Host "  Running silent uninstall..." -ForegroundColor Cyan
    Start-Process -FilePath $nsisUninstaller -ArgumentList "/S" -Wait
}

# Belt-and-braces manual cleanup (in case NSIS uninstaller wasn't there
# or didn't catch everything from a partially-installed copy)
if (Test-Path $installDir) {
    Write-Host "  Removing $installDir..." -ForegroundColor Cyan
    Remove-Item -Recurse -Force $installDir
}

if (Test-Path $startMenu) {
    Write-Host "  Removing Start Menu shortcut..." -ForegroundColor Cyan
    Remove-Item -Force $startMenu
}

if (Test-Path $desktop) {
    Write-Host "  Removing Desktop shortcut..." -ForegroundColor Cyan
    Remove-Item -Force $desktop
}

# Remove from user PATH
$userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
if ($userPath -like "*Notepatra*") {
    Write-Host "  Removing from user PATH..." -ForegroundColor Cyan
    $newPath = ($userPath -split ';' |
                Where-Object { $_ -notlike "*Notepatra*" -and $_ -ne "" }) -join ';'
    [Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
}

if (Test-Path $uninstallKey) {
    Write-Host "  Removing Installed Apps registry entry..." -ForegroundColor Cyan
    Remove-Item -Recurse -Force $uninstallKey
}

# User data
if (Test-Path $userConfig) {
    Write-Host "  Removing user config at $userConfig..." -ForegroundColor Cyan
    Remove-Item -Recurse -Force $userConfig
}
if (Test-Path $appData) {
    Write-Host "  Removing app data at $appData..." -ForegroundColor Cyan
    Remove-Item -Recurse -Force $appData
}

Write-Host ""
Write-Host "  ✅ Notepatra removed." -ForegroundColor Green
Write-Host ""
Write-Host "  Left alone (intentional):" -ForegroundColor Gray
Write-Host "    • Visual C++ Redistributables (used by other apps)" -ForegroundColor Gray
Write-Host "    • Files you edited with Notepatra" -ForegroundColor Gray
Write-Host "    • Ollama and any models you pulled" -ForegroundColor Gray
Write-Host ""
