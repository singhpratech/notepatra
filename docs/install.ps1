# Notepatra Installer for Windows
# Usage: irm https://notepatra.org/install.ps1 | iex
# Or: powershell -ExecutionPolicy Bypass -Command "irm https://notepatra.org/install.ps1 | iex"

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "  ╔══════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "  ║         Notepatra Installer          ║" -ForegroundColor Cyan
Write-Host "  ║   The code editor for the AI era     ║" -ForegroundColor Cyan
Write-Host "  ╚══════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

$repo = "singhpratech/notepatra"
$installDir = "$env:LOCALAPPDATA\Notepatra"
$exeName = "notepatra.exe"

# Create install directory
New-Item -ItemType Directory -Path $installDir -Force | Out-Null

Write-Host "  Fetching latest release..." -ForegroundColor Yellow

# Get latest release
try {
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest"
    $asset = $release.assets | Where-Object { $_.name -like "*windows*" } | Select-Object -First 1

    if ($asset) {
        $downloadUrl = $asset.browser_download_url
        $fileName = $asset.name
    } else {
        throw "No Windows release found"
    }
} catch {
    Write-Host ""
    Write-Host "  No release available yet. Download manually from:" -ForegroundColor Red
    Write-Host "  https://github.com/$repo/actions" -ForegroundColor White
    Write-Host ""
    Write-Host "  Or build from source:" -ForegroundColor Yellow
    Write-Host "    1. Install Qt5, Rust, CMake, Visual Studio 2022" -ForegroundColor Gray
    Write-Host "    2. git clone https://github.com/$repo.git" -ForegroundColor Gray
    Write-Host "    3. cd notepatra\rust-core && cargo build --release && cd .." -ForegroundColor Gray
    Write-Host "    4. mkdir build && cd build && cmake .. && cmake --build . --config Release" -ForegroundColor Gray
    Write-Host ""
    exit 0
}

Write-Host "  Downloading: $fileName" -ForegroundColor Yellow
$zipPath = "$env:TEMP\notepatra-download.zip"

# Download
Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -UseBasicParsing

# Extract
Write-Host "  Extracting to $installDir..." -ForegroundColor Yellow
Expand-Archive -Path $zipPath -DestinationPath $installDir -Force
Remove-Item $zipPath -Force

# Find the exe
$exe = Get-ChildItem -Path $installDir -Recurse -Filter $exeName | Select-Object -First 1
if (-not $exe) {
    Write-Host "  Error: notepatra.exe not found after extraction" -ForegroundColor Red
    exit 1
}

# Move everything to install dir root if nested
if ($exe.Directory.FullName -ne $installDir) {
    Get-ChildItem -Path $exe.Directory.FullName -Recurse | Move-Item -Destination $installDir -Force -ErrorAction SilentlyContinue
}

# Add to PATH
$userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
if ($userPath -notlike "*$installDir*") {
    [Environment]::SetEnvironmentVariable("PATH", "$installDir;$userPath", "User")
    Write-Host "  Added to PATH" -ForegroundColor Green
}

# Create Start Menu shortcut
$startMenu = "$env:APPDATA\Microsoft\Windows\Start Menu\Programs"
$shortcutPath = "$startMenu\Notepatra.lnk"
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = "$installDir\$exeName"
$shortcut.WorkingDirectory = $installDir
$shortcut.Description = "Notepatra — Native code editor with AI-powered formatters"
$shortcut.Save()
Write-Host "  Created Start Menu shortcut" -ForegroundColor Green

# Create Desktop shortcut
$desktopPath = "$env:USERPROFILE\Desktop\Notepatra.lnk"
$shortcut2 = $shell.CreateShortcut($desktopPath)
$shortcut2.TargetPath = "$installDir\$exeName"
$shortcut2.WorkingDirectory = $installDir
$shortcut2.Description = "Notepatra — Native code editor"
$shortcut2.Save()
Write-Host "  Created Desktop shortcut" -ForegroundColor Green

$size = [math]::Round((Get-ChildItem $installDir -Recurse | Measure-Object Length -Sum).Sum / 1MB, 1)

Write-Host ""
Write-Host "  ✅ Notepatra installed! ($size MB)" -ForegroundColor Green
Write-Host ""
Write-Host "  Location:  $installDir\$exeName" -ForegroundColor White
Write-Host "  Run:       notepatra" -ForegroundColor White
Write-Host "  Or find 'Notepatra' in Start Menu" -ForegroundColor White
Write-Host ""
Write-Host "  Envisioned by Prateek Singh. Built by Claude." -ForegroundColor DarkGray
Write-Host ""
