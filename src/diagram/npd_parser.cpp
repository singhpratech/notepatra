#include "npd_parser.h"

#include <QJsonArray>
#include <QHash>

namespace Npd {

namespace {

// Strip one layer of matching surrounding double quotes, if present.
QString unquote(QString s) {
    s = s.trimmed();
    if (s.size() >= 2 && s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')))
        return s.mid(1, s.size() - 2);
    return s;
}

// First whitespace-delimited token + the remainder (already trimmed).
void splitFirstToken(const QString &in, QString &tok, QString &rest) {
    const QString s = in.trimmed();
    int i = 0;
    while (i < s.size() && !s.at(i).isSpace()) ++i;
    tok  = s.left(i);
    rest = s.mid(i).trimmed();
}

// Decode "(Label)" / "[Label]" / "{Label}" / "([Label])" into a shape + label.
// Anything without recognized brackets is treated as a plain box label.
void decodeShape(const QString &specIn, Shape &shape, QString &label) {
    const QString spec = specIn.trimmed();
    auto inside = [&](int lead, int trail) {
        return spec.mid(lead, spec.size() - lead - trail).trimmed();
    };
    if (spec.startsWith(QLatin1String("([")) && spec.endsWith(QLatin1String("])"))) {
        shape = Shape::Database; label = inside(2, 2);
    } else if (spec.startsWith(QLatin1Char('(')) && spec.endsWith(QLatin1Char(')'))) {
        shape = Shape::Pill; label = inside(1, 1);
    } else if (spec.startsWith(QLatin1Char('{')) && spec.endsWith(QLatin1Char('}'))) {
        shape = Shape::Decision; label = inside(1, 1);
    } else if (spec.startsWith(QLatin1Char('[')) && spec.endsWith(QLatin1Char(']'))) {
        shape = Shape::Box; label = inside(1, 1);
    } else {
        shape = Shape::Box; label = spec;
    }
}

// A "#rgb" / "#rgba" / "#rrggbb" / "#rrggbbaa" hex colour token.
bool isHexColor(const QString &t) {
    if (!t.startsWith(QLatin1Char('#'))) return false;
    const QString h = t.mid(1);
    if (!(h.size() == 3 || h.size() == 4 || h.size() == 6 || h.size() == 8)) return false;
    for (const QChar c : h) {
        const QChar lc = c.toLower();
        const bool hex = (lc >= QLatin1Char('0') && lc <= QLatin1Char('9'))
                      || (lc >= QLatin1Char('a') && lc <= QLatin1Char('f'));
        if (!hex) return false;
    }
    return true;
}

// Common colour NAMES the pure-Core parser recognises as a trailing token. The
// renderer accepts any QColor-valid string; this curated set only exists to
// disambiguate a trailing colour word from ordinary label text without QColor.
bool isNamedColor(const QString &t) {
    static const QStringList names = {
        QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"),
        QStringLiteral("orange"), QStringLiteral("yellow"), QStringLiteral("purple"),
        QStringLiteral("violet"), QStringLiteral("teal"), QStringLiteral("cyan"),
        QStringLiteral("magenta"), QStringLiteral("pink"), QStringLiteral("brown"),
        QStringLiteral("gray"), QStringLiteral("grey"), QStringLiteral("black"),
        QStringLiteral("white"), QStringLiteral("gold"), QStringLiteral("indigo"),
        QStringLiteral("lime"), QStringLiteral("navy"), QStringLiteral("maroon"),
        QStringLiteral("olive"), QStringLiteral("coral"), QStringLiteral("salmon"),
        QStringLiteral("crimson"), QStringLiteral("turquoise"), QStringLiteral("tan"),
        QStringLiteral("silver"), QStringLiteral("aqua"), QStringLiteral("fuchsia"),
        QStringLiteral("steelblue"), QStringLiteral("seagreen"), QStringLiteral("tomato"),
        QStringLiteral("slategray"), QStringLiteral("darkcyan"), QStringLiteral("goldenrod") };
    return names.contains(t.toLower());
}

// Strip a trailing colour token off `spec` (in place) and return it, or "" if
// none. A hex token is always taken; a colour NAME is taken only when what
// precedes it ends at a shape/quote boundary ( ) ] } " ), so a bare word like
// "red" inside an unbracketed label is left untouched.
QString takeTrailingColor(QString &spec) {
    const QString s = spec.trimmed();
    int i = s.size();
    while (i > 0 && !s.at(i - 1).isSpace()) --i;
    const QString tok  = s.mid(i).trimmed();
    const QString head = s.left(i).trimmed();
    if (tok.isEmpty() || head.isEmpty()) return QString();
    if (isHexColor(tok)) { spec = head; return tok; }
    const bool boundary = head.endsWith(QLatin1Char(')')) || head.endsWith(QLatin1Char(']'))
                       || head.endsWith(QLatin1Char('}')) || head.endsWith(QLatin1Char('"'));
    if (boundary && isNamedColor(tok)) { spec = head; return tok.toLower(); }
    return QString();
}

} // namespace

QString shapeName(Shape s) {
    switch (s) {
        case Shape::Pill:     return QStringLiteral("pill");
        case Shape::Decision: return QStringLiteral("decision");
        case Shape::Database: return QStringLiteral("database");
        case Shape::Icon:     return QStringLiteral("icon");
        case Shape::Box:      break;
    }
    return QStringLiteral("box");
}

Diagram parse(const QString &src) {
    Diagram d;
    QHash<QString, int> index;   // node id → position in d.nodes
    // Source lines of the group/note statements, so the cross-reference checks
    // below (which must run AFTER every node is known) still report a line.
    QVector<int> groupLines, noteLines;

    auto nodeRef = [&](const QString &id) -> Node & {
        auto it = index.find(id);
        if (it != index.end()) return d.nodes[it.value()];
        Node n; n.id = id; n.label = id;   // default: a box labelled by its id
        index.insert(id, d.nodes.size());
        d.nodes.append(n);
        return d.nodes.last();
    };
    auto err = [&](int line, const QString &msg) {
        d.errors.append(QStringLiteral("line %1: %2").arg(line).arg(msg));
    };

    const QStringList lines = src.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const int lineNo = i + 1;
        QString line = lines.at(i).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

        // First token decides the statement kind. A leading keyword always wins,
        // so a `node`/`title`/`textbox`/`icon` line whose LABEL happens to contain
        // "->" (e.g. `node x [Maps A -> B]`) is NOT mistaken for an edge.
        QString kw, rest;
        splitFirstToken(line, kw, rest);
        const bool isKeyword =
            kw == QLatin1String("diagram") || kw == QLatin1String("title") ||
            kw == QLatin1String("palette") || kw == QLatin1String("textbox") ||
            kw == QLatin1String("node")    || kw == QLatin1String("icon")   ||
            kw == QLatin1String("direction") || kw == QLatin1String("layout") ||
            kw == QLatin1String("group")   || kw == QLatin1String("note")   ||
            kw == QLatin1String("legend");

        // ── edges: a non-keyword line with an arrow is an edge — or a CHAIN ──
        // `a -> b -> c` expands to a→b and b→c (it must NOT create one node named
        // literally "b -> c"). Each arrow keeps its own direction, so
        // `m -> n <-> o` mixes a directed and a bidirectional hop on one line.
        //
        // A trailing ": label" decorates the LAST hop (so `check -> dash : yes`
        // keeps labelling that single edge; put distinct per-hop labels on their
        // own lines). The label separator is the first ':' / '::' that lies AFTER
        // the first arrow AND is preceded by whitespace: that keeps a written
        // " : label" working — and lets a label itself contain "->" — while colons
        // INSIDE a node id (http://x, ns:b, b:state) stay part of the id.
        if (!isKeyword && line.contains(QLatin1String("->"))) {
            const int firstArrow = line.indexOf(QLatin1String("->"));  // ≥0 by the guard above
            QString chain = line, edgeLabel;
            for (int i = qMax(firstArrow, 1); i < line.size(); ++i) {
                if (line.at(i) == QLatin1Char(':') && line.at(i - 1).isSpace()) {
                    const int skip = (i + 1 < line.size() && line.at(i + 1) == QLatin1Char(':')) ? 2 : 1;
                    edgeLabel = line.mid(i + skip).trimmed();
                    chain     = line.left(i);
                    break;
                }
            }

            QStringList nodes;        // node refs, in source order
            QVector<bool> bidirOps;   // bidirOps[k] joins nodes[k] → nodes[k+1]
            QVector<bool> dashOps;    // dashOps[k]  — that hop is drawn dashed
            int pos = 0, last = 0;
            while (pos < chain.size()) {
                // 4-char dotted operators first, so "-.->" is not read as "->".
                if (chain.mid(pos, 4) == QLatin1String("<.->")) {
                    nodes.append(chain.mid(last, pos - last).trimmed());
                    bidirOps.append(true); dashOps.append(true);
                    pos += 4; last = pos;
                } else if (chain.mid(pos, 4) == QLatin1String("-.->")) {
                    nodes.append(chain.mid(last, pos - last).trimmed());
                    bidirOps.append(false); dashOps.append(true);
                    pos += 4; last = pos;
                } else if (chain.mid(pos, 3) == QLatin1String("<->")) {
                    nodes.append(chain.mid(last, pos - last).trimmed());
                    bidirOps.append(true); dashOps.append(false);
                    pos += 3; last = pos;
                } else if (chain.mid(pos, 2) == QLatin1String("->")) {
                    nodes.append(chain.mid(last, pos - last).trimmed());
                    bidirOps.append(false); dashOps.append(false);
                    pos += 2; last = pos;
                } else {
                    ++pos;
                }
            }
            nodes.append(chain.mid(last).trimmed());

            bool valid = nodes.size() >= 2;
            for (const QString &n : nodes) if (n.isEmpty()) valid = false;
            if (!valid) { err(lineNo, QStringLiteral("edge needs both endpoints")); continue; }

            for (const QString &n : nodes) nodeRef(n);
            for (int k = 1; k < nodes.size(); ++k) {
                Edge e;
                e.from = nodes.at(k - 1);
                e.to   = nodes.at(k);
                e.bidirectional = bidirOps.at(k - 1);
                e.dashed        = dashOps.at(k - 1);
                if (k == nodes.size() - 1) e.label = unquote(edgeLabel);  // label rides the last hop
                d.edges.append(e);
            }
            continue;
        }

        // ── keyword statements (kw / rest already split above) ──
        if (kw == QLatin1String("diagram")) {
            QString t; splitFirstToken(rest, t, rest);
            if (t == QLatin1String("flow") || t == QLatin1String("er") || t == QLatin1String("system"))
                d.type = t;
            else
                err(lineNo, QStringLiteral("unknown diagram type '%1' (expected flow|er|system)").arg(t));
        } else if (kw == QLatin1String("title")) {
            d.title = unquote(rest);
        } else if (kw == QLatin1String("palette")) {
            QString p; splitFirstToken(rest, p, rest);
            if (!p.isEmpty()) d.palette = p;
        } else if (kw == QLatin1String("direction") || kw == QLatin1String("layout")) {
            QString v; splitFirstToken(rest, v, rest);
            const QString u = v.toUpper();
            // TD is accepted as a Mermaid-flavoured spelling of TB.
            if (u == QLatin1String("TB") || u == QLatin1String("TD")) d.direction = QStringLiteral("TB");
            else if (u == QLatin1String("LR"))                        d.direction = QStringLiteral("LR");
            else err(lineNo, QStringLiteral("unknown direction '%1' (expected TB|LR)").arg(v));
        } else if (kw == QLatin1String("group")) {
            // group "Label" : a b c   —  ids are space-separated, validated below
            QString r = rest.trimmed(), label, idPart;
            if (r.startsWith(QLatin1Char('"'))) {
                const int close = r.indexOf(QLatin1Char('"'), 1);
                if (close < 0) { err(lineNo, QStringLiteral("group label is missing a closing quote")); continue; }
                label  = r.mid(1, close - 1);
                idPart = r.mid(close + 1).trimmed();
                if (!idPart.startsWith(QLatin1Char(':'))) { err(lineNo, QStringLiteral("group needs ':' before its member ids")); continue; }
                idPart = idPart.mid(1);
            } else {
                const int colon = r.indexOf(QLatin1Char(':'));
                if (colon < 0) { err(lineNo, QStringLiteral("group needs ':' before its member ids")); continue; }
                label  = r.left(colon).trimmed();
                idPart = r.mid(colon + 1);
            }
            const QStringList members = idPart.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (members.isEmpty()) { err(lineNo, QStringLiteral("group needs at least one member id")); continue; }
            Group g; g.label = label; g.members = members;
            groupLines.append(lineNo);
            d.groups.append(g);
        } else if (kw == QLatin1String("note")) {
            QString id; splitFirstToken(rest, id, rest);
            if (id.isEmpty()) { err(lineNo, QStringLiteral("note needs a node id")); continue; }
            const QString text = unquote(rest);
            if (text.isEmpty()) { err(lineNo, QStringLiteral("note needs some text")); continue; }
            Note nt; nt.target = id; nt.text = text;
            noteLines.append(lineNo);
            d.notes.append(nt);
        } else if (kw == QLatin1String("legend")) {
            QString sw; splitFirstToken(rest, sw, rest);
            const QString text = unquote(rest);
            if (sw.isEmpty() || text.isEmpty()) { err(lineNo, QStringLiteral("legend needs a swatch and some text")); continue; }
            if (sw.toLower() != QLatin1String("dashed") && !isHexColor(sw) && !isNamedColor(sw)) {
                err(lineNo, QStringLiteral("legend swatch '%1' is not 'dashed', a #hex or a colour name").arg(sw));
                continue;
            }
            LegendItem li; li.swatch = isHexColor(sw) ? sw : sw.toLower(); li.text = text;
            d.legend.append(li);
        } else if (kw == QLatin1String("textbox")) {
            const QString t = unquote(rest);
            if (!t.isEmpty()) d.textboxes.append(t);
        } else if (kw == QLatin1String("node")) {
            QString id; splitFirstToken(rest, id, rest);
            if (id.isEmpty()) { err(lineNo, QStringLiteral("node needs an id")); continue; }
            QString spec = rest, hover;
            const int hv = rest.indexOf(QLatin1String("::"));
            if (hv >= 0) { spec = rest.left(hv).trimmed(); hover = unquote(rest.mid(hv + 2)); }
            const QString color = takeTrailingColor(spec);   // after the shape, before ::
            Shape shape; QString label;
            decodeShape(spec, shape, label);
            Node &n = nodeRef(id);
            n.shape = shape;
            if (!label.isEmpty()) n.label = label;
            n.hover = hover;
            if (!color.isEmpty()) n.color = color;
        } else if (kw == QLatin1String("icon")) {
            // icon <id> :name "Label"  [:: "hover"]
            QString id; splitFirstToken(rest, id, rest);
            if (id.isEmpty()) { err(lineNo, QStringLiteral("icon needs an id")); continue; }
            QString hover;
            const int hv = rest.indexOf(QLatin1String("::"));
            if (hv >= 0) { hover = unquote(rest.mid(hv + 2)); rest = rest.left(hv).trimmed(); }
            if (!rest.startsWith(QLatin1Char(':'))) { err(lineNo, QStringLiteral("icon needs ':name' before the label")); continue; }
            QString iconName; QString after;
            splitFirstToken(rest.mid(1), iconName, after);
            if (iconName.isEmpty()) { err(lineNo, QStringLiteral("icon name is empty")); continue; }
            const QString color = takeTrailingColor(after);  // trailing colour after the quoted label
            Node &n = nodeRef(id);
            n.shape = Shape::Icon;
            n.icon  = iconName;
            const QString label = unquote(after);
            if (!label.isEmpty()) n.label = label;
            n.hover = hover;
            if (!color.isEmpty()) n.color = color;
        } else {
            err(lineNo, QStringLiteral("unrecognized statement '%1'").arg(kw));
        }
    }

