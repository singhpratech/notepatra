#ifndef LEXER_GO_H
#define LEXER_GO_H

#include <Qsci/qscilexercpp.h>

// Go uses // and /* */ comments, " strings, ` raw strings, similar number
// literals to C, so QsciLexerCPP is a solid base. We only need to override
// keywords with Go's much smaller (25 keyword) language set + the built-in
// types, functions, and predeclared identifiers.
class LexerGo : public QsciLexerCPP {
public:
    explicit LexerGo(QObject *parent = nullptr) : QsciLexerCPP(parent) {}
    const char *language() const override { return "Go"; }
    const char *keywords(int set) const override;
};

#endif
