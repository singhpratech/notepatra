; Notepatra Windows installer (NSIS MUI2)
;
; Builds notepatra-setup.exe which:
;   - Installs notepatra.exe + Qt DLLs to %LOCALAPPDATA%\Notepatra (user-level,
;     no admin prompt) or Program Files (admin-level) based on user choice
;   - Writes HKCU uninstall registry key so it appears in "Installed apps"
;   - Generates uninstall.exe that fully removes files + registry + shortcuts
;   - Creates Start Menu + optional Desktop shortcuts
;   - Sets DisplayIcon / Publisher / HelpLink / URLInfoAbout metadata
;
; Build command (CI):
;   makensis /DVERSION=0.1.4 /DSOURCE_DIR=notepatra-win installers\windows.nsi
;
; Output: notepatra-setup-0.1.4.exe in the current directory

!ifndef VERSION
    !define VERSION "0.0.0"
!endif
!ifndef SOURCE_DIR
    !define SOURCE_DIR "notepatra-win"
!endif

!define APP_NAME "Notepatra"
!define APP_PUBLISHER "Prateek Singh"
!define APP_URL "https://notepatra.org"
!define APP_HELP_URL "https://github.com/singhpratech/notepatra"
!define APP_EXE "notepatra.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

; ─── Installer metadata shown in Windows Explorer right-click → Properties ───
Name "${APP_NAME} ${VERSION}"
Caption "${APP_NAME} ${VERSION} Setup"
BrandingText "Notepatra — native code editor for Linux, macOS, Windows"
OutFile "notepatra-setup-${VERSION}.exe"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName"     "${APP_NAME}"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "FileDescription" "${APP_NAME} Installer"
VIAddVersionKey "FileVersion"     "${VERSION}"
VIAddVersionKey "CompanyName"     "${APP_PUBLISHER}"
VIAddVersionKey "LegalCopyright"  "© ${APP_PUBLISHER}. GPL-3.0 licensed."

; Install per-user by default — no UAC prompt
InstallDir "$LOCALAPPDATA\${APP_NAME}"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma

; ─── MUI2 modern UI ───────────────────────────────────────────────────
!include "MUI2.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON "..\resources\notepatra.ico"
!define MUI_UNICON "..\resources\notepatra.ico"

; Install pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Notepatra"
!define MUI_FINISHPAGE_LINK "Visit notepatra.org"
!define MUI_FINISHPAGE_LINK_LOCATION "${APP_URL}"
!insertmacro MUI_PAGE_FINISH

; Uninstall pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ─── Install sections ─────────────────────────────────────────────────
Section "Notepatra (required)" SecMain
    SectionIn RO  ; required, cannot be deselected

    SetOutPath "$INSTDIR"

    ; Bundle everything from notepatra-win/ — this is the output of
    ; the Windows build job (notepatra.exe + Qt DLLs + platforms/ + styles/)
    File /r "${SOURCE_DIR}\*.*"

    ; Calculate installed size for the "Installed apps" entry
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0

    ; ─── Registry: Uninstall entry (shows in Settings → Apps → Installed) ─
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName"     "${APP_NAME}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion"  "${VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher"       "${APP_PUBLISHER}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon"     "$INSTDIR\${APP_EXE}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout"    "${APP_URL}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "HelpLink"        "${APP_HELP_URL}"
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" "$0"
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1

    ; Generate uninstall.exe next to the app
    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Start Menu shortcut" SecStartMenu
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" \
        "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk" \
        "$INSTDIR\uninstall.exe"
SectionEnd

Section "Desktop shortcut" SecDesktop
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" \
        "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
SectionEnd

Section "Add to PATH" SecPath
    ; Prepend install dir to user PATH so `notepatra` works from any terminal
    Push "$INSTDIR"
    Call AddToUserPath
SectionEnd

; ─── Uninstall section ────────────────────────────────────────────────
Section "Uninstall"
    ; Remove from PATH
    Push "$INSTDIR"
    Call un.RemoveFromUserPath

    ; Remove shortcuts
    Delete "$DESKTOP\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"

    ; Remove files — everything under $INSTDIR
    RMDir /r "$INSTDIR"

    ; Remove registry entries
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
SectionEnd

; ─── Section descriptions shown in the Components page ────────────────
LangString DESC_SecMain       ${LANG_ENGLISH} "Install Notepatra and Qt runtime DLLs. Required."
LangString DESC_SecStartMenu  ${LANG_ENGLISH} "Create Start Menu shortcuts under Notepatra."
LangString DESC_SecDesktop    ${LANG_ENGLISH} "Create a Notepatra shortcut on the Desktop."
LangString DESC_SecPath       ${LANG_ENGLISH} "Add Notepatra to the user PATH so 'notepatra' works from any terminal."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain}      $(DESC_SecMain)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} $(DESC_SecStartMenu)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop}   $(DESC_SecDesktop)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecPath}      $(DESC_SecPath)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ─── Helpers: PATH manipulation (user scope, no admin) ────────────────
!include "LogicLib.nsh"
!include "FileFunc.nsh"

; Push "$INSTDIR" then Call AddToUserPath
Function AddToUserPath
    Exch $0
    Push $1
    Push $2
    ReadRegStr $1 HKCU "Environment" "PATH"
    ${If} $1 == ""
        StrCpy $2 "$0"
    ${Else}
        ; Bail if already present
        Push "$1;"
        Push "$0;"
        Call StrContains
        Pop $2
        ${If} $2 != ""
            ; Already in PATH, skip
            Pop $2
            Pop $1
            Pop $0
            Return
        ${EndIf}
        StrCpy $2 "$0;$1"
    ${EndIf}
    WriteRegExpandStr HKCU "Environment" "PATH" "$2"
    ; Broadcast WM_SETTINGCHANGE so already-open shells see the change
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
    Pop $2
    Pop $1
    Pop $0
FunctionEnd

Function un.RemoveFromUserPath
    Exch $0
    Push $1
    ReadRegStr $1 HKCU "Environment" "PATH"
    ${If} $1 != ""
        ; Remove "$0;" and ";$0" from PATH
        ${un.WordReplace} "$1" "$0;" "" "+" $1
        ${un.WordReplace} "$1" ";$0" "" "+" $1
        ${un.WordReplace} "$1" "$0"  "" "+" $1
        WriteRegExpandStr HKCU "Environment" "PATH" "$1"
        SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
    ${EndIf}
    Pop $1
    Pop $0
FunctionEnd

; String-contains helper: push haystack, needle; pops result (empty = not found)
Function StrContains
    Exch $R1  ; needle
    Exch
    Exch $R2  ; haystack
    Push $R3
    Push $R4
    Push $R5
    StrLen $R3 $R1
    StrCpy $R4 0
    StrCpy $R5 ""
    loop:
        StrCpy $R5 $R2 $R3 $R4
        StrCmp $R5 $R1 done
        StrCmp $R5 "" done
        IntOp $R4 $R4 + 1
        Goto loop
    done:
    StrCpy $R1 $R5
    Pop $R5
    Pop $R4
    Pop $R3
    Pop $R2
    Exch $R1
FunctionEnd

; NSIS stdlib's WordReplace is in WordFunc.nsh
!include "WordFunc.nsh"
!insertmacro WordReplace
!insertmacro un.WordReplace
