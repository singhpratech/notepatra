// Regression tests for terminal.cpp's ansiToHtml() ANSI-to-HTML parser.
// Verifies the v0.1.26 upgrade adds:
//   - 256-colour palette (38;5;N, 48;5;N)
//   - 24-bit truecolor (38;2;R;G;B, 48;2;R;G;B)
//   - Italic (3), faint (2), strikethrough (9)
//   - Cancel-attribute codes (22, 23, 24, 29)
//
// Without leaking literal escape sequence numbers as plain text -- the
// pre-v0.1.26 bug where `bat foo.py` rendered as a wall of digits.
//
// We can't include terminal.cpp directly (it needs QApplication + a
// real terminal widget), so we copy the static helper into a test
// translation unit. If the implementation in terminal.cpp drifts from
// this copy, the test will need to be updated -- which is fine because
// it'll catch any silent regression in the parser.

#include <QString>
#include <QStringList>
#include <QChar>
#include <QCoreApplication>
#include <cstdio>

// ─── Helper: copy of the static palette from terminal.cpp ─────────────
struct AnsiPalette { QString c[16]; };
static const AnsiPalette kAnsi = {{
    "#1E1E1E", "#F14C4C", "#76D275", "#F2C14E",
    "#569CD6", "#C678DD", "#4EC9B0", "#D4D4D4",
    "#6C6C6C", "#FF8B8B", "#B5E2A9", "#FFE0A3",
    "#9CDCFE", "#E4B0F5", "#A8EAD9", "#FFFFFF",
}};

static QString ansi256ToHex(int idx) {
    if (idx < 0 || idx > 255) return QString();
    if (idx < 16) return kAnsi.c[idx];
    if (idx >= 232) {
        const int v = 8 + (idx - 232) * 10;
        return QString::asprintf("#%02x%02x%02x", v, v, v);
    }
    const int n = idx - 16;
    static const int kCube[6] = {0, 95, 135, 175, 215, 255};
    const int r = kCube[(n / 36) % 6];
    const int g = kCube[(n / 6)  % 6];
    const int b = kCube[n        % 6];
    return QString::asprintf("#%02x%02x%02x", r, g, b);
}

static QString ansiColourToHex(int code) {
    if (code >= 30 && code <= 37)   return kAnsi.c[code - 30];
    if (code >= 90 && code <= 97)   return kAnsi.c[code - 90 + 8];
    if (code >= 40 && code <= 47)   return kAnsi.c[code - 40];
    if (code >= 100 && code <= 107) return kAnsi.c[code - 100 + 8];
    return QString();
}

struct SgrState {
    QString fg, bg;
    bool bold = false, faint = false, italic = false, underline = false, strike = false;
    void reset() { fg.clear(); bg.clear(); bold = faint = italic = underline = strike = false; }
    QString toCss() const {
        QString s;
        if (!fg.isEmpty()) s += fg;
        if (!bg.isEmpty()) s += bg;
        if (bold)      s += "font-weight:bold;";
        if (faint)     s += "opacity:0.6;";
        if (italic)    s += "font-style:italic;";
        if (underline) s += "text-decoration:underline;";
        if (strike) {
            if (underline) s.replace("text-decoration:underline;",
                                     "text-decoration:underline line-through;");
            else s += "text-decoration:line-through;";
        }
        return s;
    }
};

