// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════
// Mermaid flowchart → .npd translator.
//
// A best-effort importer for the common Mermaid `graph` / `flowchart` subset
// so users can paste an existing Mermaid diagram and keep working in .npd.
// Pure (Qt Core only), so it unit-tests without a GUI — same pattern as
// chart_spec_to_vega.cpp (project memory "vega-translator-pure-function").
//
// Shape mapping (Mermaid → .npd):
//   A[txt]    rectangle   → box        [txt]
//   A(txt)    round       → pill       (txt)
//   A([txt])  stadium     → pill       (txt)
//   A((txt))  circle      → pill       (txt)
//   A[[txt]]  subroutine  → box        [txt]
//   A[(txt)]  cylinder    → database   ([txt])
//   A{txt}    rhombus     → decision   {txt}
//   A{{txt}}  hexagon     → decision   {txt}
//   A>txt]    asymmetric  → box        [txt]
//   A[/txt/]  parallelogram/trapezoid  → box [txt]
//
// Edge mapping:
//   A --> B            → a -> b
//   A --- B / ==>  (any solid arrow)   → a -> b
//   A -.-> B           → a -.-> b   (dashed)
//   A <--> B           → a <-> b
//   A -->|label| B     → a -> b : label
//   A -- label --> B   → a -> b : label
//
// Structure mapping:
//   graph LR / RL      → direction LR   (TD / TB / BT → direction TB)
//   subgraph Name … end → group "Name" : <ids seen inside>
//
// Anything else unrecognized (classDef, style, click, %% comments) is dropped,
// so the result always parses with no errors. Text the .npd grammar cannot
// carry verbatim is adjusted: a "::" in a node label (the .npd hover
// separator, with no escape) becomes U+2237 "∷", and an edge label starting
// with '"' or ':' is quoted. Only flowcharts are translated: a document whose
// header names another Mermaid type is refused with an error.
// ═══════════════════════════════════════════════════════════════════════

#ifndef NOTEPATRA_MERMAID_IMPORT_H
#define NOTEPATRA_MERMAID_IMPORT_H

#include <QString>

namespace Npd {

// Translate a Mermaid flowchart into .npd source (best effort; the result
// always parses with Npd::parse(...).ok()). Returns "" and sets *error (when given) instead of an empty
// diagram when there is nothing to import:
//   - empty input,
//   - another Mermaid diagram type (sequenceDiagram, classDiagram, ...): the
//     message names the type and what is supported,
//   - text that yields no nodes or edges.
QString mermaidToNpd(const QString &mermaid, QString *error = nullptr);

// "Supported: ..." sentence the errors above end with.
QString mermaidSupportedTypes();

}  // namespace Npd

#endif  // NOTEPATRA_MERMAID_IMPORT_H
