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
//   palette auto|paper|slate|clay|ocean|forest|mono|default
//                                   # optional, default "auto" (paper on a light
//                                   #   host, default on a dark one — resolved by
//                                   #   the renderer, NOT here: this stays pure)
//   direction TB|LR                 # optional, default TB (alias: `layout`)
//
//   node <id> (Label)               # () pill   (start / end)
//   node <id> [Label]               # [] box    (process, default)
//   node <id> {Label}               # {} decision (diamond)
//   node <id> ([Label])             # ([]) database (cylinder)
//   node <id> [Short] :: "Full detail shown on hover, kept out of the shape"
//   node <id> [Label] #1565c0       # optional per-node colour (after the shape)
//   node <id> (Label) green         # ...as #hex OR a common colour name
//   icon <id> :name "Label"         # rich-icon node; ':: "hover"' also allowed
//
// Colour is OPT-IN flavour: a node with no colour keeps the diagram palette
// (monochrome). The trailing colour token, when present, sits AFTER the shape
// (and before any ':: "hover"'). The parser stays pure Qt-Core, so it stores
// the raw colour string and the renderer (which has QColor) validates it and
// derives a readable border + auto-contrast text; an unrecognised string is
// ignored and the node falls back to the palette.
//
//   <a> -> <b>                       # directed edge a→b
//   <a> -.-> <b>                     # dashed edge (also `<a> <.-> <b>`)
//   <a> -> <b> : label               # edge with a label drawn on the arrow
//   <a> <-> <b> : label              # bidirectional (label optional)
//   <a> -> <b> -> <c> : label        # chain — expands to a→b, b→c; label on
//                                    #   the LAST hop (per-hop labels: own lines)
//
//   textbox "A caption / figure note"
//
//   group "Label" : a b c            # container drawn behind those nodes; a node
//                                    #   may be in AT MOST one group, and every id
//                                    #   must be a real node (else: line error)
//   note <id> "text"                 # small tinted card pinned beside the node
//   legend dashed "async"            # legend row: a dashed-line swatch
//   legend #cc785c "hot path"        # legend row: a colour swatch (#hex or name)
//
// Endpoints referenced by an edge but never declared with `node` are
// auto-created as default boxes (Mermaid-style leniency), so `a -> b` alone
// is a valid diagram. A line whose first token is a keyword (node/icon/diagram/
// title/palette/direction/textbox/group/note/legend) is always that statement,
// so a label may contain "->".
// Node ids may contain colons (http://x, ns:b) — the edge-label ':' is only the
// one written with a leading space (" : label"); colour goes on `node` lines.

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
    QString color;            // optional per-node colour token ("#1565c0" / "green"); empty ⇒ use palette
    Shape   shape = Shape::Box;
};

struct Edge {
    QString from;
    QString to;
    QString label;            // text drawn on the arrow (may be empty)
    bool    bidirectional = false;
    bool    dashed = false;   // `-.->` / `<.->` — drawn with a dashed stroke
};

// A named container drawn BEHIND the nodes it holds (`group "Label" : a b c`).
struct Group {
    QString     label;
    QStringList members;      // node ids, in source order
};

// A small annotation card pinned beside a node (`note <id> "text"`).
struct Note {
    QString target;           // node id
    QString text;
};

// One legend row (`legend dashed "async"` / `legend #cc785c "hot path"`).
struct LegendItem {
    QString swatch;           // "dashed" | "#hex" | a colour name
    QString text;
};

struct Diagram {
    QString       type    = QStringLiteral("flow");     // flow | er | system
    QString       title;
    // RAW palette token — "auto" resolves to paper/default in the renderer, which
    // is the only layer that knows whether the host widget is light or dark.
    QString       palette   = QStringLiteral("auto");
    QString       direction = QStringLiteral("TB");     // TB | LR
    QVector<Node> nodes;
    QVector<Edge> edges;
    QVector<Group>      groups;
    QVector<Note>       notes;
    QVector<LegendItem> legend;
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
