/* SPDX-License-Identifier: GPL-3.0-or-later
 * ════════════════════════════════════════════════════════════════════════
 * Notepatra Diagram renderer  (.npd diagrams-as-code → infinite SVG canvas)
 *
 * Consumes EXACTLY the JSON graph emitted by src/diagram/npd_parser.h:
 *
 *   { "type":"flow"|"er"|"system", "title":"...",
 *     "palette":"clay"|"ocean"|"forest"|"mono"|"default",
 *     "nodes":[{"id","label","shape":"box"|"pill"|"decision"|"database"|"icon",
 *               "hover"?,"icon"?}],
 *     "edges":[{"from","to","label"?,"bidirectional"?}],
 *     "textboxes":["..."] }
 *
 * Layout is dagre (rankdir TB). The whole drawing lives inside one
 * <g id="npd-viewport"> whose transform we drive for pan/zoom/fit so the
 * canvas is effectively infinite.
 *
 * Public surface (called from C++ via runJavaScript):
 *   window.npdRender(jsonStringOrObject)   — (re)render a graph
 *   window.npdFit()                        — fit drawing to viewport
 *   window._npd_export_png(slot, scale)    — rasterise → window['_npd_result_'+slot]
 *   window._npd_export_webp(slot, scale)
 *   window._npd_export_jpeg(slot, scale)
 *   window._npd_export_svg(slot)           — serialise SVG string → result slot
 *
 * No runtime CDN. dagre is vendored at lib/dagre.min.js. Icons are inlined
 * (see ICONS below) so the rendered SVG is self-contained and exportable.
 * ════════════════════════════════════════════════════════════════════════ */
