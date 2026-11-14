// SPDX-License-Identifier: GPL-3.0-or-later

#include "mermaid_import.h"

#include <QHash>
#include <QRegularExpression>
#include <QVector>
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

// The statement keywords Npd::parse reads from the first word of a line.
bool isNpdKeyword(const QString &w) {
    static const QStringList kws = {
        QStringLiteral("diagram"), QStringLiteral("title"),     QStringLiteral("palette"),
        QStringLiteral("textbox"), QStringLiteral("node"),      QStringLiteral("icon"),
        QStringLiteral("direction"), QStringLiteral("layout"),  QStringLiteral("group"),
        QStringLiteral("note"),    QStringLiteral("legend")};
    return kws.contains(w);
}

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

    // In .npd the first "::" on a node line starts its hover text, even inside
    // the brackets, and the grammar has no escape for it. A Mermaid label like
    // "std::vector" or "IPv6 ::1" would be cut there and leave the shape
    // unclosed, so each "::" becomes U+2237 PROPORTION, a single character
    // that reads as a double colon.
    auto wrap = [](const QString &label, const QString &o, const QString &c) {
        QString text = unquote(label).simplified();
        text.replace(QLatin1String("::"), QString(QChar(0x2237)));
        return o + text + c;
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

// The diagram-type keyword Mermaid reads from the first meaningful line, or ""
// when that line is already flowchart content (an edge or a node), which the
// importer accepts without a header. Skips blank lines, %% comments,
// %%{init}%% directives and a leading `---` front-matter block.
QString headerKeyword(const QString &mermaid) {
    const QStringList lines = mermaid.split(QLatin1Char('\n'));
    int i = 0;
    while (i < lines.size() && lines.at(i).trimmed().isEmpty()) ++i;
    if (i < lines.size() && lines.at(i).trimmed() == QLatin1String("---")) {
        for (++i; i < lines.size(); ++i)
            if (lines.at(i).trimmed() == QLatin1String("---")) { ++i; break; }
    }
    for (; i < lines.size(); ++i) {
        const QString t = lines.at(i).trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1String("%%"))) continue;
        // `A --> B`, `A[x]`, `A -.-> B`: content, not a header.
        if (t.contains(QLatin1String("--")) || t.contains(QLatin1String("==")) ||
            t.contains(QLatin1String("-.")) || t.contains(QLatin1Char('[')) ||
            t.contains(QLatin1Char('(')) || t.contains(QLatin1Char('{')))
            return QString();
        int k = 0;
        while (k < t.size() && (t.at(k).isLetterOrNumber() || t.at(k) == QLatin1Char('-') ||
                                t.at(k) == QLatin1Char('_')))
            ++k;
        return t.left(k);
    }
    return QString();
}

