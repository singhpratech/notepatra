// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LEXER_PLAINTEXT_H
#define LEXER_PLAINTEXT_H

#include <Qsci/qscilexercustom.h>

// v0.1.84 — Custom QsciLexerCustom subclass for .txt files.
//
// Plain text has no formal grammar — we paint a few useful patterns
// (URLs, emails, numbers, all-caps headings, quoted strings, inline
// backtick code) over otherwise-default-styled prose. The goal is
// subtle visual structure, not parsing.
//
// Why QsciLexerCustom (not QsciLexer)?
//   QsciLexer wraps Scintilla's built-in lexers via lexer()/keywords();
//   QsciLexerCustom skips Scintilla's lexer pipeline entirely and lets
//   us drive the styling buffer ourselves from styleText(). Right
//   choice for "regex-paint a few patterns" — no token state machine.
//
// Style IDs (kept stable; npp_palette.cpp themes them by description()):
//   0  Default     unmatched prose
//   1  URL         https?:// and www. links
//   2  Email       user@host.tld
//   3  Number      bare integers/floats, $-prefixed currency
//   4  Heading     ALL-CAPS line of 3+ words
//   5  String      "double-quoted" (line-bounded, ≤80 char)
//   6  String alt  'single-quoted' (line-bounded, ≤80 char)
//   7  Code        `backtick-inline` (line-bounded, ≤80 char)
class LexerPlainText : public QsciLexerCustom {
    Q_OBJECT
public:
    explicit LexerPlainText(QObject *parent = nullptr);
    ~LexerPlainText() override = default;

    const char *language() const override { return "Plain Text"; }
    QString description(int style) const override;
    void styleText(int start, int end) override;
};

#endif
