# Windows bundle: manual DLL copy fallback (skip windeployqt)

If `windeployqt` keeps failing to deploy the Qt platform plugins, the next defensive option is to **bypass windeployqt entirely** and copy every required DLL manually. This is what many production Qt apps do because windeployqt's transitive-scanning behavior is unreliable.

## The replacement bundle step

Replace the current `Verify exe, embed icon, bundle Qt + QScintilla DLLs` step with this:

```yaml
- name: Bundle Qt + QScintilla DLLs (manual, no windeployqt)
  shell: pwsh
  run: |
    $exe = Get-ChildItem -Path build -Recurse -Filter "notepatra.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $exe) {
      Write-Host "::error::notepatra.exe not found"
      Get-ChildItem -Path build -Recurse | Select-Object FullName
      exit 1
    }

    $outDir = "notepatra-win"
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    Copy-Item $exe.FullName "$outDir\"

    $qtRoot = "${{ steps.qscintilla.outputs.QT_ROOT }}"
    $qtBin = "$qtRoot\bin"
    $qtPlugins = "$qtRoot\plugins"

    # Required Qt5 DLLs for a Qt5Widgets + Qt5Network + Qt5PrintSupport app.
    $requiredDlls = @(
      "Qt5Core.dll",
      "Qt5Gui.dll",
      "Qt5Widgets.dll",
      "Qt5Network.dll",
      "Qt5PrintSupport.dll"
    )

    foreach ($dll in $requiredDlls) {
      $src = Join-Path $qtBin $dll
      if (Test-Path $src) {
        Copy-Item $src $outDir
        Write-Host "  ✓ $dll"
      } else {
        Write-Host "::error::Missing required Qt DLL: $src"
        exit 1
      }
    }

    # Required Qt plugins (subdirectory layout matters — keep platforms/, styles/, etc.)
    $requiredPluginPaths = @(
      @{src="platforms\qwindows.dll"; dest="platforms"},
      @{src="styles\qwindowsvistastyle.dll"; dest="styles"},
      @{src="imageformats\qico.dll"; dest="imageformats"},
      @{src="imageformats\qjpeg.dll"; dest="imageformats"},
      @{src="imageformats\qsvg.dll"; dest="imageformats"},
      @{src="iconengines\qsvgicon.dll"; dest="iconengines"},
      @{src="printsupport\windowsprintersupport.dll"; dest="printsupport"},
      @{src="bearer\qgenericbearer.dll"; dest="bearer"}
    )

    foreach ($p in $requiredPluginPaths) {
      $src = Join-Path $qtPlugins $p.src
      $destDir = Join-Path $outDir $p.dest
      New-Item -ItemType Directory -Path $destDir -Force | Out-Null
      if (Test-Path $src) {
        Copy-Item $src $destDir
        Write-Host "  ✓ $($p.src)"
      } else {
        Write-Host "::warning::Missing optional Qt plugin: $src"
      }
    }

    # QScintilla DLL
    $qsciDll = "${{ steps.qscintilla.outputs.QSC_DLL }}"
    if ($qsciDll -and (Test-Path $qsciDll)) {
      Copy-Item $qsciDll "$outDir\"
      Write-Host "  ✓ qscintilla2_qt5.dll"
    } else {
      Write-Host "::error::qscintilla2_qt5.dll missing"
      exit 1
    }

    # MSVC runtime DLLs (find them in the VS install)
    # These are preinstalled on Windows Server 2022 runners but we bundle
    # them so the binary works on stripped-down Windows installs.
    $vcRedistPaths = @()
    if ($env:VCToolsRedistDir) {
      $vcRedistPaths += "$env:VCToolsRedistDir\x64\Microsoft.VC*.CRT"
    }
    $vcRedistPaths += "C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT"
    $vcRedistPaths += "C:\Program Files\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT"

    $vcRedistFound = $false
    foreach ($path in $vcRedistPaths) {
      $matches = Get-ChildItem -Path $path -ErrorAction SilentlyContinue
      foreach ($d in $matches) {
        Get-ChildItem $d.FullName -Filter "*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
          Copy-Item $_.FullName $outDir -Force
          Write-Host "  ✓ $($_.Name) (from MSVC redist)"
          $vcRedistFound = $true
        }
        if ($vcRedistFound) { break }
      }
      if ($vcRedistFound) { break }
    }
    if (-not $vcRedistFound) {
      Write-Host "::warning::MSVC redist DLLs not found — relying on system install"
    }

    Write-Host ""
    Write-Host "════════════ Final bundle ════════════"
    Get-ChildItem $outDir -Recurse | Select-Object FullName, Length | Format-Table -AutoSize
    $totalSize = (Get-ChildItem $outDir -Recurse | Measure-Object Length -Sum).Sum / 1MB
    Write-Host "Total package: $([math]::Round($totalSize, 1)) MB"

    # Final assertion: all critical files present
    $critical = @(
      "$outDir\notepatra.exe",
      "$outDir\Qt5Core.dll",
      "$outDir\Qt5Gui.dll",
      "$outDir\Qt5Widgets.dll",
      "$outDir\qscintilla2_qt5.dll",
      "$outDir\platforms\qwindows.dll"
    )
    $missing = @()
    foreach ($f in $critical) {
      if (-not (Test-Path $f)) { $missing += $f }
    }
    if ($missing.Count -gt 0) {
      Write-Host "::error::Critical files missing from bundle:"
      foreach ($f in $missing) { Write-Host "  - $f" }
      exit 1
    }
    Write-Host "✓ All critical files present"
```

## Why this is more reliable than windeployqt

1. **Explicit list, no auto-discovery.** We know exactly which DLLs we need and we copy them. No "windeployqt didn't see this dependency".
2. **Subdirectory structure preserved.** `platforms\qwindows.dll`, `styles\qwindowsvistastyle.dll`, etc. — all in the right relative paths next to the exe.
3. **No transitive scan limitation.** windeployqt's "I don't recurse into third-party DLLs" problem doesn't exist here because we don't ask it to.
4. **Same DLLs every time, regardless of which Qt features the exe happens to import.**

## Tradeoffs

- **Bigger bundle** — copies things we might not strictly need (e.g. Qt5Network if the exe doesn't actually use it)
- **List has to stay in sync with the source code** — if we add Qt5Sql later, we need to add it to the list
- **No `--compiler-runtime` shortcut** — we manually find the MSVC redist

The benefit (bundle ALWAYS works) outweighs the cost (slightly bigger zip + a 10-line list to maintain).

## When to apply this

Apply this if:
1. The current dual-windeployqt approach keeps failing
2. The diagnostic in issue #1 shows windeployqt running successfully but `platforms\qwindows.dll` still missing
3. After ~3 more iterations of trying to convince windeployqt to do the right thing

This is the **deterministic** alternative. windeployqt is convenience; the manual copy is correctness.
