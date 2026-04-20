#include "hexeditor.h"
#include "fonts.h"
#include "config.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFile>
#include <QFileInfo>
#include <QFont>

namespace {
static bool hexIsDark() {
    const QString &t = Config::instance().theme;
    return t.compare("Dark", Qt::CaseInsensitive) == 0 ||
           t.compare("Monokai", Qt::CaseInsensitive) == 0;
}
}

HexEditorDialog::HexEditorDialog(const QString &filePath, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Hex Editor — " + QFileInfo(filePath).fileName());
    resize(850, 600);

    auto *layout = new QVBoxLayout(this);
    const bool dark = hexIsDark();

    // Info
    m_infoLabel = new QLabel;
    m_infoLabel->setStyleSheet(QString(
        "font-weight: bold; padding: 4px; background: %1; color: %2;")
        .arg(dark ? "#252526" : "#F0F0F0",
             dark ? "#D4D4D4" : "#141413"));
    layout->addWidget(m_infoLabel);

    // Hex view — always monospace on a dark-ish canvas regardless of
    // theme, because the byte-color palette (printable / NUL / newline /
    // non-printable) is tuned for dark readability. On Light theme we
    // switch to a lighter ivory canvas and flip the NUL grey.
    m_hexView = new QTextEdit;
    m_hexView->setReadOnly(true);
    QFont mono = notepatraCodeFont();
    m_hexView->setFont(mono);
    m_hexView->setStyleSheet(QString(
        "QTextEdit { background: %1; color: %2; border: none; }")
        .arg(dark ? "#1E1E1E" : "#FAF9F5",
             dark ? "#D4D4D4" : "#141413"));
    m_hexView->setLineWrapMode(QTextEdit::NoWrap);
    layout->addWidget(m_hexView, 1);

    // Close button
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *closeBtn = new QPushButton("Close");
    closeBtn->setFixedWidth(100);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    // Load file
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_infoLabel->setText("Error: Cannot open file");
        return;
    }

    qint64 fileSize = file.size();
    qint64 maxRead = qMin(fileSize, (qint64)(1024 * 1024)); // 1 MB max for hex view
    QByteArray data = file.read(maxRead);

    m_infoLabel->setText(QString("File: %1 | Size: %2 bytes | Showing: %3 bytes")
                         .arg(filePath)
                         .arg(fileSize)
                         .arg(data.size()));

    // Build hex dump
    QString hexDump;
    hexDump.reserve(data.size() * 5);

    for (int offset = 0; offset < data.size(); offset += 16) {
        // Offset
        QString line = QString("<span style='color:#569CD6;'>%1</span>  ")
                       .arg(offset, 8, 16, QChar('0'));

        // Hex bytes
        QString hexPart, asciiPart;
        for (int j = 0; j < 16; j++) {
            if (offset + j < data.size()) {
                unsigned char byte = (unsigned char)data[offset + j];
                QString color;
                if (byte == 0x00) color = "#555555";
                else if (byte == 0x0A || byte == 0x0D) color = "#4EC9B0"; // newlines
                else if (byte >= 0x20 && byte < 0x7F) color = "#D4D4D4"; // printable
                else color = "#CE9178"; // non-printable

                hexPart += QString("<span style='color:%1;'>%2</span> ")
                           .arg(color)
                           .arg(byte, 2, 16, QChar('0'));

                // ASCII
                if (byte >= 0x20 && byte < 0x7F)
                    asciiPart += QString(QChar(byte)).toHtmlEscaped();
                else
                    asciiPart += "<span style='color:#555;'>.</span>";
            } else {
                hexPart += "   ";
                asciiPart += " ";
            }
            if (j == 7) hexPart += " "; // gap between 8-byte groups
        }

        line += hexPart + " <span style='color:#808080;'>|</span> " + asciiPart;
        hexDump += line + "<br>";
    }

    if (fileSize > maxRead) {
        hexDump += QString("<br><span style='color:#DCDCAA;'>[Showing first %1 of %2 bytes]</span>")
                   .arg(maxRead).arg(fileSize);
    }

    m_hexView->setHtml("<pre>" + hexDump + "</pre>");
}
