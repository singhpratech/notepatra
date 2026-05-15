#ifndef LEXERUTILS_H
#define LEXERUTILS_H

#include <QString>

class QObject;
class QsciLexer;
class QsciScintilla;

QString detectLanguageFromPath(const QString &path, const QString &text);
QsciLexer *createLexerForLanguage(const QString &language, QObject *parent);

// v0.1.87 — Save As dialog filter list builder.
//
// Returns a ";;" -separated Qt filter string for QFileDialog::setNameFilters,
// covering every language Notepatra has a dedicated lexer for. Format per
// entry: "Display Name (*.ext1 *.ext2 *.ext3)". "All Files (*)" is always
// first so unfamiliar extensions still save correctly.
//
// If currentLanguage matches one of the entries, the returned `selected`
// out-parameter is set to that entry so the dialog opens with the right
// filter pre-selected (matches Notepad++ behaviour). Pass nullptr if you
// don't care about pre-selection.
//
// Pre-v0.1.87 the save-as dialog passed only "All Files (*)" and the dropdown
// was effectively dead — users couldn't see which extension would be appended.
QString buildSaveAsFilters(const QString &currentLanguage,
                           QString *selectedFilter = nullptr);

// v0.1.87 follow-up — extract the first `*.ext` pattern's extension from a
// Save As filter entry like "Python (*.py *.pyw *.pyx)" → "py" (NO leading
// dot, matches QFileDialog::setDefaultSuffix's expected format). Returns an
// empty string for "All Files (*)" or filename-only filters like
// "Dockerfile (Dockerfile Containerfile)" — neither should auto-append.
QString firstExtensionFromFilter(const QString &filter);

// v0.1.87 follow-up — post-Accept safety net: if the selected filter has at
// least one `*.ext` pattern AND the path ends with NONE of them, append the
// first extension. Used as a backstop in case the platform file dialog
// ignores setDefaultSuffix (some Linux GTK builds do). Returns path unchanged
// when filter is "All Files (*)" or already-matching.
QString applySaveAsFilterSuffix(const QString &path, const QString &filter);

// v0.1.84 — push curated SCI_SETKEYWORDS strings (from sql_keywords.h /
// lang_keywords.h) into the Scintilla editor for the given language string.
// The QScintilla lexers ship small built-in keyword sets that lag modern
// language additions; this fills the gaps without subclassing every lexer.
// Slot numbers vary per lexer — see implementation for the mapping.
void populateExtraKeywords(QsciScintilla *editor, const QString &lang);

#endif
