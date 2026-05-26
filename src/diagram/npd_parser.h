// Notepatra Diagram (.npd) — the parser for our own text DSL.
//
// .npd is the SOURCE OF TRUTH for a diagram (see project memory
// "diagramming-tool-direction"): the visual canvas is a pure projection of
// this text, which is why undo/redo, step-review and version-control all
// come for free. This module is deliberately PURE (Qt Core only — QString /
// QJson) so it builds and unit-tests on every platform without WebEngine.
//
// Grammar (line-oriented; '#' starts a comment; blank lines ignored):
//
//   diagram flow|er|system          # optional, default flow
//   title   "Some Title"            # optional
//   palette clay|ocean|forest|mono  # optional, default "default"
//
//   node <id> (Label)               # () pill   (start / end)
//   node <id> [Label]               # [] box    (process, default)
//   node <id> {Label}               # {} decision (diamond)
//   node <id> ([Label])             # ([]) database (cylinder)
//   node <id> [Short] :: "Full detail shown on hover, kept out of the shape"
//   icon <id> :name "Label"         # rich-icon node; ':: "hover"' also allowed
//
//   <a> -> <b>                       # directed edge a→b
//   <a> -> <b> : label               # edge with a label drawn on the arrow
//   <a> <-> <b> : label              # bidirectional
//
//   textbox "A caption / figure note"
//
// Endpoints referenced by an edge but never declared with `node` are
// auto-created as default boxes (Mermaid-style leniency), so `a -> b` alone
// is a valid diagram.

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>

namespace Npd {

enum class Shape { Box, Pill, Decision, Database, Icon };

struct Node {
    QString id;
    QString label;            // short text shown INSIDE the shape
    QString hover;            // full detail shown on hover (may be empty)
    QString icon;             // icon name (Shape::Icon, or decorative); may be empty
    Shape   shape = Shape::Box;
};

struct Edge {
    QString from;
    QString to;
    QString label;            // text drawn on the arrow (may be empty)
    bool    bidirectional = false;
};

struct Diagram {
    QString       type    = QStringLiteral("flow");     // flow | er | system
    QString       title;
    QString       palette = QStringLiteral("default");
    QVector<Node> nodes;
    QVector<Edge> edges;
    QStringList   textboxes;
    QStringList   errors;     // "line N: message" — empty ⇒ clean parse
    bool ok() const { return errors.isEmpty(); }
};

// Parse .npd source into a Diagram model. Never throws; malformed lines are
// reported in `errors` (with 1-based line numbers) while still parsing the
// rest, so the live preview can show as much as it can.
Diagram parse(const QString &src);

// Serialize the model to the JSON graph the JS renderer consumes. This is the
// stable contract between the C++ parser and resources/diagram/render.js.
QJsonObject toGraphJson(const Diagram &d);

// "box" | "pill" | "decision" | "database" | "icon" — used in the JSON + tests.
QString shapeName(Shape s);

} // namespace Npd
