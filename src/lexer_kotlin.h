#ifndef LEXER_KOTLIN_H
#define LEXER_KOTLIN_H

#include <Qsci/qscilexerjava.h>

// Kotlin runs on the JVM and shares a lot of Java's lexical structure
// (// and /* */ comments, "..." strings, decimal/hex literals). We
// subclass QsciLexerJava and override keywords() to swap Java's keyword
// set for Kotlin's (different fundamentals: fun, val, var, data class,
// when, sealed, suspend, plus contextual/soft keywords).
class LexerKotlin : public QsciLexerJava {
public:
    explicit LexerKotlin(QObject *parent = nullptr) : QsciLexerJava(parent) {}
    const char *language() const override { return "Kotlin"; }
    const char *keywords(int set) const override;
};

#endif
