#ifndef LEXERUTILS_H
#define LEXERUTILS_H

#include <QString>

class QObject;
class QsciLexer;
class QsciScintilla;

QString detectLanguageFromPath(const QString &path, const QString &text);
QsciLexer *createLexerForLanguage(const QString &language, QObject *parent);

// v0.1.84 — push curated SCI_SETKEYWORDS strings (from sql_keywords.h /
// lang_keywords.h) into the Scintilla editor for the given language string.
// The QScintilla lexers ship small built-in keyword sets that lag modern
// language additions; this fills the gaps without subclassing every lexer.
// Slot numbers vary per lexer — see implementation for the mapping.
void populateExtraKeywords(QsciScintilla *editor, const QString &lang);

#endif