(function (global) {
  "use strict";

  var SVGNS = "http://www.w3.org/2000/svg";

  // ──────────────────────────────────────────────────────────────────────
  // Palettes. Each entry colours node fill / stroke / text, edge stroke,
  // the title header, and caption boxes. "default" is a calm slate/blue.
  // Values chosen to read well on the light "paper" canvas background.
  // ──────────────────────────────────────────────────────────────────────
  var PALETTES = {
    default: {
      bg: "#fbfaf7",
      nodeFill: "#ffffff", nodeStroke: "#3b4a63", nodeText: "#1f2733",
      accentFill: "#eef2f9", accentStroke: "#3b4a63",
      edge: "#5a6b86", edgeText: "#3b4a63",
      header: "#1f2733", caption: "#475068", captionBg: "#f3f1ec",
      captionBorder: "#d9d4c8", icon: "#3b4a63"
    },
    clay: {
      bg: "#fbf6f1",
      nodeFill: "#fffaf5", nodeStroke: "#a85c3b", nodeText: "#4a2c1d",
      accentFill: "#f6e4d8", accentStroke: "#a85c3b",
      edge: "#b06a45", edgeText: "#8a4a2e",
      header: "#5c3220", caption: "#7a4a31",
      captionBg: "#f6ebe2", captionBorder: "#e3cdbd", icon: "#a85c3b"
    },
    ocean: {
      bg: "#f3f8fb",
      nodeFill: "#f8fcff", nodeStroke: "#1f6f8b", nodeText: "#0f3a4a",
      accentFill: "#d8ecf4", accentStroke: "#1f6f8b",
      edge: "#2b87a3", edgeText: "#1b5f78",
      header: "#0f3a4a", caption: "#2a5f72",
      captionBg: "#e7f2f7", captionBorder: "#c3dde8", icon: "#1f6f8b"
    },
    forest: {
      bg: "#f4f8f2",
      nodeFill: "#f9fcf7", nodeStroke: "#3f7a43", nodeText: "#1f3a22",
      accentFill: "#e1eedd", accentStroke: "#3f7a43",
      edge: "#4d8a52", edgeText: "#2f5f33",
      header: "#1f3a22", caption: "#3a5f3d",
      captionBg: "#e9f2e6", captionBorder: "#cbe0c5", icon: "#3f7a43"
    },
    mono: {
      bg: "#f7f7f7",
      nodeFill: "#ffffff", nodeStroke: "#444444", nodeText: "#222222",
      accentFill: "#ededed", accentStroke: "#444444",
      edge: "#666666", edgeText: "#333333",
      header: "#1a1a1a", caption: "#555555",
      captionBg: "#f0f0f0", captionBorder: "#d6d6d6", icon: "#444444"
    }
  };

  function palette(name) {
    return PALETTES[name] || PALETTES.default;
  }

  // ──────────────────────────────────────────────────────────────────────
  // Inlined icon geometry. Each value is the *inner* markup of a 24×24
  // viewBox SVG (matches resources/diagram/icons/<name>.svg). We inline so
  // the rendered SVG carries its own glyphs and the PNG/SVG export stays
  // self-contained (an <image href="qrc://…"> would not serialise/rasterise).
  // stroke="currentColor" → we set `color` on the wrapping <g>.
  // ──────────────────────────────────────────────────────────────────────
  var ICONS = {
    database:
      '<ellipse cx="12" cy="5" rx="7" ry="2.6"/>' +
      '<path d="M5 5v6c0 1.4 3.1 2.6 7 2.6s7-1.2 7-2.6V5"/>' +
      '<path d="M5 11v6c0 1.4 3.1 2.6 7 2.6s7-1.2 7-2.6v-6"/>',
    server:
      '<rect x="3" y="4" width="18" height="6" rx="1.5"/>' +
      '<rect x="3" y="14" width="18" height="6" rx="1.5"/>' +
      '<line x1="6.5" y1="7" x2="6.5" y2="7"/>' +
      '<line x1="6.5" y1="17" x2="6.5" y2="17"/>' +
      '<line x1="10" y1="7" x2="17" y2="7"/>' +
      '<line x1="10" y1="17" x2="17" y2="17"/>',
    user:
      '<circle cx="12" cy="8" r="4"/>' +
      '<path d="M4 20c0-4 3.6-6 8-6s8 2 8 6"/>',
    patient:
      '<circle cx="12" cy="7" r="3.4"/>' +
      '<path d="M4.5 20c0-3.6 3.4-5.6 7.5-5.6s7.5 2 7.5 5.6"/>' +
      '<path d="M12 9.5v4"/><path d="M10 11.5h4"/>',
    hospital:
      '<rect x="4" y="6" width="16" height="14" rx="1.5"/>' +
      '<path d="M12 9v4"/><path d="M10 11h4"/>' +
      '<line x1="8" y1="20" x2="8" y2="16"/>' +
      '<line x1="16" y1="20" x2="16" y2="16"/>' +
      '<path d="M9 6V4h6v2"/>',
    document:
      '<path d="M6 3h8l5 5v13H6z"/>' +
      '<path d="M14 3v5h5"/>' +
      '<line x1="9" y1="13" x2="16" y2="13"/>' +
      '<line x1="9" y1="16.5" x2="16" y2="16.5"/>',
    cloud:
      '<path d="M7 18a4 4 0 0 1 .6-7.95A5 5 0 0 1 17 9.5a3.5 3.5 0 0 1 .5 6.95z"/>',
    gear:
      '<circle cx="12" cy="12" r="3"/>' +
      '<path d="M12 2.5v2.5M12 19v2.5M21.5 12H19M5 12H2.5M18.7 5.3l-1.8 1.8M7.1 16.9l-1.8 1.8M18.7 18.7l-1.8-1.8M7.1 7.1L5.3 5.3"/>',
    table:
      '<rect x="3.5" y="4.5" width="17" height="15" rx="1.5"/>' +
      '<line x1="3.5" y1="9.5" x2="20.5" y2="9.5"/>' +
      '<line x1="3.5" y1="14.5" x2="20.5" y2="14.5"/>' +
      '<line x1="9" y1="9.5" x2="9" y2="19.5"/>' +
      '<line x1="15" y1="9.5" x2="15" y2="19.5"/>',
    process:
      '<rect x="3.5" y="7" width="17" height="10" rx="1.5"/>' +
      '<line x1="3.5" y1="10.3" x2="20.5" y2="10.3"/>' +
      '<line x1="9" y1="13.5" x2="15" y2="13.5"/>',
    decision:
      '<path d="M12 3l9 9-9 9-9-9z"/>' +
      '<path d="M9.5 12h5"/><path d="M12 9.5v5"/>',
    chart:
      '<path d="M4 4v16h16"/>' +
      '<rect x="7" y="11" width="3" height="6"/>' +
      '<rect x="12" y="8" width="3" height="9"/>' +
      '<rect x="17" y="5" width="3" height="12"/>'
  };

  // ──────────────────────────────────────────────────────────────────────
  // Module state — the live drawing + viewport transform.
  // ──────────────────────────────────────────────────────────────────────
  var state = {
    svg: null,        // outer <svg> (full viewport)
    viewport: null,   // <g> we pan/zoom
    drawing: null,    // <g> holding the actual graph (header+nodes+edges)
    bbox: null,       // { x, y, w, h } of the drawing in graph coords
    tx: 0, ty: 0, scale: 1,
    hoverBox: null,   // HTML div used for the styled tooltip
    lastGraph: null   // last parsed graph object (for re-render on resize)
  };

  // Approximate text width (px) for a given font-size. We have no DOM-metrics
  // guarantee before insertion, so we use a stable per-char estimate. Good
  // enough for truncation + node sizing; the visible text element is the
  // source of truth at paint time.
  function approxTextWidth(text, fontSize) {
    if (!text) return 0;
    var w = 0;
    for (var i = 0; i < text.length; i++) {
      var c = text.charCodeAt(i);
      if (c === 105 || c === 108 || c === 73 || c === 106 || c === 46 || c === 44) {
        w += fontSize * 0.30;            // i l I j . ,
      } else if (c === 109 || c === 119 || c === 77 || c === 87) {
        w += fontSize * 0.92;            // m w M W
      } else if (c >= 65 && c <= 90) {
        w += fontSize * 0.66;            // uppercase
      } else {
        w += fontSize * 0.54;            // default lowercase / digit
      }
    }
    return w;
  }

  // Truncate to maxWidth (px) with an ellipsis; returns the display string.
  function truncateToWidth(text, fontSize, maxWidth) {
    if (!text) return "";
    if (approxTextWidth(text, fontSize) <= maxWidth) return text;
    var ell = "…";
    var ellW = approxTextWidth(ell, fontSize);
    var out = "";
    for (var i = 0; i < text.length; i++) {
      var next = out + text[i];
      if (approxTextWidth(next, fontSize) + ellW > maxWidth) break;
      out = next;
    }
    return (out.length ? out : "") + ell;
  }

  function el(name, attrs) {
    var node = document.createElementNS(SVGNS, name);
    if (attrs) {
      for (var k in attrs) {
        if (Object.prototype.hasOwnProperty.call(attrs, k) && attrs[k] != null) {
          node.setAttribute(k, String(attrs[k]));
        }
      }
    }
    return node;
  }

  function esc(s) {
    return String(s == null ? "" : s)
      .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  // ──────────────────────────────────────────────────────────────────────
  // Shape builders. Each returns an SVG element group placed at (0,0)..(w,h)
  // in *local* node coords; the caller translates the group to the node's
  // top-left. `pal` is the active palette, `accent` toggles the accent fill.
  // ──────────────────────────────────────────────────────────────────────
  function shapeBox(w, h, pal) {
    return el("rect", {
      x: 0, y: 0, width: w, height: h, rx: 8, ry: 8,
      fill: pal.nodeFill, stroke: pal.nodeStroke, "stroke-width": 1.6
    });
  }

  function shapePill(w, h, pal) {
    var r = h / 2;
    return el("rect", {
      x: 0, y: 0, width: w, height: h, rx: r, ry: r,
      fill: pal.accentFill, stroke: pal.accentStroke, "stroke-width": 1.6
    });
  }

  function shapeDecision(w, h, pal) {
    // Diamond inscribed in the w×h box.
    var pts = [
      (w / 2) + "," + 0,
      w + "," + (h / 2),
      (w / 2) + "," + h,
      0 + "," + (h / 2)
    ].join(" ");
    return el("polygon", {
      points: pts,
      fill: pal.accentFill, stroke: pal.accentStroke, "stroke-width": 1.6
    });
  }

  function shapeDatabase(w, h, pal) {
    // Cylinder: top ellipse + body + bottom curve.
    var g = el("g", null);
    var ry = Math.min(10, h * 0.16);
    var d =
      "M0," + ry +
      " A" + (w / 2) + "," + ry + " 0 0 1 " + w + "," + ry +
      " L" + w + "," + (h - ry) +
      " A" + (w / 2) + "," + ry + " 0 0 1 0," + (h - ry) +
      " Z";
    g.appendChild(el("path", {
      d: d, fill: pal.nodeFill, stroke: pal.nodeStroke, "stroke-width": 1.6
    }));
    // top lid
    g.appendChild(el("ellipse", {
      cx: w / 2, cy: ry, rx: w / 2, ry: ry,
      fill: pal.accentFill, stroke: pal.nodeStroke, "stroke-width": 1.6
    }));
    return g;
  }

  // ──────────────────────────────────────────────────────────────────────
  // Node sizing. Decides w×h per shape from the (possibly long) label so
  // dagre lays things out without overlap. We size to the *display* label
  // (post-truncation cap) so boxes stay compact; the full label lives in the
  // tooltip when truncated.
  // ──────────────────────────────────────────────────────────────────────
  var FONT = 13;
  var ICON_FONT = 12;
  var MAX_LABEL_PX = 168;   // cap node text width; longer → ellipsis + hover

  function sizeFor(node) {
    var shape = node.shape || "box";
    var label = node.label != null ? String(node.label) : String(node.id || "");
    var textW = Math.min(approxTextWidth(label, FONT), MAX_LABEL_PX);
    if (shape === "icon") {
      var lblW = Math.min(approxTextWidth(label, ICON_FONT), MAX_LABEL_PX);
      return { w: Math.max(64, lblW + 24), h: 70 };
    }
    if (shape === "decision") {
      // Diamonds need extra room — text sits in the narrow middle band.
      var dw = Math.max(96, textW * 1.7 + 36);
      return { w: dw, h: Math.max(64, dw * 0.62) };
    }
    if (shape === "database") {
      return { w: Math.max(96, textW + 44), h: 64 };
    }
    if (shape === "pill") {
      return { w: Math.max(80, textW + 44), h: 40 };
    }
    // box (default)
    return { w: Math.max(88, textW + 36), h: 44 };
  }

  // ──────────────────────────────────────────────────────────────────────
  // Render one node group at its laid-out centre.
  // ──────────────────────────────────────────────────────────────────────
  function renderNode(node, layout, pal) {
    var shape = node.shape || "box";
    var w = layout.width, h = layout.height;
    var cx = layout.x, cy = layout.y;
    var x0 = cx - w / 2, y0 = cy - h / 2;

    var g = el("g", {
      "class": "npd-node",
      transform: "translate(" + x0 + "," + y0 + ")",
      "data-id": node.id
    });

    // Shape
    if (shape === "pill") g.appendChild(shapePill(w, h, pal));
    else if (shape === "decision") g.appendChild(shapeDecision(w, h, pal));
    else if (shape === "database") g.appendChild(shapeDatabase(w, h, pal));
    else if (shape === "icon") {
      // light card behind the icon+label for hit area + contrast
      g.appendChild(el("rect", {
        x: 0, y: 0, width: w, height: h, rx: 8, ry: 8,
        fill: pal.nodeFill, stroke: pal.nodeStroke, "stroke-width": 1.4,
        "stroke-dasharray": "1 0"
      }));
    } else g.appendChild(shapeBox(w, h, pal));

    var label = node.label != null ? String(node.label) : String(node.id || "");

    if (shape === "icon") {
      // Icon glyph centred in the upper area, label beneath.
      var iconName = node.icon || "process";
      var inner = ICONS[iconName] || ICONS.process;
      var size = 30;
      var ig = el("g", {
        transform: "translate(" + (w / 2 - size / 2) + "," + 8 + ") scale(" + (size / 24) + ")",
        fill: "none", stroke: "currentColor",
        "stroke-width": 1.6, "stroke-linecap": "round", "stroke-linejoin": "round",
        style: "color:" + pal.icon + ";"
      });
      // innerHTML on an SVG group — set via a parsed fragment for safety.
      setSvgInner(ig, inner);
      g.appendChild(ig);

      var disp = truncateToWidth(label, ICON_FONT, w - 12);
      var t = el("text", {
        x: w / 2, y: h - 12, "text-anchor": "middle",
        "font-size": ICON_FONT, "font-family": "system-ui, 'Segoe UI', sans-serif",
        fill: pal.nodeText
      });
      t.textContent = disp;
      g.appendChild(t);
    } else {
      // Centre label, truncate to the inner width. The decision diamond is
      // widest at the vertical centre (where the text sits), so it only needs
      // a modest side margin — w*0.34 starved short labels (e.g. "Gamma").
      var pad = (shape === "decision") ? w * 0.22 : 14;
      var disp2 = truncateToWidth(label, FONT, w - pad * 2);
      var ty = h / 2;
      var t2 = el("text", {
        x: w / 2, y: ty, "text-anchor": "middle", "dominant-baseline": "central",
        "font-size": FONT, "font-family": "system-ui, 'Segoe UI', sans-serif",
        fill: pal.nodeText, "font-weight": 500
      });
      t2.textContent = disp2;
      g.appendChild(t2);
    }

    // Tooltip: SVG <title> for native + a flag for the styled hover box.
    // truncated must reflect the string we ACTUALLY drew (icon→disp, else→disp2),
    // not a re-derived width — otherwise a truncated label can lose its full
    // text on hover (the decision-diamond "Gam…"-with-no-tooltip bug).
    var shownStr = (shape === "icon") ? disp : disp2;
    var truncated = (shownStr !== label);
    var tip = node.hover || (truncated ? label : "");
    if (tip) {
      var title = el("title", null);
      title.textContent = (node.hover ? node.hover : label);
      g.appendChild(title);
      g.setAttribute("data-hover", node.hover ? node.hover : label);
      g.style.cursor = "help";
      attachHover(g, node.hover ? node.hover : label, cx, y0);
    }

    return g;
  }

  // Parse a markup string into the given SVG group. Uses a throwaway <svg>
  // wrapper so namespaced children are created correctly across engines.
  function setSvgInner(group, markup) {
    var wrap = "<svg xmlns='" + SVGNS + "'>" + markup + "</svg>";
    var doc = new DOMParser().parseFromString(wrap, "image/svg+xml");
    var root = doc.documentElement;
    var kids = root.childNodes;
    for (var i = 0; i < kids.length; i++) {
      group.appendChild(document.importNode(kids[i], true));
    }
  }

  // ──────────────────────────────────────────────────────────────────────
  // Styled hover box (HTML overlay). The SVG <title> alone is an OS tooltip;
  // this gives a themed box that shows the full detail while the shape stays
  // readable. Positioned in *screen* space from the node's graph-space anchor.
  // ──────────────────────────────────────────────────────────────────────
  function ensureHoverBox() {
    if (state.hoverBox) return state.hoverBox;
    var d = document.createElement("div");
    d.id = "npd-hover";
    d.style.cssText = [
      "position:fixed", "pointer-events:none", "z-index:50",
      "max-width:280px", "padding:7px 10px", "border-radius:7px",
      "font:12px/1.4 system-ui,'Segoe UI',sans-serif",
      "background:rgba(33,39,51,0.96)", "color:#f4f4f4",
      "border:1px solid rgba(255,255,255,0.14)",
      "box-shadow:0 6px 20px rgba(0,0,0,0.28)",
      "opacity:0", "transition:opacity .08s", "white-space:normal",
      "word-break:break-word", "display:none"
    ].join(";");
    document.body.appendChild(d);
    state.hoverBox = d;
    return d;
  }

  function attachHover(g, text, anchorCx, anchorTop) {
    g.addEventListener("mouseenter", function (ev) {
      var box = ensureHoverBox();
      box.textContent = text;
      box.style.display = "block";
      positionHover(box, ev);
      box.style.opacity = "1";
    });
    g.addEventListener("mousemove", function (ev) {
      if (state.hoverBox && state.hoverBox.style.display === "block") {
        positionHover(state.hoverBox, ev);
      }
    });
    g.addEventListener("mouseleave", function () {
      if (state.hoverBox) {
        state.hoverBox.style.opacity = "0";
        state.hoverBox.style.display = "none";
      }
    });
  }

  function positionHover(box, ev) {
    var pad = 14;
    var x = ev.clientX + pad;
    var y = ev.clientY + pad;
    var r = box.getBoundingClientRect();
    var vw = window.innerWidth, vh = window.innerHeight;
    if (x + r.width + 4 > vw) x = ev.clientX - r.width - pad;
    if (y + r.height + 4 > vh) y = ev.clientY - r.height - pad;
    if (x < 4) x = 4;
    if (y < 4) y = 4;
    box.style.left = x + "px";
    box.style.top = y + "px";
  }

  // ──────────────────────────────────────────────────────────────────────
  // Edge rendering. dagre gives an array of points; we draw a smooth-ish
  // polyline (rounded via a simple Catmull-ish path) with an arrowhead at
  // the target (and at the source too when bidirectional). The label sits
  // at the mid-point with a small backing rect for legibility.
  // ──────────────────────────────────────────────────────────────────────
  function pointsToPath(pts) {
    if (!pts || !pts.length) return "";
    var d = "M" + pts[0].x + "," + pts[0].y;
    for (var i = 1; i < pts.length; i++) {
      d += " L" + pts[i].x + "," + pts[i].y;
    }
    return d;
  }

  function midPoint(pts) {
    if (!pts || pts.length === 0) return { x: 0, y: 0 };
    if (pts.length === 1) return { x: pts[0].x, y: pts[0].y };
    var mid = Math.floor(pts.length / 2);
    if (pts.length % 2 === 0) {
      var a = pts[mid - 1], b = pts[mid];
      return { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
    }
    return { x: pts[mid].x, y: pts[mid].y };
  }

  function renderEdge(pts, edge, pal, markerEnd, markerStart) {
    var g = el("g", { "class": "npd-edge" });
    var path = el("path", {
      d: pointsToPath(pts),
      fill: "none", stroke: pal.edge, "stroke-width": 1.6,
      "stroke-linecap": "round", "stroke-linejoin": "round"
    });
    path.setAttribute("marker-end", "url(#" + markerEnd + ")");
    if (edge.bidirectional) path.setAttribute("marker-start", "url(#" + markerStart + ")");
    g.appendChild(path);

    if (edge.label) {
      var m = midPoint(pts);
      var label = String(edge.label);
      var disp = truncateToWidth(label, 11, 150);
      var tw = approxTextWidth(disp, 11);
      g.appendChild(el("rect", {
        x: m.x - tw / 2 - 4, y: m.y - 9, width: tw + 8, height: 16, rx: 3,
        fill: pal.bg, opacity: 0.92
      }));
      var t = el("text", {
        x: m.x, y: m.y, "text-anchor": "middle", "dominant-baseline": "central",
        "font-size": 11, "font-family": "system-ui, 'Segoe UI', sans-serif",
        fill: pal.edgeText
      });
      t.textContent = disp;
      if (disp !== label) {
        var ti = el("title", null);
        ti.textContent = label;
        t.appendChild(ti);
      }
      g.appendChild(t);
    }
    return g;
  }

  // ──────────────────────────────────────────────────────────────────────
  // Build the whole drawing from the parsed graph.
  // ──────────────────────────────────────────────────────────────────────
  function buildDrawing(graph) {
    var pal = palette(graph.palette);
    var nodes = Array.isArray(graph.nodes) ? graph.nodes : [];
    var edges = Array.isArray(graph.edges) ? graph.edges : [];

    var Graph = global.dagre && global.dagre.graphlib && global.dagre.graphlib.Graph;
    if (!Graph) throw new Error("dagre not loaded (lib/dagre.min.js missing)");

    var dg = new global.dagre.graphlib.Graph({ multigraph: true });
    dg.setGraph({ rankdir: "TB", nodesep: 42, ranksep: 56, marginx: 16, marginy: 16 });
    dg.setDefaultEdgeLabel(function () { return {}; });

    var nodeById = {};
    nodes.forEach(function (n) {
      if (!n || n.id == null) return;
      nodeById[n.id] = n;
      var s = sizeFor(n);
      dg.setNode(String(n.id), { width: s.w, height: s.h });
    });

    // Edges: skip dangling endpoints (parser should not emit them, but be safe).
    var validEdges = [];
    edges.forEach(function (e, i) {
      if (!e || e.from == null || e.to == null) return;
      if (!dg.hasNode(String(e.from)) || !dg.hasNode(String(e.to))) return;
      var name = "e" + i;
      dg.setEdge(String(e.from), String(e.to), {}, name);
      validEdges.push({ edge: e, name: name });
    });

    global.dagre.layout(dg);

    // Root drawing group.
    var drawing = el("g", { "class": "npd-drawing" });

    // Header band (title) drawn above the laid-out graph.
    var headerH = 0;
    var gInfo = dg.graph();
    var graphW = gInfo.width || 0;
    var graphH = gInfo.height || 0;
    var headerY = -54;
    if (graph.title) {
      headerH = 54;
      var ht = el("text", {
        x: 0, y: headerY + 22,
        "font-size": 19, "font-weight": 700,
        "font-family": "system-ui, 'Segoe UI', sans-serif",
        fill: pal.header
      });
      ht.textContent = String(graph.title);
      drawing.appendChild(ht);
      // subtle rule under the title
      drawing.appendChild(el("line", {
        x1: 0, y1: headerY + 38, x2: Math.max(graphW, 160), y2: headerY + 38,
        stroke: pal.captionBorder, "stroke-width": 1
      }));
    }

    // Edges first (under nodes).
    var edgeLayer = el("g", { "class": "npd-edges" });
    validEdges.forEach(function (ve) {
      var ed = dg.edge(String(ve.edge.from), String(ve.edge.to), ve.name);
      if (!ed || !ed.points) return;
      edgeLayer.appendChild(renderEdge(ed.points, ve.edge, pal, "npd-arrow", "npd-arrow-start"));
    });
    drawing.appendChild(edgeLayer);

    // Nodes on top.
    var nodeLayer = el("g", { "class": "npd-nodes" });
    dg.nodes().forEach(function (id) {
      var n = nodeById[id];
      if (!n) return;
      var ly = dg.node(id);
      nodeLayer.appendChild(renderNode(n, ly, pal));
    });
    drawing.appendChild(nodeLayer);

    // Caption boxes (textboxes) below the graph.
    var captions = Array.isArray(graph.textboxes) ? graph.textboxes : [];
    var captionTop = graphH + 22;
    var capWidth = Math.max(graphW, 220);
    captions.forEach(function (cap, i) {
      if (cap == null) return;
      var text = String(cap);
      var cg = renderCaption(text, capWidth, pal);
      cg.setAttribute("transform", "translate(0," + captionTop + ")");
      drawing.appendChild(cg);
      captionTop += cg._height + 10;
    });

    // Compute bbox in graph coords (header above, captions below).
    var minY = (graph.title ? headerY : 0);
    var maxY = captionTop;
    var minX = 0;
    var maxX = Math.max(graphW, capWidth);
    return {
      drawing: drawing,
      bbox: { x: minX, y: minY, w: (maxX - minX), h: (maxY - minY) },
      pal: pal
    };
  }

  // Word-wrap a caption into <= maxWidth and return a <g> with a backing box.
  function renderCaption(text, maxWidth, pal) {
    var fs = 12;
    var innerW = maxWidth - 24;
    var words = text.split(/\s+/);
    var lines = [];
    var cur = "";
    words.forEach(function (w) {
      var trial = cur ? cur + " " + w : w;
      if (approxTextWidth(trial, fs) > innerW && cur) {
        lines.push(cur);
        cur = w;
      } else {
        cur = trial;
      }
    });
    if (cur) lines.push(cur);
    if (!lines.length) lines.push("");

    var lineH = 17;
    var boxH = lines.length * lineH + 16;
    var g = el("g", { "class": "npd-caption" });
    g.appendChild(el("rect", {
      x: 0, y: 0, width: maxWidth, height: boxH, rx: 6,
      fill: pal.captionBg, stroke: pal.captionBorder, "stroke-width": 1
    }));
    lines.forEach(function (ln, i) {
      var t = el("text", {
        x: 12, y: 12 + i * lineH + 6,
        "font-size": fs, "font-family": "system-ui, 'Segoe UI', sans-serif",
        fill: pal.caption
      });
      t.textContent = ln;
      g.appendChild(t);
    });
    g._height = boxH;
    return g;
  }

  // ──────────────────────────────────────────────────────────────────────
  // Markers (arrowheads). Created once into <defs>, recoloured per render
  // to match the palette edge colour.
  // ──────────────────────────────────────────────────────────────────────
  function ensureDefs(svg, pal) {
    var defs = svg.querySelector("defs");
    if (!defs) {
      defs = el("defs", null);
      svg.appendChild(defs);
    }
    defs.innerHTML = "";
    var end = el("marker", {
      id: "npd-arrow", viewBox: "0 0 10 10", refX: 9, refY: 5,
      markerWidth: 7, markerHeight: 7, orient: "auto-start-reverse"
    });
    end.appendChild(el("path", { d: "M0,0 L10,5 L0,10 z", fill: pal.edge }));
    defs.appendChild(end);

    var start = el("marker", {
      id: "npd-arrow-start", viewBox: "0 0 10 10", refX: 1, refY: 5,
      markerWidth: 7, markerHeight: 7, orient: "auto-start-reverse"
    });
    start.appendChild(el("path", { d: "M10,0 L0,5 L10,10 z", fill: pal.edge }));
    defs.appendChild(start);
  }

  // ──────────────────────────────────────────────────────────────────────
  // Viewport transform helpers (pan / zoom / fit).
  // ──────────────────────────────────────────────────────────────────────
  function applyTransform() {
    if (!state.viewport) return;
    state.viewport.setAttribute(
      "transform",
      "translate(" + state.tx + "," + state.ty + ") scale(" + state.scale + ")"
    );
  }

  function fit() {
    if (!state.svg || !state.bbox) return;
    var rect = state.svg.getBoundingClientRect();
    var vw = rect.width || state.svg.clientWidth || 800;
    var vh = rect.height || state.svg.clientHeight || 600;
    var b = state.bbox;
    var pad = 40;
    if (b.w <= 0 || b.h <= 0) {
      state.scale = 1; state.tx = pad; state.ty = pad; applyTransform(); return;
    }
    var sx = (vw - pad * 2) / b.w;
    var sy = (vh - pad * 2) / b.h;
    var s = Math.min(sx, sy);
    if (!isFinite(s) || s <= 0) s = 1;
    s = Math.min(s, 1.6);            // don't over-zoom tiny diagrams
    state.scale = s;
    // Centre the drawing in the viewport.
    state.tx = (vw - b.w * s) / 2 - b.x * s;
    state.ty = (vh - b.h * s) / 2 - b.y * s;
    applyTransform();
  }

  function wireInteractions() {
    var svg = state.svg;
    var dragging = false, lastX = 0, lastY = 0;

    svg.addEventListener("mousedown", function (e) {
      if (e.button !== 0) return;
      dragging = true;
      lastX = e.clientX; lastY = e.clientY;
      svg.style.cursor = "grabbing";
    });
    window.addEventListener("mousemove", function (e) {
      if (!dragging) return;
      state.tx += (e.clientX - lastX);
      state.ty += (e.clientY - lastY);
      lastX = e.clientX; lastY = e.clientY;
      applyTransform();
    });
    window.addEventListener("mouseup", function () {
      dragging = false;
      svg.style.cursor = "grab";
    });

    svg.addEventListener("wheel", function (e) {
      e.preventDefault();
      var rect = svg.getBoundingClientRect();
      var mx = e.clientX - rect.left;
      var my = e.clientY - rect.top;
      var factor = e.deltaY < 0 ? 1.12 : 1 / 1.12;
      var newScale = Math.max(0.08, Math.min(8, state.scale * factor));
      // Zoom toward the cursor: keep the graph point under the cursor fixed.
      var gx = (mx - state.tx) / state.scale;
      var gy = (my - state.ty) / state.scale;
      state.scale = newScale;
      state.tx = mx - gx * newScale;
      state.ty = my - gy * newScale;
      applyTransform();
    }, { passive: false });

    // double-click anywhere to re-fit
    svg.addEventListener("dblclick", function () { fit(); });
    svg.style.cursor = "grab";

    window.addEventListener("resize", function () {
      // keep transform; nothing to recompute besides bounds-driven fit on demand
    });
  }

  // ──────────────────────────────────────────────────────────────────────
  // Mount: build (or reuse) the outer <svg> + viewport <g> and interactions.
  // ──────────────────────────────────────────────────────────────────────
  function ensureCanvas() {
    var host = document.getElementById("npd-canvas");
    if (!host) {
      host = document.createElement("div");
      host.id = "npd-canvas";
      host.style.cssText = "position:absolute;inset:0;width:100%;height:100%;";
      document.body.appendChild(host);
    }
    if (state.svg && state.svg.parentNode === host) return;

    host.innerHTML = "";
    var svg = el("svg", {
      id: "npd-svg", width: "100%", height: "100%",
      xmlns: SVGNS, "xmlns:xlink": "http://www.w3.org/1999/xlink"
    });
    svg.style.cssText = "display:block;width:100%;height:100%;touch-action:none;";
    var bgRect = el("rect", {
      id: "npd-bg", x: 0, y: 0, width: "100%", height: "100%", fill: "#fbfaf7"
    });
    svg.appendChild(bgRect);
    var viewport = el("g", { id: "npd-viewport" });
    svg.appendChild(viewport);
    host.appendChild(svg);

    state.svg = svg;
    state.viewport = viewport;
    wireInteractions();
  }

  // ──────────────────────────────────────────────────────────────────────
  // PUBLIC: render a graph (string or object).
  // ──────────────────────────────────────────────────────────────────────
  function npdRender(jsonStringOrObject) {
    var graph;
    try {
      graph = (typeof jsonStringOrObject === "string")
        ? JSON.parse(jsonStringOrObject)
        : jsonStringOrObject;
    } catch (e) {
      showError("Could not parse diagram JSON: " + String(e));
      return;
    }
    if (!graph || typeof graph !== "object") {
      showError("Diagram JSON is empty or not an object.");
      return;
    }
    state.lastGraph = graph;

    ensureCanvas();
    var pal = palette(graph.palette);

    // Background to palette.
    var bg = state.svg.querySelector("#npd-bg");
    if (bg) bg.setAttribute("fill", pal.bg);
    document.body.style.background = pal.bg;

    ensureDefs(state.svg, pal);

    var built;
    try {
      built = buildDrawing(graph);
    } catch (e) {
      showError("Render failed: " + String(e && e.message ? e.message : e));
      return;
    }

    // Swap in the new drawing.
    state.viewport.innerHTML = "";
    state.viewport.appendChild(built.drawing);
    state.drawing = built.drawing;
    state.bbox = built.bbox;

    // Defer fit until layout has a measurable viewport size.
    fit();
    requestAnimationFrame(fit);
  }

  function showError(msg) {
    ensureCanvas();
    state.viewport.innerHTML = "";
    state.bbox = { x: 0, y: 0, w: 400, h: 80 };
    var g = el("g", null);
    var t = el("text", {
      x: 20, y: 36, "font-size": 14,
      "font-family": "system-ui, 'Segoe UI', sans-serif", fill: "#b00020"
    });
    t.textContent = msg;
    g.appendChild(t);
    state.viewport.appendChild(g);
    state.tx = 0; state.ty = 0; state.scale = 1; applyTransform();
  }

  // ──────────────────────────────────────────────────────────────────────
  // PUBLIC: fit.
  // ──────────────────────────────────────────────────────────────────────
  function npdFit() { fit(); }

  // ──────────────────────────────────────────────────────────────────────
  // Export. Serialise the current drawing into a standalone, sized <svg>
  // (NOT the full-viewport one — we want the diagram tightly cropped to its
  // bbox, with the palette background). PNG/WebP/JPEG rasterise that SVG via
  // an Image onto a canvas; SVG returns the XML string. All results land on
  // window['_npd_result_'+slot] so the C++ side can poll one slot name.
  // ──────────────────────────────────────────────────────────────────────
  function serializeStandaloneSvg() {
    if (!state.drawing || !state.bbox) return null;
    var b = state.bbox;
    var pad = 24;
    var w = Math.max(1, Math.ceil(b.w + pad * 2));
    var h = Math.max(1, Math.ceil(b.h + pad * 2));
    var pal = palette(state.lastGraph ? state.lastGraph.palette : "default");

    var out = el("svg", {
      xmlns: SVGNS, "xmlns:xlink": "http://www.w3.org/1999/xlink",
      width: w, height: h, viewBox: "0 0 " + w + " " + h
    });
    out.appendChild(el("rect", { x: 0, y: 0, width: w, height: h, fill: pal.bg }));
    // Re-include the markers so arrowheads survive standalone.
    var defs = el("defs", null);
    var end = el("marker", {
      id: "npd-arrow", viewBox: "0 0 10 10", refX: 9, refY: 5,
      markerWidth: 7, markerHeight: 7, orient: "auto-start-reverse"
    });
    end.appendChild(el("path", { d: "M0,0 L10,5 L0,10 z", fill: pal.edge }));
    defs.appendChild(end);
    var start = el("marker", {
      id: "npd-arrow-start", viewBox: "0 0 10 10", refX: 1, refY: 5,
      markerWidth: 7, markerHeight: 7, orient: "auto-start-reverse"
    });
    start.appendChild(el("path", { d: "M10,0 L0,5 L10,10 z", fill: pal.edge }));
    defs.appendChild(start);
    out.appendChild(defs);

    var clone = state.drawing.cloneNode(true);
    var shift = el("g", { transform: "translate(" + (pad - b.x) + "," + (pad - b.y) + ")" });
    shift.appendChild(clone);
    out.appendChild(shift);

    var xml = new XMLSerializer().serializeToString(out);
    if (xml.indexOf("xmlns=") === -1) {
      xml = xml.replace("<svg ", "<svg xmlns='" + SVGNS + "' ");
    }
    return { xml: xml, w: w, h: h };
  }

  function setResult(slot, value) {
    global["_npd_result_" + slot] = value;
    // Back-compat with the vega export-slot convention used elsewhere.
    global["_notepatra_export_" + slot] = value;
  }

  function exportRaster(slot, mime, scale, quality) {
    var s = serializeStandaloneSvg();
    if (!s) { setResult(slot, "__nodiagram__"); return; }
    var sc = (typeof scale === "number" && scale > 0) ? Math.min(scale, 8) : 1;
    var img = new Image();
    var svgBlobUrl = "data:image/svg+xml;charset=utf-8," + encodeURIComponent(s.xml);
    img.onload = function () {
      try {
        var canvas = document.createElement("canvas");
        canvas.width = Math.max(1, Math.round(s.w * sc));
        canvas.height = Math.max(1, Math.round(s.h * sc));
        var ctx = canvas.getContext("2d");
        // JPEG has no alpha — paint the palette bg first.
        if (mime === "image/jpeg") {
          var pal = palette(state.lastGraph ? state.lastGraph.palette : "default");
          ctx.fillStyle = pal.bg;
          ctx.fillRect(0, 0, canvas.width, canvas.height);
        }
        ctx.setTransform(sc, 0, 0, sc, 0, 0);
        ctx.drawImage(img, 0, 0);
        var url = (mime === "image/png")
          ? canvas.toDataURL("image/png")
          : canvas.toDataURL(mime, (typeof quality === "number" ? quality : 0.92));
        setResult(slot, url);
      } catch (e) {
        setResult(slot, "__err__:" + String(e));
      }
    };
    img.onerror = function () { setResult(slot, "__err__:image-load-failed"); };
    img.src = svgBlobUrl;
  }

  function _npd_export_png(slot, scale) { exportRaster(slot, "image/png", scale); }
  function _npd_export_webp(slot, scale) { exportRaster(slot, "image/webp", scale, 0.92); }
  function _npd_export_jpeg(slot, scale) { exportRaster(slot, "image/jpeg", scale, 0.92); }

  function _npd_export_svg(slot) {
    var s = serializeStandaloneSvg();
    if (!s) { setResult(slot, "__nodiagram__"); return ""; }
    var doc = "<?xml version='1.0' encoding='UTF-8'?>\n" + s.xml;
    setResult(slot, doc);
    return doc;
  }

  // ──────────────────────────────────────────────────────────────────────
  // Expose.
  // ──────────────────────────────────────────────────────────────────────
  global.npdRender = npdRender;
  global.npdFit = npdFit;
  global._npd_export_png = _npd_export_png;
  global._npd_export_webp = _npd_export_webp;
  global._npd_export_jpeg = _npd_export_jpeg;
  global._npd_export_svg = _npd_export_svg;

  // Convenience for the shell / debugging.
  global.npd = {
    render: npdRender, fit: npdFit, palettes: PALETTES, icons: ICONS,
    state: state
  };

})(typeof window !== "undefined" ? window : this);