// A header keyword that names a Mermaid diagram type other than a flowchart.
bool isOtherMermaidType(const QString &kw) {
    static const QStringList types = {
        QStringLiteral("sequenceDiagram"), QStringLiteral("classDiagram"),
        QStringLiteral("classDiagram-v2"), QStringLiteral("stateDiagram"),
        QStringLiteral("stateDiagram-v2"), QStringLiteral("erDiagram"),
        QStringLiteral("journey"), QStringLiteral("gantt"), QStringLiteral("pie"),
        QStringLiteral("quadrantChart"), QStringLiteral("requirementDiagram"),
        QStringLiteral("gitGraph"), QStringLiteral("C4Context"),
        QStringLiteral("C4Container"), QStringLiteral("C4Component"),
        QStringLiteral("C4Dynamic"), QStringLiteral("C4Deployment"),
        QStringLiteral("mindmap"), QStringLiteral("timeline"), QStringLiteral("zenuml"),
        QStringLiteral("sankey"), QStringLiteral("sankey-beta"), QStringLiteral("xychart"),
        QStringLiteral("xychart-beta"), QStringLiteral("block"), QStringLiteral("block-beta"),
        QStringLiteral("packet"), QStringLiteral("packet-beta"), QStringLiteral("kanban"),
        QStringLiteral("architecture"), QStringLiteral("architecture-beta"),
        QStringLiteral("radar"), QStringLiteral("radar-beta"), QStringLiteral("treemap"),
        QStringLiteral("treemap-beta")};
    if (kw.isEmpty()) return false;
    // Mermaid matches its type keywords case-sensitively, so a headerless
    // flowchart whose first line is a bare word ("Diagram", "Journey") is
    // content, not a header.
    if (types.contains(kw)) return true;
    // Types Mermaid adds later follow the same naming: a camelCase word
    // ending in "Diagram" (requirementDiagram) or a lowercase word plus
    // "-beta" (sankey-beta). Both start with a lowercase ASCII letter.
    const QChar c0 = kw.at(0);
    if (c0 < QLatin1Char('a') || c0 > QLatin1Char('z')) return false;
    auto asciiAlnum = [](const QString &s) {
        for (const QChar c : s)
            if (!((c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                  (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
                  (c >= QLatin1Char('0') && c <= QLatin1Char('9'))))
                return false;
        return true;
    };
    static const QString diagramSuffix = QStringLiteral("Diagram"), betaSuffix = QStringLiteral("-beta");
    if (kw.size() > diagramSuffix.size() && kw.endsWith(diagramSuffix))
        return asciiAlnum(kw.left(kw.size() - diagramSuffix.size()));
    if (kw.size() > betaSuffix.size() && kw.endsWith(betaSuffix))
        return asciiAlnum(kw.left(kw.size() - betaSuffix.size()));
    return false;
}

}  // namespace

QString mermaidSupportedTypes() {
    return QStringLiteral("Supported: Mermaid flowcharts, i.e. text that starts with "
                          "\"flowchart\" or \"graph\" (TD, TB, BT, LR or RL).");
}

QString mermaidToNpd(const QString &mermaid, QString *error) {
    auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return QString();
    };
    if (error) error->clear();
    if (mermaid.trimmed().isEmpty()) return fail(QStringLiteral("there is no Mermaid text to import"));
    const QString kw = headerKeyword(mermaid);
    const bool flowHeader = kw.compare(QLatin1String("graph"), Qt::CaseInsensitive) == 0 ||
                            kw.compare(QLatin1String("flowchart"), Qt::CaseInsensitive) == 0;
    if (isOtherMermaidType(kw))
        return fail(QStringLiteral("Mermaid \"%1\" diagrams cannot be imported. %2")
                        .arg(kw, mermaidSupportedTypes()));

    QStringList nodeDecls;             // ordered "node <id> <shape>"
    QHash<QString, int> declIndex;     // id → index in nodeDecls
    QStringList edgeLines;             // ordered ".npd" edge lines
    // subgraph → .npd group. Members are collected as ids are seen inside the
    // block; a node lands in the INNERMOST open subgraph and in only one group.
    QStringList groupLabels;
    QVector<QStringList> groupMembers;
    QHash<QString, int> groupOf;       // node id → group index
    QVector<int> openGroups;           // stack of indices of the open subgraphs

    auto registerNode = [&](const QString &token) -> QString {
        QString id, shape;
        const bool hasShape = parseNodeToken(token, id, shape);
        if (id.isEmpty()) return QString();
        if (!openGroups.isEmpty() && !groupOf.contains(id)) {
            const int gi = openGroups.last();
            groupOf.insert(id, gi);
            groupMembers[gi].append(id);
        }
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
    QRegularExpression headerRe(R"(^\s*(graph|flowchart)\b\s*([A-Za-z]{2})?)",
                                QRegularExpression::CaseInsensitiveOption);
    QRegularExpression subgraphRe(R"(^\s*subgraph\b\s*(.*)$)",
                                  QRegularExpression::CaseInsensitiveOption);
    QRegularExpression subTitleRe(R"(^\S+\s*\[(.*)\]$)");
    QRegularExpression endRe(R"(^\s*end\s*$)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression dropRe(
        R"(^\s*(classDef|class|style|click|linkStyle|direction|%%).*)",
        QRegularExpression::CaseInsensitiveOption);

    QString diagType = QStringLiteral("flow");
    QString direction;
    QString title;

    for (QString raw : mermaid.split('\n')) {
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith("%%")) continue;          // mermaid comment
        {   // graph/flowchart TD … — the direction token carries over to .npd
            const auto hm = headerRe.match(line);
            if (hm.hasMatch()) {
                const QString dir = hm.captured(2).toUpper();
                if (dir == "LR" || dir == "RL") direction = QStringLiteral("LR");
                else if (dir == "TD" || dir == "TB" || dir == "BT") direction = QStringLiteral("TB");
                continue;
            }
        }
        {   // subgraph Name … end  →  group "Name" : ids
            const auto sm = subgraphRe.match(line);
            if (sm.hasMatch()) {
                QString label = unquote(sm.captured(1).trimmed());
                const auto tm = subTitleRe.match(label);
                if (tm.hasMatch()) label = unquote(tm.captured(1).trimmed());
                label.remove(QLatin1Char('"'));   // the .npd group label is quoted
                if (label.isEmpty()) label = QStringLiteral("Group");
                groupLabels.append(label);
                groupMembers.append(QStringList());
                openGroups.append(groupLabels.size() - 1);
                continue;
            }
            if (endRe.match(line).hasMatch()) {
                if (!openGroups.isEmpty()) openGroups.removeLast();
                continue;
            }
        }
        if (dropRe.match(line).hasMatch()) continue;   // unsupported constructs
        if (line.endsWith(';')) line.chop(1);

        // ── edge? ──
        QString from, to, label;
        bool isEdge = false, bidir = false, dashed = false;

        auto dm = dashLabel.match(line);
        if (dm.hasMatch()) {
            from = dm.captured(1).trimmed();
            label = unquote(dm.captured(2));
            to = dm.captured(4).trimmed();
            dashed = line.contains(QLatin1String("-."));
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
                dashed = am.captured(0).contains(QLatin1Char('.'));
                if (!from.isEmpty() && !to.isEmpty()) isEdge = true;
            }
        }

        if (isEdge) {
            // Both ends must have an id before either joins a subgraph: a
            // member with no node behind it is a .npd error.
            QString ida, idb, ignored;
            parseNodeToken(from, ida, ignored);
            parseNodeToken(to, idb, ignored);
            if (ida.isEmpty() || idb.isEmpty()) continue;
            const QString a = registerNode(from);
            const QString b = registerNode(to);
            const QString op = dashed ? (bidir ? "<.->" : "-.->") : (bidir ? "<->" : "->");
            // A line starting with an .npd keyword (`title -> x`) is read as
            // that statement; with the arrow attached the first word is no
            // longer the keyword, and the edge parses the same.
            QString e = isNpdKeyword(a) ? QStringLiteral("%1%2 %3").arg(a, op, b)
                                        : QStringLiteral("%1 %2 %3").arg(a, op, b);
            if (!label.isEmpty()) {
                QString text = label.simplified();
                // A leading quote would read as an unterminated quoted label
                // and a leading ':' as part of a '::' separator; quoting the
                // whole label keeps both as text.
                if (text.startsWith(QLatin1Char('"')) || text.startsWith(QLatin1Char(':')))
                    text = QLatin1Char('"') + text + QLatin1Char('"');
                e += " : " + text;
            }
            edgeLines.append(e);
            continue;
        }

        // ── standalone node declaration?  (e.g. "A[Label]") ──
        QString id, shape;
        if (parseNodeToken(line, id, shape)) registerNode(line);
    }

    if (nodeDecls.isEmpty() && edgeLines.isEmpty())
        return fail(flowHeader
                        ? QStringLiteral("the Mermaid flowchart has no nodes or edges to import")
                        : QStringLiteral("found no Mermaid flowchart nodes or edges to import. %1")
                              .arg(mermaidSupportedTypes()));

    QStringList out;
    out << "# imported from Mermaid";
    out << "diagram " + diagType;
    if (!title.isEmpty()) out << "title \"" + title + "\"";
    if (!direction.isEmpty()) out << "direction " + direction;
    out << QString();
    out += nodeDecls;
    if (!nodeDecls.isEmpty() && !edgeLines.isEmpty()) out << QString();
    out += edgeLines;
    QStringList groupLines;
    for (int gi = 0; gi < groupLabels.size(); ++gi) {
        if (groupMembers.at(gi).isEmpty()) continue;   // empty subgraph → nothing to draw
        groupLines << QStringLiteral("group \"%1\" : %2")
                          .arg(groupLabels.at(gi), groupMembers.at(gi).join(QLatin1Char(' ')));
    }
    if (!groupLines.isEmpty()) { out << QString(); out += groupLines; }
    return out.join('\n') + "\n";
}

}  // namespace Npd