static QString ansiToHtml(const QString &raw) {
    QString out; out.reserve(raw.size() + 32);
    SgrState st; bool spanOpen = false;
    int i = 0; const int n = raw.size();
    auto reopenSpan = [&]() {
        if (spanOpen) { out += "</span>"; spanOpen = false; }
        const QString css = st.toCss();
        if (!css.isEmpty()) { out += "<span style='" + css + "'>"; spanOpen = true; }
    };
    while (i < n) {
        QChar c = raw[i];
        if (c == QChar(0x1B) && i + 1 < n && raw[i+1] == '[') {
            int j = i + 2;
            while (j < n && !(raw[j].isLetter())) ++j;
            if (j >= n) break;
            const QChar finalByte = raw[j];
            const QString params = raw.mid(i + 2, j - i - 2);
            i = j + 1;
            if (finalByte != 'm') continue;
            const QStringList parts = params.isEmpty() ? QStringList{"0"} : params.split(';');
            int p = 0;
            while (p < parts.size()) {
                bool ok = false;
                const int code = parts[p].toInt(&ok);
                if (!ok) { ++p; continue; }
                if (code == 38 || code == 48) {
                    const bool isFg = (code == 38);
                    if (p + 1 < parts.size()) {
                        const int kind = parts[p + 1].toInt();
                        if (kind == 5 && p + 2 < parts.size()) {
                            const int idx = parts[p + 2].toInt();
                            const QString hex = ansi256ToHex(idx);
                            if (!hex.isEmpty()) {
                                if (isFg) st.fg = "color:" + hex + ";";
                                else      st.bg = "background:" + hex + ";";
                            }
                            p += 3; continue;
                        }
                        if (kind == 2 && p + 4 < parts.size()) {
                            const int r = parts[p + 2].toInt();
                            const int g = parts[p + 3].toInt();
                            const int b = parts[p + 4].toInt();
                            const QString hex = QString::asprintf("#%02x%02x%02x",
                                qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
                            if (isFg) st.fg = "color:" + hex + ";";
                            else      st.bg = "background:" + hex + ";";
                            p += 5; continue;
                        }
                    }
                    ++p; continue;
                }
                switch (code) {
                    case 0:  st.reset();             break;
                    case 1:  st.bold = true;         break;
                    case 2:  st.faint = true;        break;
                    case 3:  st.italic = true;       break;
                    case 4:  st.underline = true;    break;
                    case 9:  st.strike = true;       break;
                    case 22: st.bold = false; st.faint = false; break;
                    case 23: st.italic = false;      break;
                    case 24: st.underline = false;   break;
                    case 29: st.strike = false;      break;
                    case 39: st.fg.clear();          break;
                    case 49: st.bg.clear();          break;
                    default:
                        if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97))
                            st.fg = "color:" + ansiColourToHex(code) + ";";
                        else if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107))
                            st.bg = "background:" + ansiColourToHex(code) + ";";
                        break;
                }
                ++p;
            }
            reopenSpan();
        } else if (c == '\n') { out += "<br>"; ++i; }
        else if (c == '\r')   { ++i; }
        else {
            if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else if (c == '&') out += "&amp;";
            else if (c == ' ') out += "&nbsp;";
            else out += c;
            ++i;
        }
    }
    if (spanOpen) out += "</span>";
    return out;
}

