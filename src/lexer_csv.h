#ifndef LEXER_CSV_H
#define LEXER_CSV_H

// v0.1.84 — custom Scintilla-compatible CSV / TSV lexer.
//
// QScintilla ships no native lexer for comma-separated tables, so before
// v0.1.84 .csv / .tsv files opened as plain text (monochrome). This class
// is a QsciLexerCustom subclass that paints:
//   - the header row in bold (style 1)
//   - data cells in alternating "Column A" / "Column B" stripes
//     (styles 2 / 3) to give a striped-table visual
//   - separators in operator colour (style 4)
//   - quoted fields including "" escapes as strings (style 5)
//   - purely numeric cells in number colour (style 6)
//   - `#`-prefixed comment lines in italic (style 7)
//
// The description() strings ("Header", "Column A", "Column B", "Separator",
// "Quoted", "Number", "Comment") feed into npp_palette.cpp's matcher chain
// so the theme system can map them to colours like every other lexer.
//
// Separator defaults to ',' for CSV; lexerutils.cpp may set '\t' for TSV
// via setSeparator() — kept as a simple QChar field so callers don't need
// to switch lexer classes for the two formats.

#include <Qsci/qscilexercustom.h>
#include <QChar>

class LexerCsv : public QsciLexerCustom {
    Q_OBJECT
public:
    explicit LexerCsv(QObject *parent = nullptr);
    ~LexerCsv() override = default;

    const char *language() const override { return "CSV"; }
    QString description(int style) const override;
    void styleText(int start, int end) override;

    // Allow lexerutils.cpp to flip to TSV by setting '\t' before attaching
    // the lexer to the editor. Default is ',' so plain CSV Just Works.
    void setSeparator(QChar sep) { m_separator = sep; }
    QChar separator() const { return m_separator; }

private:
    QChar m_separator = QLatin1Char(',');
};

#endif
