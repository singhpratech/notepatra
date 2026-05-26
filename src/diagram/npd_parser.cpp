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

        // ── edges first: a line containing an arrow is an edge ──
        const bool bidir = line.contains(QLatin1String("<->"));
        const int arrowPos = bidir ? line.indexOf(QLatin1String("<->"))
                                   : line.indexOf(QLatin1String("->"));
        if (arrowPos >= 0) {
            const int arrowLen = bidir ? 3 : 2;
            const QString from = line.left(arrowPos).trimmed();
            QString rhs = line.mid(arrowPos + arrowLen).trimmed();
            QString label;
            const int colon = rhs.indexOf(QLatin1Char(':'));
            if (colon >= 0) { label = rhs.mid(colon + 1).trimmed(); rhs = rhs.left(colon).trimmed(); }
            const QString to = rhs;
            if (from.isEmpty() || to.isEmpty()) { err(lineNo, QStringLiteral("edge needs both endpoints")); continue; }
            nodeRef(from); nodeRef(to);
            Edge e; e.from = from; e.to = to; e.label = unquote(label); e.bidirectional = bidir;
            d.edges.append(e);
            continue;
        }

        // ── keyword statements ──
        QString kw, rest;
        splitFirstToken(line, kw, rest);

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
        } else if (kw == QLatin1String("textbox")) {
            const QString t = unquote(rest);
            if (!t.isEmpty()) d.textboxes.append(t);
        } else if (kw == QLatin1String("node")) {
            QString id; splitFirstToken(rest, id, rest);
            if (id.isEmpty()) { err(lineNo, QStringLiteral("node needs an id")); continue; }
            QString spec = rest, hover;
            const int hv = rest.indexOf(QLatin1String("::"));
            if (hv >= 0) { spec = rest.left(hv).trimmed(); hover = unquote(rest.mid(hv + 2)); }
            Shape shape; QString label;
            decodeShape(spec, shape, label);
            Node &n = nodeRef(id);
            n.shape = shape;
            if (!label.isEmpty()) n.label = label;
            n.hover = hover;
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
            Node &n = nodeRef(id);
            n.shape = Shape::Icon;
            n.icon  = iconName;
            const QString label = unquote(after);
            if (!label.isEmpty()) n.label = label;
            n.hover = hover;
        } else {
            err(lineNo, QStringLiteral("unrecognized statement '%1'").arg(kw));
        }
    }
    return d;
}

QJsonObject toGraphJson(const Diagram &d) {
    QJsonObject root;
    root[QStringLiteral("type")]    = d.type;
    root[QStringLiteral("title")]   = d.title;
    root[QStringLiteral("palette")] = d.palette;

    QJsonArray nodes;
    for (const Node &n : d.nodes) {
        QJsonObject o;
        o[QStringLiteral("id")]    = n.id;
        o[QStringLiteral("label")] = n.label;
        o[QStringLiteral("shape")] = shapeName(n.shape);
        if (!n.hover.isEmpty()) o[QStringLiteral("hover")] = n.hover;
        if (!n.icon.isEmpty())  o[QStringLiteral("icon")]  = n.icon;
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
        edges.append(o);
    }
    root[QStringLiteral("edges")] = edges;

    QJsonArray boxes;
    for (const QString &t : d.textboxes) boxes.append(t);
    root[QStringLiteral("textboxes")] = boxes;

    return root;
}

} // namespace Npd