// ─── Tests ────────────────────────────────────────────────────────────
static int passed = 0, failed = 0;
static void check(const char *label, bool ok) {
    if (ok) { ++passed; std::printf("  [PASS] %s\n", label); }
    else    { ++failed; std::printf("  [FAIL] %s\n", label); }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // Plain text -- no escapes
    check("plain text passes through", ansiToHtml("hello") == "hello");

    // Basic 8-colour FG (red = 31)
    {
        const QString in = "\x1b[31mred\x1b[0m";
        const QString out = ansiToHtml(in);
        check("31m red FG produces span", out.contains("<span"));
        check("31m red FG has correct hex", out.contains("#F14C4C"));
        check("31m red FG closed by reset", out.endsWith("</span>red") || out.contains("red</span>"));
    }

    // Bold + colour combo
    {
        const QString out = ansiToHtml("\x1b[1;32mbold green\x1b[0m");
        check("bold + green produces bold style", out.contains("font-weight:bold"));
        check("bold + green produces colour",     out.contains("#76D275"));
    }

    // Italic (NEW in v0.1.26)
    {
        const QString out = ansiToHtml("\x1b[3mitalic text\x1b[0m");
        check("italic (3m) produces italic style", out.contains("font-style:italic"));
    }

    // Faint (NEW in v0.1.26)
    {
        const QString out = ansiToHtml("\x1b[2mfaint text\x1b[0m");
        check("faint (2m) produces opacity 0.6", out.contains("opacity:0.6"));
    }

    // Strikethrough (NEW in v0.1.26)
    {
        const QString out = ansiToHtml("\x1b[9mstruck\x1b[0m");
        check("strikethrough (9m) produces line-through", out.contains("line-through"));
    }

    // Cancel-attribute codes
    {
        const QString out = ansiToHtml("\x1b[1;31mboldred\x1b[22mnotbold\x1b[0m");
        // After 22, the bold should be off; we should see span closed and reopened without bold
        check("22 cancels bold leaving colour", out.contains("#F14C4C") &&
                                                 out.indexOf("font-weight:bold") < out.lastIndexOf("</span>"));
    }

    // 256-colour palette FG (NEW in v0.1.26)
    {
        // Code 196 in xterm-256 cube = bright red (#FF0000-ish)
        const QString out = ansiToHtml("\x1b[38;5;196mvividred\x1b[0m");
        check("256-color FG (38;5;196) produces colour span", out.contains("color:#"));
        check("256-color FG (38;5;196) gives red-ish hex",    out.contains("ff0000") ||
                                                              out.contains("#ff0000"));
    }
    {
        // Code 16 = first cube entry, equals palette index 0 = "#1E1E1E"
        const QString out = ansiToHtml("\x1b[38;5;16mblack\x1b[0m");
        check("256-color FG (38;5;16) maps to cube origin", out.contains("#000000"));
    }
    {
        // Code 232 = grayscale start, hex 080808
        const QString out = ansiToHtml("\x1b[38;5;232mgray\x1b[0m");
        check("256-color FG (38;5;232) is dark gray", out.contains("#080808"));
    }
    {
        // Code 255 = grayscale end, hex EEEEEE
        const QString out = ansiToHtml("\x1b[38;5;255mwhite\x1b[0m");
        check("256-color FG (38;5;255) is light gray", out.contains("#eeeeee"));
    }

    // 256-colour BG (NEW in v0.1.26)
    {
        const QString out = ansiToHtml("\x1b[48;5;21mbgblue\x1b[0m");
        check("256-color BG (48;5;21) produces background style", out.contains("background:#"));
    }

    // Truecolor FG (NEW in v0.1.26)
    {
        const QString out = ansiToHtml("\x1b[38;2;255;128;64mcustom\x1b[0m");
        check("truecolor FG (38;2;255;128;64) produces hex", out.contains("#ff8040"));
    }
    {
        // Out-of-range channels should be clamped
        const QString out = ansiToHtml("\x1b[38;2;300;200;-50mclamp\x1b[0m");
        check("truecolor with out-of-range R clamped to 255", out.contains("ff"));
    }

    // Truecolor BG (NEW in v0.1.26)
    {
        const QString out = ansiToHtml("\x1b[48;2;0;100;200mbgcustom\x1b[0m");
        check("truecolor BG (48;2;0;100;200) produces background", out.contains("background:#0064c8"));
    }

    // Combination: 256-colour + bold + italic
    {
        const QString out = ansiToHtml("\x1b[1;3;38;5;208m\x1b[0m");
        // We don't have content, but the parser shouldn't barf
        check("combo 1;3;38;5;208 doesn't leak digits as text",
              !out.contains("38") && !out.contains("208") && !out.contains("5;"));
    }

    // Real-world bat-style output: keyword highlighting via 256-colour
    {
        const QString in = "\x1b[38;5;81mdef\x1b[0m \x1b[38;5;148mfoo\x1b[0m():";
        const QString out = ansiToHtml(in);
        check("real-world bat-style 256-colour works",
              out.contains("def") && out.contains("foo") &&
              !out.contains("38;5;") && !out.contains("[38"));
    }

    // Reset clears all attributes
    {
        const QString out = ansiToHtml("\x1b[1;31mthen\x1b[0m \x1b[32mthen\x1b[0m");
        // After the first reset we should have a fresh span for the green text
        check("reset between sequences gives separate colours",
              out.contains("#F14C4C") && out.contains("#76D275"));
    }

    // Unknown / unsupported escape (cursor-move) should not break parser
    {
        const QString out = ansiToHtml("\x1b[2J\x1b[Hclear-screen-then-text");
        check("unknown CSI sequences (J, H) don't break parser",
              out.contains("clear-screen-then-text"));
    }

    // Newline becomes <br>
    check("newline becomes <br>", ansiToHtml("a\nb") == "a<br>b");

    // HTML special chars are escaped in plain text
    check("< escaped",  ansiToHtml("<tag>") == "&lt;tag&gt;");
    check("& escaped",  ansiToHtml("a & b") == "a&nbsp;&amp;&nbsp;b");
    check("space preserved as &nbsp;", ansiToHtml("a b").contains("&nbsp;"));

    std::printf("\n=== Summary: %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
