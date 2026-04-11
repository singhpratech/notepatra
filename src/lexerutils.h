#ifndef LEXERUTILS_H
#define LEXERUTILS_H

#include <QString>

class QObject;
class QsciLexer;

QString detectLanguageFromPath(const QString &path, const QString &text);
QsciLexer *createLexerForLanguage(const QString &language, QObject *parent);

#endif
