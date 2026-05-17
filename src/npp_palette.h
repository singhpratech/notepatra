// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NPP_PALETTE_H
#define NPP_PALETTE_H

#include <QFont>
class QsciLexer;

// Paint the Notepad++ default stylers.xml palette onto any QScintilla lexer.
// Keywords blue bold, comments green italic, numbers orange, strings gray,
// operators bold black, preprocessor brown bold, regex purple.
//
// Works across all 40+ lexers by iterating over lexer->description(i) and
// matching keyword/comment/number/string/operator substrings, so no per-lexer
// style constants are hard-coded.
void applyNotepadPlusPalette(QsciLexer *lexer, const QFont &baseFont,
                             const QString &themeName = QString());

#endif
