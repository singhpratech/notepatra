// SPDX-License-Identifier: GPL-3.0-or-later

#include "lexer_powershell.h"

LexerPowerShell::LexerPowerShell(QObject *parent) : QsciLexer(parent) {}

const char *LexerPowerShell::language() const { return "PowerShell"; }

// Scintilla's internal lexer name. SCI_SETLEXERLANGUAGE("powershell")
// loads SCLEX_POWERSHELL (88), Scintilla's native PowerShell lexer.
const char *LexerPowerShell::lexer() const { return "powershell"; }

// Comprehensive keyword sets for SCLEX_POWERSHELL.
// Sources verified against:
//   - Microsoft "about_Reserved_Words" / "about_Language_Keywords"
//   - PowerShell Language Specification chapter 02 (lang-spec)
//   - Microsoft "Approved Verbs for Windows PowerShell Commands" doc
//   - VS Code PowerShell extension default token mapping
const char *LexerPowerShell::keywords(int set) const {
    // Scintilla's SCLEX_POWERSHELL exposes 6 keyword wordlists, indexed 0..5:
    //   idx 0 = PowerShell Keywords (begin/break/while/...)
    //   idx 1 = Cmdlets (full Verb-Noun names like New-Object)
    //   idx 2 = Aliases (ls, gci, ...)
    //   idx 3 = Functions (user-defined function names — usually empty)
    //   idx 4 = User1 (custom — we use for .NET type names in [type] literals)
    //   idx 5 = DocComment keywords
    //
    // QScintilla's `keywords(int set)` API is 1-BASED. Scintilla idx N gets
    // the string returned by `keywords(N+1)`. Pre-v0.1.32 this file mis-
    // mapped the sets — operators landed in idx 1 (Cmdlets), the verb list
    // landed in idx 2 (Aliases), full Verb-Noun names landed in idx 4
    // (User1) instead of idx 1 (Cmdlets). Result: `New-Object` etc. were
    // styled as User1 (style 12), which the palette didn't theme, so they
    // rendered as default text — exactly the bug the user reported on Windows.
    if (set == 1) {
        // Scintilla idx 0 = PowerShell Keywords
        return
            "begin break catch class configuration continue data define do "
            "dynamicparam else elseif end enum exit filter finally for "
            "foreach from function hidden if in inlinescript local param "
            "parallel private process return sequence static switch throw "
            "trap try until using var while workflow";
    }
    if (set == 2) {
        // Scintilla idx 1 = Cmdlets — full Verb-Noun names AND the bare verb
        // prefixes from the Microsoft Approved Verbs list. Listing both means
        // Scintilla colours `New-Object`, `Get-Item`, etc. as
        // SCE_POWERSHELL_CMDLET (style 9). The palette renders style 9 as
        // pure blue (`#0000FF`) on light, matching Microsoft's PowerShell ISE
        // convention exactly.
        return
            // Most-used full Verb-Noun cmdlet names — Object/Process/Service
            "Where-Object ForEach-Object Select-Object Sort-Object "
            "Group-Object Measure-Object Compare-Object Tee-Object "
            "New-Object Get-Member "
            "Get-Process Get-Service Get-ChildItem Get-Content Get-Item "
            "Get-ItemProperty Get-Location Get-History Get-Command Get-Help "
            "Get-Variable Get-Date Get-Random Get-Host Get-Module "
            "Set-Location Set-Item Set-ItemProperty Set-Variable "
            "Set-Content Set-StrictMode Set-ExecutionPolicy "
            "New-Item New-Variable New-Module New-Alias "
            "Remove-Item Remove-Variable Remove-Module "
            "Copy-Item Move-Item Rename-Item "
            "Test-Path Resolve-Path Split-Path Join-Path Convert-Path "
            "Invoke-Expression Invoke-Command Invoke-WebRequest Invoke-RestMethod "
            "Out-Host Out-Null Out-File Out-String Out-GridView "
            "Write-Host Write-Output Write-Error Write-Warning "
            "Write-Verbose Write-Debug Write-Information Write-Progress "
            "Read-Host "
            "Import-Module Export-ModuleMember Import-Csv Export-Csv "
            "ConvertTo-Json ConvertFrom-Json ConvertTo-Xml Select-String "
            "Start-Process Stop-Process Start-Sleep Start-Job Stop-Job "
            "Get-Job Receive-Job Wait-Job "
            "Format-Table Format-List Format-Wide Format-Custom "
            // Microsoft Approved Verbs (Common / Communications / Data /
            // Diagnostic / Lifecycle / Security / Other). The verb-prefix
            // catches any Verb-Anything cmdlet not listed by full name above.
            "Add Clear Close Copy Enter Exit Find Format Get Hide Join "
            "Lock Move New Open Optimize Pop Push Redo Remove Rename Reset "
            "Resize Search Select Set Show Skip Split Step Switch Undo "
            "Unlock Watch "
            "Connect Disconnect Read Receive Send Write "
            "Backup Checkpoint Compare Compress Convert ConvertFrom "
            "ConvertTo Dismount Edit Expand Export Group Import Initialize "
            "Limit Merge Mount Out Publish Restore Save Sync Unpublish "
            "Update "
            "Debug Measure Ping Repair Resolve Test Trace "
            "Approve Assert Build Complete Confirm Deny Deploy Disable "
            "Enable Install Invoke Register Request Restart Resume Start "
            "Stop Submit Suspend Uninstall Unregister Wait "
            "Block Grant Protect Revoke Unblock Unprotect "
            "Use";
    }
    if (set == 3) {
        // Scintilla idx 2 = Aliases — daily-driver short forms from
        // `Get-Alias` on a default Windows PowerShell 5.1 / 7 install.
        return
            "ac asnp cat cd chdir clc clear clhy cli clp cls clv cnsn "
            "compare copy cpi cpp curl cvpa dbp del diff dir dnsn ebp echo "
            "epal epcsv epsn erase etsn exsn fc fhx fl foreach ft fw gal "
            "gbp gc gci gcm gcs gdr ghy gi gjb gl gm gmo gp gps gpv group "
            "gsn gsnp gsv gu gv gwmi h history icm iex ihy ii ipal ipcsv "
            "ipmo ipsn irm ise iwmi iwr kill lp ls man md measure mi mount "
            "move mp mv nal ndr ni nmo npssc nsn nv ogv oh popd ps pushd "
            "pwd r rbp rcjb rcsn rd rdr ren ri rjb rm rmdir rmo rni rnp rp "
            "rsn rsnp rujb rv rvpa rwmi sajb sal saps sasv sbp sc select "
            "set shcm si sl sleep sls sort sp spjb spps spsv start sujb sv "
            "swmi tee trcm type wget where wjb write";
    }
    if (set == 5) {
        // Scintilla idx 4 = User1 — common .NET type names so `[string]`,
        // `[int]`, `[datetime]`, `[hashtable]` etc. pick up a distinct
        // colour when bracketed. Doubles as a hint for type-test contexts.
        return
            "string int int32 int64 long short byte double float decimal "
            "char bool object void array hashtable pscustomobject "
            "scriptblock datetime timespan guid regex xml "
            "system.io.file system.io.directory system.io.path "
            "system.text.encoding system.management.automation";
    }
    return nullptr;
}

