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

# Download the artifact
Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -UseBasicParsing

# SHA-256 verification — download SHA256SUMS for this release tag
$tag = $release.tag_name
$shaUrl = "https://github.com/$repo/releases/download/$tag/SHA256SUMS"
$shaFallback = "https://notepatra.org/SHA256SUMS.$tag.txt"
$shaFile = "$env:TEMP\SHA256SUMS"
$shaOk = $false
try {
    Invoke-WebRequest -Uri $shaUrl -OutFile $shaFile -UseBasicParsing -ErrorAction Stop
    $shaOk = $true
} catch {
    try {
        Invoke-WebRequest -Uri $shaFallback -OutFile $shaFile -UseBasicParsing -ErrorAction Stop
        $shaOk = $true
    } catch {
        Write-Host "  WARN: SHA256SUMS not available — skipping checksum verification" -ForegroundColor Yellow
    }
}
if ($shaOk) {
    $expected = (Get-Content $shaFile | Select-String -Pattern $fileName -SimpleMatch | Select-Object -First 1) -replace '^([a-f0-9]+).*','$1'
    if ($expected) {
        $actual = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash.ToLower()
        if ($actual -ne $expected.ToLower()) {
            Write-Host ""
            Write-Host "  ERROR: SHA-256 mismatch — refusing to install." -ForegroundColor Red
            Write-Host "    expected: $expected" -ForegroundColor Red
            Write-Host "    actual:   $actual" -ForegroundColor Red
            Write-Host "    file:     $fileName" -ForegroundColor Red
            Write-Host ""
            Write-Host "  This means the download was corrupted, MITM'd, or the release was tampered with." -ForegroundColor Red
            Write-Host "  Report at: https://github.com/$repo/issues/new" -ForegroundColor Red
            Remove-Item $zipPath -Force
            exit 1
        }
        Write-Host "  SHA-256 verified" -ForegroundColor Green
    }
}

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
