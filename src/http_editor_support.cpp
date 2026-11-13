// SPDX-License-Identifier: GPL-3.0-or-later

#include "http_editor_support.h"

#include "editor.h"
#include "http_file.h"
#include "theme_detect.h"

#include <QAction>
#include <QFileInfo>
#include <QMenu>

namespace {
constexpr int kMaxScanChars = 8 * 1024 * 1024;  // markers only; Send still parses on demand
}

HttpEditorSupport *HttpEditorSupport::ensure(Editor *ed, bool *created) {
    if (created) *created = false;
    if (!ed) return nullptr;
    if (auto *s = ed->findChild<HttpEditorSupport *>(QString(), Qt::FindDirectChildrenOnly))
        return s;
    if (created) *created = true;
    return new HttpEditorSupport(ed);
}

bool HttpEditorSupport::isHttpPath(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("http") || suffix == QLatin1String("rest");
}

HttpEditorSupport::HttpEditorSupport(Editor *ed) : QObject(ed), m_ed(ed) {
    m_rescan.setSingleShot(true);
    m_rescan.setInterval(250);
    connect(&m_rescan, &QTimer::timeout, this, &HttpEditorSupport::rescanNow);
    connect(ed, &QsciScintilla::textChanged, this, [this]() {
        if (m_active) m_rescan.start();
    });
    connect(ed, &Editor::documentIdentityChanged, this, &HttpEditorSupport::refreshIdentity);
    connect(ed, &QsciScintilla::marginClicked, this, &HttpEditorSupport::onMarginClicked);
    connect(ed, &Editor::contextMenuAboutToShow, this, &HttpEditorSupport::onContextMenu);
    refreshIdentity();
}

void HttpEditorSupport::refreshMarkerColors() {
    if (!m_active) return;
    // QsciScintilla's QColor API converts to Scintilla's BGR COLORREF itself.
    const QColor c(npPalette().successFg);
    m_ed->setMarkerBackgroundColor(c, kMarker);
    m_ed->setMarkerForegroundColor(c.darker(130), kMarker);
}

void HttpEditorSupport::refreshIdentity() {
    const bool want = isHttpPath(m_ed->filePath());
    if (want == m_active) return;
    m_active = want;
    if (m_active) {
        m_ed->markerDefine(QsciScintilla::RightTriangle, kMarker);
        m_ed->setMarginType(kMargin, QsciScintilla::SymbolMargin);
        m_ed->setMarginWidth(kMargin, 16);
        m_ed->setMarginSensitivity(kMargin, true);
        m_ed->setMarginMarkerMask(kMargin, 1 << kMarker);
        refreshMarkerColors();
        rescanNow();
    } else {
        m_rescan.stop();
        m_ed->markerDeleteAll(kMarker);
        m_ed->setMarginWidth(kMargin, 0);
        m_ed->setMarginSensitivity(kMargin, false);
    }
    emit activeChanged(m_active);
}

void HttpEditorSupport::rescanNow() {
    m_rescan.stop();
    if (!m_active) return;
    m_ed->markerDeleteAll(kMarker);
    const QString text = m_ed->text();
    if (text.size() > kMaxScanChars) return;
    for (const HttpFile::Request &r : HttpFile::parse(text).requests)
        m_ed->markerAdd(r.requestLine, kMarker);
}

void HttpEditorSupport::onMarginClicked(int margin, int line, Qt::KeyboardModifiers) {
    if (!m_active || margin != kMargin) return;
    if (m_ed->markersAtLine(line) & (1 << kMarker)) emit sendRequested(m_ed, line);
}

void HttpEditorSupport::onContextMenu(QMenu *menu, const QPoint &pos) {
    if (!m_active || !menu) return;
    int line = -1, col = 0;
    const long p = m_ed->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMPOINT,
                                       (unsigned long)pos.x(), (long)pos.y());
    if (p >= 0) m_ed->lineIndexFromPosition(int(p), &line, &col);
    if (line < 0) m_ed->getCursorPosition(&line, &col);

    const bool onRequest = HttpFile::parse(m_ed->text()).requestAtLine(line) >= 0;
    auto *act = new QAction(tr("Send Request"), menu);
    act->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+R")));
    act->setEnabled(onRequest);
    act->setObjectName(QStringLiteral("ctxSendHttpRequest"));
    connect(act, &QAction::triggered, this, [this, line]() { emit sendRequested(m_ed, line); });
    QAction *first = menu->actions().value(0);
    menu->insertAction(first, act);
    menu->insertSeparator(first);
}
