@echo off
:: Notepatra — remove file associations registered by register-associations.bat

echo.
echo   Removing Notepatra from Windows "Open with" menu (HKCU only)...
echo.

:: Remove the main app registration
reg delete "HKCU\Software\Classes\Applications\notepatra.exe" /f >nul 2>&1
reg delete "HKCU\Software\Classes\Notepatra.Document" /f >nul 2>&1

:: Remove from per-extension OpenWithProgids — we don't blow away the
:: whole key because other apps may also be registered for that ext.
set EXTS=.txt .log .md .json .xml .yaml .yml .ini .conf .cfg .csv .py .js .ts .cpp .c .h .hpp .rs .go .java .cs .sql .html .css .sh .ps1 .bat
for %%E in (%EXTS%) do (
    reg delete "HKCU\Software\Classes\%%E\OpenWithProgids" /v "Notepatra.Document" /f >nul 2>&1
)

:: Remove the shell context-menu entries (Edit with Notepatra / Open in Notepatra)
reg delete "HKCU\Software\Classes\*\shell\Edit with Notepatra" /f >nul 2>&1
reg delete "HKCU\Software\Classes\Directory\shell\Open in Notepatra" /f >nul 2>&1
reg delete "HKCU\Software\Classes\Directory\Background\shell\Open in Notepatra" /f >nul 2>&1

echo   Done. Windows file-type associations + shell context menu entries cleaned.
echo   (Files you edited and the unzipped notepatra.exe folder are untouched.)
echo.
pause
