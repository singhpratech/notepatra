#ifndef LEXER_RUST_H
#define LEXER_RUST_H

#include <Qsci/qscilexercpp.h>

// QScintilla doesn't ship a Rust lexer, but Rust shares enough of C++'s
// punctuation grammar (// and /* */ comments, " strings, numeric literals,
// braces) that subclassing QsciLexerCPP and overriding keywords() gives a
// 95%-correct Rust experience for free. The only visible difference vs
// "real" Rust syntax highlighting is that raw strings r"..." are styled
// as identifiers instead of strings -- a small price for shipping today.
class LexerRust : public QsciLexerCPP {
public:
    explicit LexerRust(QObject *parent = nullptr) : QsciLexerCPP(parent) {}
    const char *language() const override { return "Rust"; }
    const char *keywords(int set) const override;
};

#endif
