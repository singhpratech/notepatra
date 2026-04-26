#include "lexer_powershell.h"

LexerPowerShell::LexerPowerShell(QObject *parent) : QsciLexer(parent) {}

const char *LexerPowerShell::language() const { return "PowerShell"; }

// Scintilla's internal lexer name. SCI_SETLEXERLANGUAGE("powershell")
// loads SCLEX_POWERSHELL (88), Scintilla's native PowerShell lexer.
const char *LexerPowerShell::lexer() const { return "powershell"; }

const char *LexerPowerShell::keywords(int set) const {
    // Scintilla's PowerShell lexer accepts 5 keyword sets:
    //   set 1: language statement keywords        -> SCE_POWERSHELL_KEYWORD (8)
    //   set 2: comparison / logical operators     -> styles overridden 6/8
    //   set 3: cmdlet verbs                       -> SCE_POWERSHELL_CMDLET (9)
    //   set 4: cmdlet aliases (ls, dir, gci ...)  -> SCE_POWERSHELL_ALIAS (10)
    //   set 5: user-defined words 1               -> SCE_POWERSHELL_USER1 (12)
    if (set == 1) {
        return
            // Statement / control-flow keywords
            "begin break catch class continue data define do "
            "dynamicparam else elseif end enum exit filter finally for "
            "foreach from function hidden if in inlinescript param "
            "parallel process return sequence static switch throw trap "
            "try until using var while workflow "
            // Type-related keywords
            "configuration interface namespace public private protected "
            // Boolean / null literals
            "true false null";
    }
    if (set == 2) {
        // PowerShell comparison and logical operators -- always prefixed
        // with a dash in actual code (-eq, -ne, etc.) but the lexer's
        // operator-recogniser strips the dash and matches the bare word.
        return
            // Comparison
            "eq ne ge gt lt le like notlike match notmatch contains "
            "notcontains in notin replace ireplace creplace ieq cne "
            "ceq ile cle ige cge ilt clt igt cgt "
            // Logical
            "and or not band bor bxor bnot xor "
            // Type tests
            "is isnot as "
            // Other
            "split join f";
    }
    if (set == 3) {
        // The 50 most common cmdlet verbs from the official PowerShell
        // verb list. Cmdlets are formatted Verb-Noun, so this matches
        // the prefix of every standard cmdlet.
        return
            "Add Approve Assert Backup Block Build Checkpoint Clear "
            "Close Compare Complete Compress Confirm Connect Convert "
            "ConvertFrom ConvertTo Copy Debug Deny Disable Disconnect "
            "Dismount Edit Enable Enter Exit Expand Export Find "
            "Format Get Grant Group Hide Import Initialize Install "
            "Invoke Join Limit Lock Measure Merge Mount Move New Open "
            "Optimize Out Ping Pop Publish Push Read Receive Redo "
            "Register Remove Rename Repair Request Reset Resize "
            "Resolve Restart Restore Resume Revoke Save Search Select "
            "Send Set Show Skip Split Start Step Stop Submit Suspend "
            "Switch Sync Test Trace Unblock Undo Uninstall Unlock "
            "Unprotect Unpublish Unregister Update Use Wait Watch Write";
    }
    if (set == 4) {
        // Standard cmdlet aliases users type all day.
        return
            // Filesystem
            "ls dir gci cd cp copy mv move rm del erase rd rmdir "
            "cat type gc more pwd gl pushd popd "
            // Process / object pipeline
            "ps gps gsv kill spps echo write "
            "where ? foreach % select sort group measure "
            // Common
            "cls clear man help "
            "iex icm ipmo ipsn ipal "
            "Get-Help Get-Command Get-Member "
            "Where-Object ForEach-Object Select-Object Sort-Object "
            "Group-Object Measure-Object Out-Host Out-Null Write-Host "
            "Write-Output Write-Error Write-Warning Write-Verbose";
    }
    return nullptr;
}

QString LexerPowerShell::description(int style) const {
    // Style numbers from Scintilla/scintilla/include/SciLexer.h, the
    // SCE_POWERSHELL_* enum.
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
    // applied yet. Real styling comes from npp_palette.cpp.
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
        case 10: return QColor("#0070C1"); // medium blue aliases
        case 11: return QColor("#795E26"); // amber functions (same as cmdlets)
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
