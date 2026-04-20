@echo off
:: ─────────────────────────────────────────────────────────────────
::  Notepatra — register file associations (portable zip edition)
::
::  Run this ONCE after unzipping. It adds Notepatra to the Windows
::  "Open with" menu for common text/code file types (HKCU only —
::  no admin required). After running, you can right-click any
::  text file → "Open with" → "Notepatra" to set it as default.
::
::  Undo with: unregister-associations.bat
:: ─────────────────────────────────────────────────────────────────

setlocal EnableDelayedExpansion
set "EXE=%~dp0notepatra.exe"

if not exist "%EXE%" (
    echo ERROR: notepatra.exe not found at %EXE%
    echo Run this script from the folder where you unzipped Notepatra.
    pause
    exit /b 1
)

echo.
echo   Registering Notepatra as "Open with" candidate for:
echo   .txt .log .md .json .xml .yaml .yml .ini .conf .cfg .csv
echo   .py .js .ts .cpp .c .h .hpp .rs .go .java .cs .sql
echo   .html .css .sh .ps1 .bat
echo.

:: Register the app under HKCU (current user only, no admin)
reg add "HKCU\Software\Classes\Applications\notepatra.exe\shell\open\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul
reg add "HKCU\Software\Classes\Applications\notepatra.exe" /v "FriendlyAppName" /d "Notepatra" /f >nul
reg add "HKCU\Software\Classes\Applications\notepatra.exe\shell\open" /v "Icon" /d "\"%EXE%\",0" /f >nul

:: Create a Notepatra ProgId so the app shows with a proper name in
:: "Open with" (rather than just "notepatra.exe").
reg add "HKCU\Software\Classes\Notepatra.Document" /ve /d "Notepatra Document" /f >nul
reg add "HKCU\Software\Classes\Notepatra.Document\DefaultIcon" /ve /d "\"%EXE%\",0" /f >nul
reg add "HKCU\Software\Classes\Notepatra.Document\shell\open\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul

:: Add ourselves to the SupportedTypes list for each extension. This
:: adds Notepatra to the "Open with" menu without making it default —
:: the user picks default via right-click → Open with → Choose another.
set EXTS=.txt .log .md .json .xml .yaml .yml .ini .conf .cfg .csv .py .js .ts .cpp .c .h .hpp .rs .go .java .cs .sql .html .css .sh .ps1 .bat
for %%E in (%EXTS%) do (
    reg add "HKCU\Software\Classes\Applications\notepatra.exe\SupportedTypes" /v "%%E" /d "" /f >nul
    reg add "HKCU\Software\Classes\%%E\OpenWithProgids" /v "Notepatra.Document" /d "" /f >nul
)

echo   Done. Notepatra now appears in right-click "Open with" for the
echo   file types listed above.
echo.
echo   To set Notepatra as DEFAULT for a specific type:
echo     1. Right-click a file of that type
echo     2. "Open with" ^> "Choose another app"
echo     3. Pick Notepatra, check "Always use this app"
echo.
echo   Starting Windows 11, you can also use:
echo     Settings ^> Apps ^> Default apps ^> search for the extension
echo.

pause
