# Notepatra Installer for Windows
# Usage: irm https://notepatra.org/install.ps1 | iex
# Or: powershell -ExecutionPolicy Bypass -Command "irm https://notepatra.org/install.ps1 | iex"

$ErrorActionPreference = "Stop"

# Ensure the console renders our UTF-8 box-drawing chars, em-dashes, and the
# final "installed" check correctly. Without this, the Windows console falls
# back to the legacy OEM codepage (cp437/cp1252) and every multi-byte UTF-8
# sequence shows up as garbage like "â•" / "â€"" / "âœ…".
try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
} catch {}

Write-Host ""
Write-Host "  +======================================+" -ForegroundColor Cyan
Write-Host "  |         Notepatra Installer          |" -ForegroundColor Cyan
Write-Host "  |   The code editor for the AI era     |" -ForegroundColor Cyan
Write-Host "  +======================================+" -ForegroundColor Cyan
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

# SHA-256 verification — HARD REQUIRED. Refuse install if SHA256SUMS unreachable
# or the artifact is not listed. (Previously soft-fail: MITM could swap binary
# while blocking SHA256SUMS and bypass verification.)
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
        Write-Host ""
        Write-Host "  ERROR: Could not fetch SHA256SUMS for release $tag" -ForegroundColor Red
        Write-Host "    Tried: $shaUrl" -ForegroundColor Red
        Write-Host "    Tried: $shaFallback" -ForegroundColor Red
        Write-Host "  Refusing to install an unverified binary." -ForegroundColor Red
        Write-Host ""
        Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
        exit 1
    }
}
$expected = (Get-Content $shaFile | Select-String -Pattern $fileName -SimpleMatch | Select-Object -First 1) -replace '^([a-f0-9]+).*','$1'
if (-not $expected) {
    Write-Host ""
    Write-Host "  ERROR: Artifact '$fileName' is not listed in SHA256SUMS for $tag" -ForegroundColor Red
    Write-Host "  Refusing to install an unverified binary." -ForegroundColor Red
    Write-Host ""
    Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
    exit 1
}
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

