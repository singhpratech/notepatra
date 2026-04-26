#ifndef LEXER_POWERSHELL_H
#define LEXER_POWERSHELL_H

#include <Qsci/qscilexer.h>

// PowerShell lexer wrapping Scintilla's built-in SCLEX_POWERSHELL (lexer
// id 88). QScintilla doesn't ship a Qt-side wrapper so we provide one
// ourselves: subclass QsciLexer (the abstract base), report "powershell"
// as the lexer name -- which makes Scintilla load its native PowerShell
// lexer -- then provide keyword sets and per-style descriptions.
//
// PowerShell syntax this enables:
//   - $variable / $env:VAR  (variable style 5)
//   - "double" / 'single' / @"here-string"@   (string styles 2 / 13)
//   - # line comments / <# block comments #>  (styles 1 / 15)
//   - Verb-Noun cmdlets (Get-Process)         (cmdlet style 9)
//   - Comparison operators -eq -ne -lt -gt    (operator style 6)
//   - Keywords if/else/foreach/function       (keyword style 8)
class LexerPowerShell : public QsciLexer {
    Q_OBJECT
public:
    explicit LexerPowerShell(QObject *parent = nullptr);
    ~LexerPowerShell() override = default;

    const char *language() const override;
    const char *lexer() const override;
    const char *keywords(int set) const override;
    QString description(int style) const override;

    // Default per-style colours. These get overridden by Notepatra's
    // theme palette (npp_palette / applyNotepadPlusPalette) when a real
    // theme is applied -- but providing sensible defaults here means
    // the editor doesn't render as monochrome black-on-white if the
    // user opens a .ps1 before any theme has been applied.
    QColor defaultColor(int style) const override;
    bool defaultEolFill(int style) const override;
    QFont defaultFont(int style) const override;
};

#endif
