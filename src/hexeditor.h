#ifndef HEXEDITOR_H
#define HEXEDITOR_H

#include <QDialog>
#include <QTextEdit>
#include <QLabel>

class HexEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit HexEditorDialog(const QString &filePath, QWidget *parent = nullptr);

public slots:
    // Re-apply the info-label strip + hex-view canvas stylesheet when
    // MainWindow emits themeChanged(). The hex-dump HTML itself is
    // pre-rendered (per-byte colours baked at open) so we leave it alone;
    // only the chrome swaps palettes.
    void onThemeChanged();

private:
    void applyPalette();
    QTextEdit *m_hexView;
    QLabel *m_infoLabel;
};

#endif