# Optional cosign verification — strong gate if cosign is on PATH
$cosign = Get-Command cosign -ErrorAction SilentlyContinue
if ($cosign) {
    $sigUrl = "$downloadUrl.sig"
    $pemUrl = "$downloadUrl.pem"
    $sigPath = "$zipPath.sig"
    $pemPath = "$zipPath.pem"
    $sigOk = $false
    try {
        Invoke-WebRequest -Uri $sigUrl -OutFile $sigPath -UseBasicParsing -ErrorAction Stop
        Invoke-WebRequest -Uri $pemUrl -OutFile $pemPath -UseBasicParsing -ErrorAction Stop
        $sigOk = $true
    } catch {
        Write-Host "  WARN: Cosign present but signature files unavailable — falling back to SHA-only" -ForegroundColor Yellow
    }
    if ($sigOk) {
        $certIdRegex = "^https://github.com/$repo/\.github/workflows/.+@refs/tags/$tag$"
        & cosign verify-blob `
            --certificate $pemPath `
            --signature $sigPath `
            --certificate-identity-regexp $certIdRegex `
            --certificate-oidc-issuer "https://token.actions.githubusercontent.com" `
            $zipPath 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host ""
            Write-Host "  ERROR: Cosign signature verification failed for $tag" -ForegroundColor Red
            Write-Host "  Refusing to install." -ForegroundColor Red
            Write-Host ""
            Remove-Item $zipPath, $sigPath, $pemPath -Force -ErrorAction SilentlyContinue
            exit 1
        }
        Write-Host "  Cosign signature verified" -ForegroundColor Green
        Remove-Item $sigPath, $pemPath -Force -ErrorAction SilentlyContinue
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
$sizeKb = [int]((Get-ChildItem $installDir -Recurse | Measure-Object Length -Sum).Sum / 1KB)

# ─── Register in Windows "Installed apps" so it shows in Settings → Apps ───
# and Add/Remove Programs, and so the user can uninstall it the normal way.
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Notepatra"
$displayVersion = ($release.tag_name -replace '^v','')
$uninstallScript = "$installDir\uninstall.ps1"

# Drop an uninstall.ps1 next to the binary that the registry "UninstallString"
# points to. Windows will run this when the user clicks "Uninstall" in Settings.
@'
# Notepatra uninstaller — generated by install.ps1
# Removes files, registry entries, PATH entry, and shortcuts.
$ErrorActionPreference = "SilentlyContinue"
Write-Host "Uninstalling Notepatra..." -ForegroundColor Yellow

$installDir = "$env:LOCALAPPDATA\Notepatra"
$startMenu  = "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Notepatra.lnk"
$desktop    = "$env:USERPROFILE\Desktop\Notepatra.lnk"

# Remove shortcuts
Remove-Item -Force $startMenu
Remove-Item -Force $desktop

# Remove from user PATH
$userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
if ($userPath -like "*$installDir*") {
    $newPath = ($userPath -split ';' | Where-Object { $_ -ne $installDir }) -join ';'
    [Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
}

# Remove registry uninstall entry
Remove-Item -Recurse -Force "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Notepatra"

# Remove install directory (this also removes uninstall.ps1 itself, so do it last)
Start-Job -ScriptBlock { Start-Sleep -Seconds 2; Remove-Item -Recurse -Force $using:installDir } | Out-Null

Write-Host "Notepatra has been uninstalled." -ForegroundColor Green
'@ | Out-File -FilePath $uninstallScript -Encoding UTF8 -Force

# Write the Uninstall registry key — this is what makes Notepatra show up in
# Settings → Apps → Installed apps and in Control Panel → Programs.
New-Item -Path $uninstallKey -Force | Out-Null
$uninstallCmd = "powershell.exe -ExecutionPolicy Bypass -File `"$uninstallScript`""
Set-ItemProperty -Path $uninstallKey -Name "DisplayName"     -Value "Notepatra"
Set-ItemProperty -Path $uninstallKey -Name "DisplayVersion"  -Value $displayVersion
Set-ItemProperty -Path $uninstallKey -Name "Publisher"       -Value "Prateek Singh"
Set-ItemProperty -Path $uninstallKey -Name "InstallLocation" -Value $installDir
Set-ItemProperty -Path $uninstallKey -Name "DisplayIcon"     -Value "$installDir\$exeName"
Set-ItemProperty -Path $uninstallKey -Name "UninstallString" -Value $uninstallCmd
Set-ItemProperty -Path $uninstallKey -Name "QuietUninstallString" -Value $uninstallCmd
Set-ItemProperty -Path $uninstallKey -Name "URLInfoAbout"    -Value "https://notepatra.org"
Set-ItemProperty -Path $uninstallKey -Name "HelpLink"        -Value "https://github.com/singhpratech/notepatra"
Set-ItemProperty -Path $uninstallKey -Name "EstimatedSize"   -Value $sizeKb -Type DWord
Set-ItemProperty -Path $uninstallKey -Name "NoModify"        -Value 1 -Type DWord
Set-ItemProperty -Path $uninstallKey -Name "NoRepair"        -Value 1 -Type DWord
Write-Host "  Registered in Windows 'Installed apps' (uninstall via Settings → Apps)" -ForegroundColor Green

Write-Host ""
Write-Host "  ✅ Notepatra installed! ($size MB)" -ForegroundColor Green
Write-Host ""
Write-Host "  Location:  $installDir\$exeName" -ForegroundColor White
Write-Host "  Run:       notepatra" -ForegroundColor White
Write-Host "  Or find 'Notepatra' in Start Menu" -ForegroundColor White
Write-Host ""
Write-Host "  Envisioned by Prateek Singh. Built by Claude." -ForegroundColor DarkGray
Write-Host ""
