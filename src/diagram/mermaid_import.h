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
// so the result always parses.
// ═══════════════════════════════════════════════════════════════════════

#ifndef NOTEPATRA_MERMAID_IMPORT_H
#define NOTEPATRA_MERMAID_IMPORT_H

#include <QString>

namespace Npd {

// Translate a Mermaid flowchart into .npd source. Returns "" only for empty
// input; otherwise always returns parseable .npd (best effort).
QString mermaidToNpd(const QString &mermaid);

}  // namespace Npd

#endif  // NOTEPATRA_MERMAID_IMPORT_H