QString LexerPowerShell::description(int style) const {
    // Style numbers from Scintilla/include/SciLexer.h, the SCE_POWERSHELL_*
    // enum. Names chosen to match the keyword-matching logic in
    // src/npp_palette.cpp (which looks for "variable", "cmdlet", "alias",
    // "here-string", "comment doc keyword" substrings to apply colour).
    switch (style) {
        case  0: return "Default";
        case  1: return "Comment";
        case  2: return "String";
        case  3: return "Character";
        case  4: return "Number";
        case  5: return "Variable";
        case  6: return "Operator";
        case  7: return "Identifier";
        case  8: return "Keyword";
        case  9: return "Cmdlet";
        case 10: return "Alias";
        case 11: return "Function";
        case 12: return "User-defined word 1";
        case 13: return "Comment stream";
        case 14: return "Here-string";
        case 15: return "Here-character";
        case 16: return "Comment doc keyword";
        case 17: return "Comment doc keyword error";
        default: return QString();
    }
}

QColor LexerPowerShell::defaultColor(int style) const {
    // Sensible "fallback" palette in case no Notepatra theme has been
    // applied yet. Real styling comes from npp_palette.cpp which detects
    // language() == "PowerShell" and applies the VS Code PS extension
    // colours (variable blue, cmdlet amber, alias purple, etc.).
    switch (style) {
        case  1: case 13: case 16: return QColor("#008000"); // green comments
        case 17: return QColor("#FF0000"); // red doc-keyword error
        case  2: case 14: return QColor("#A31515"); // red strings
        case  3: case 15: return QColor("#A31515"); // red chars
        case  4: return QColor("#098658"); // dark-green numbers
        case  5: return QColor("#001080"); // dark-blue variables
        case  6: return QColor("#000000"); // black operators
        case  8: return QColor("#0000FF"); // blue keywords
        case  9: return QColor("#795E26"); // amber cmdlets
        case 10: return QColor("#AF00DB"); // purple aliases
        case 11: return QColor("#795E26"); // amber functions
        case 12: return QColor("#267F99"); // teal user1 (cmdlets full name)
        default: return QsciLexer::defaultColor(style);
    }
}

bool LexerPowerShell::defaultEolFill(int style) const {
    // Here-strings and stream comments cover whole regions; eol-fill
    // makes their background extend to end of line for readability.
    return style == 13 || style == 14 || style == 15;
}

QFont LexerPowerShell::defaultFont(int style) const {
    QFont f = QsciLexer::defaultFont(style);
    if (style == 1 || style == 13 || style == 16) f.setItalic(true); // italic comments
    return f;
}
