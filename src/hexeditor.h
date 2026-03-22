#ifndef HEXEDITOR_H
#define HEXEDITOR_H

#include <QDialog>
#include <QTextEdit>
#include <QLabel>

class HexEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit HexEditorDialog(const QString &filePath, QWidget *parent = nullptr);

private:
    QTextEdit *m_hexView;
    QLabel *m_infoLabel;
};

#endif
