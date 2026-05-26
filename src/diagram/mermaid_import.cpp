// SPDX-License-Identifier: GPL-3.0-or-later

#include "mermaid_import.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

namespace Npd {
namespace {

QString unquote(QString s) {
    s = s.trimmed();
    if (s.size() >= 2 && ((s.startsWith('"') && s.endsWith('"')) ||
                          (s.startsWith('\'') && s.endsWith('\''))))
        return s.mid(1, s.size() - 2);
    return s;
}

bool isIdChar(QChar c) { return c.isLetterOrNumber() || c == '_' || c == '-'; }

// Parse a Mermaid node token ("A", "A[Label]", "B([x])", "C{{q}}", "D[(db)]"…)
// into its id and, if a shape was given, the matching .npd shape-wrapped label
// (e.g. "[Label]", "(Label)", "{Label}", "([Label])"). Returns whether a shape
// (i.e. a declaration) was present.
bool parseNodeToken(const QString &tokIn, QString &id, QString &npdShape) {
    const QString tok = tokIn.trimmed();
    npdShape.clear();
    id.clear();
    int i = 0;
    while (i < tok.size() && isIdChar(tok.at(i))) ++i;
    id = tok.left(i);
    if (id.isEmpty()) return false;
    const QString rest = tok.mid(i).trimmed();
    if (rest.isEmpty()) return false;  // bare reference, no declaration

    auto wrap = [](const QString &label, const QString &o, const QString &c) {
        return o + unquote(label).simplified() + c;
    };
    struct Rule { const char *open; const char *close; const char *no; const char *nc; };
    // Most-specific (2-char) delimiters first so "[(" isn't eaten by "[".
    static const Rule rules[] = {
        {"[(", ")]", "([", "])"},  // cylinder   → database
        {"[[", "]]", "[",  "]"},   // subroutine → box
        {"((", "))", "(",  ")"},   // circle     → pill
        {"([", "])", "(",  ")"},   // stadium    → pill
        {"{{", "}}", "{",  "}"},   // hexagon    → decision
        {"[/", "/]", "[",  "]"},   // parallelogram → box
        {"[\\", "\\]", "[", "]"},  // trapezoid  → box
        {"[", "]",  "[",  "]"},    // rectangle  → box
        {"(", ")",  "(",  ")"},    // round      → pill
        {"{", "}",  "{",  "}"},    // rhombus    → decision
        {">", "]",  "[",  "]"},    // asymmetric → box
    };
    for (const Rule &r : rules) {
        const QString o = QString::fromLatin1(r.open), c = QString::fromLatin1(r.close);
        if (rest.startsWith(o) && rest.endsWith(c) && rest.size() >= o.size() + c.size()) {
            const QString inner = rest.mid(o.size(), rest.size() - o.size() - c.size());
            npdShape = wrap(inner, QString::fromLatin1(r.no), QString::fromLatin1(r.nc));
            return true;
        }
    }
    return false;
}

}  // namespace

QString mermaidToNpd(const QString &mermaid) {
    if (mermaid.trimmed().isEmpty()) return QString();

    QStringList nodeDecls;             // ordered "node <id> <shape>"
    QHash<QString, int> declIndex;     // id → index in nodeDecls
    QStringList edgeLines;             // ordered ".npd" edge lines

    auto registerNode = [&](const QString &token) -> QString {
        QString id, shape;
        const bool hasShape = parseNodeToken(token, id, shape);
        if (id.isEmpty()) return QString();
        if (hasShape) {
            const QString decl = QStringLiteral("node %1 %2").arg(id, shape);
            if (declIndex.contains(id))
                nodeDecls[declIndex.value(id)] = decl;   // later shape wins
            else { declIndex.insert(id, nodeDecls.size()); nodeDecls.append(decl); }
        }
        return id;
    };

    // dash-label form:  A -- label --> B     /     A == label ==> B
    QRegularExpression dashLabel(
        R"(^(.*?)\s+(?:--|==|-\.)\s+(.+?)\s+(?:--|==|\.-)+\s*([>ox])\s+(.*)$)");
    // pipe label anywhere in the connector:  A -->|label| B
    QRegularExpression pipeLabel(R"(\|([^|]*)\|)");
    // bare connector:  --  --- -->  ==>  -.->  <-->  --o  --x  (≥2 dashes/= /dots)
    QRegularExpression arrowCore(R"((<)?\s*(?:-{2,}|={2,}|-?\.-+|-\.+-?)\s*([>ox])?)");
    QRegularExpression headerRe(R"(^\s*(graph|flowchart)\b)",
                                QRegularExpression::CaseInsensitiveOption);
    QRegularExpression dropRe(
        R"(^\s*(subgraph|end|classDef|class|style|click|linkStyle|direction|%%).*)",
        QRegularExpression::CaseInsensitiveOption);

    QString diagType = QStringLiteral("flow");
    QString title;

    for (QString raw : mermaid.split('\n')) {
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith("%%")) continue;          // mermaid comment
        if (headerRe.match(line).hasMatch()) continue; // graph/flowchart TD …
        if (dropRe.match(line).hasMatch()) continue;   // unsupported constructs
        if (line.endsWith(';')) line.chop(1);

        // ── edge? ──
        QString from, to, label;
        bool isEdge = false, bidir = false;

        auto dm = dashLabel.match(line);
        if (dm.hasMatch()) {
            from = dm.captured(1).trimmed();
            label = unquote(dm.captured(2));
            to = dm.captured(4).trimmed();
            isEdge = true;
        } else {
            QString work = line;
            auto pm = pipeLabel.match(work);
            if (pm.hasMatch()) {
                label = unquote(pm.captured(1));
                work.remove(pm.capturedStart(), pm.capturedLength());
            }
            auto am = arrowCore.match(work);
            if (am.hasMatch() && am.capturedStart() > 0) {
                from = work.left(am.capturedStart()).trimmed();
                to = work.mid(am.capturedEnd()).trimmed();
                bidir = (am.captured(1) == "<");
                if (!from.isEmpty() && !to.isEmpty()) isEdge = true;
            }
        }

        if (isEdge) {
            const QString a = registerNode(from);
            const QString b = registerNode(to);
            if (a.isEmpty() || b.isEmpty()) continue;
            QString e = QStringLiteral("%1 %2 %3").arg(a, bidir ? "<->" : "->", b);
            if (!label.isEmpty()) e += " : " + label.simplified();
            edgeLines.append(e);
            continue;
        }

        // ── standalone node declaration?  (e.g. "A[Label]") ──
        QString id, shape;
        if (parseNodeToken(line, id, shape)) registerNode(line);
    }

    if (nodeDecls.isEmpty() && edgeLines.isEmpty())
        return QStringLiteral("# (Mermaid import found no nodes or edges)\ndiagram flow\n");

    QStringList out;
    out << "# imported from Mermaid";
    out << "diagram " + diagType;
    if (!title.isEmpty()) out << "title \"" + title + "\"";
    out << QString();
    out += nodeDecls;
    if (!nodeDecls.isEmpty() && !edgeLines.isEmpty()) out << QString();
    out += edgeLines;
    return out.join('\n') + "\n";
}

}  // namespace Npd
