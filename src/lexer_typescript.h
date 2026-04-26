#ifndef LEXER_TYPESCRIPT_H
#define LEXER_TYPESCRIPT_H

#include <Qsci/qscilexerjavascript.h>

// TypeScript is a JavaScript superset, so QsciLexerJavaScript handles
// all the syntax fine. We override keywords() to add TS-only words like
// interface, type, enum, namespace, readonly, keyof, etc.
class LexerTypeScript : public QsciLexerJavaScript {
public:
    explicit LexerTypeScript(QObject *parent = nullptr) : QsciLexerJavaScript(parent) {}
    const char *language() const override { return "TypeScript"; }
    const char *keywords(int set) const override;
};

#endif
