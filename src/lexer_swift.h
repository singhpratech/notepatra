// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LEXER_SWIFT_H
#define LEXER_SWIFT_H

#include <Qsci/qscilexercpp.h>

// Swift uses // and /* */ comments, " strings, """...""" multiline strings,
// numeric literals similar to C. QsciLexerCPP handles all the punctuation
// so we just override keywords with Swift's larger set (declarations,
// statements, expressions, and pattern keywords).
class LexerSwift : public QsciLexerCPP {
public:
    explicit LexerSwift(QObject *parent = nullptr) : QsciLexerCPP(parent) {}
    const char *language() const override { return "Swift"; }
    const char *keywords(int set) const override;
};

#endif