    // ── cross-reference checks (run last: nodes may be declared after use) ──
    // A group member must be a real node, and a node belongs to AT MOST one
    // group — otherwise the container rectangles would have to overlap.
    QHash<QString, int> owner;   // node id → index of the group that holds it
    for (int gi = 0; gi < d.groups.size(); ++gi) {
        const int ln = gi < groupLines.size() ? groupLines.at(gi) : 0;
        for (const QString &m : d.groups.at(gi).members) {
            if (!index.contains(m))
                err(ln, QStringLiteral("group member '%1' is not a node").arg(m));
            else if (owner.contains(m))
                err(ln, QStringLiteral("node '%1' is already in group '%2'")
                        .arg(m, d.groups.at(owner.value(m)).label));
            else
                owner.insert(m, gi);
        }
    }
    for (int ni = 0; ni < d.notes.size(); ++ni) {
        if (!index.contains(d.notes.at(ni).target))
            err(ni < noteLines.size() ? noteLines.at(ni) : 0,
                QStringLiteral("note target '%1' is not a node").arg(d.notes.at(ni).target));
    }
    return d;
}

QJsonObject toGraphJson(const Diagram &d) {
    QJsonObject root;
    root[QStringLiteral("type")]    = d.type;
    root[QStringLiteral("title")]   = d.title;
    root[QStringLiteral("palette")] = d.palette;
    root[QStringLiteral("direction")] = d.direction;

    QJsonArray nodes;
    for (const Node &n : d.nodes) {
        QJsonObject o;
        o[QStringLiteral("id")]    = n.id;
        o[QStringLiteral("label")] = n.label;
        o[QStringLiteral("shape")] = shapeName(n.shape);
        if (!n.hover.isEmpty()) o[QStringLiteral("hover")] = n.hover;
        if (!n.icon.isEmpty())  o[QStringLiteral("icon")]  = n.icon;
        if (!n.color.isEmpty()) o[QStringLiteral("color")] = n.color;
        nodes.append(o);
    }
    root[QStringLiteral("nodes")] = nodes;

    QJsonArray edges;
    for (const Edge &e : d.edges) {
        QJsonObject o;
        o[QStringLiteral("from")] = e.from;
        o[QStringLiteral("to")]   = e.to;
        if (!e.label.isEmpty())  o[QStringLiteral("label")] = e.label;
        if (e.bidirectional)     o[QStringLiteral("bidirectional")] = true;
        if (e.dashed)            o[QStringLiteral("dashed")] = true;
        edges.append(o);
    }
    root[QStringLiteral("edges")] = edges;

    if (!d.groups.isEmpty()) {
        QJsonArray groups;
        for (const Group &g : d.groups) {
            QJsonObject o;
            o[QStringLiteral("label")] = g.label;
            QJsonArray ms; for (const QString &m : g.members) ms.append(m);
            o[QStringLiteral("members")] = ms;
            groups.append(o);
        }
        root[QStringLiteral("groups")] = groups;
    }
    if (!d.notes.isEmpty()) {
        QJsonArray notes;
        for (const Note &n : d.notes) {
            QJsonObject o;
            o[QStringLiteral("target")] = n.target;
            o[QStringLiteral("text")]   = n.text;
            notes.append(o);
        }
        root[QStringLiteral("notes")] = notes;
    }
    if (!d.legend.isEmpty()) {
        QJsonArray legend;
        for (const LegendItem &li : d.legend) {
            QJsonObject o;
            o[QStringLiteral("swatch")] = li.swatch;
            o[QStringLiteral("text")]   = li.text;
            legend.append(o);
        }
        root[QStringLiteral("legend")] = legend;
    }

    QJsonArray boxes;
    for (const QString &t : d.textboxes) boxes.append(t);
    root[QStringLiteral("textboxes")] = boxes;

    return root;
}

} // namespace Npd
