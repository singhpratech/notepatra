#!/usr/bin/env bash
# Run this AFTER you've clicked the Notepatra window in your taskbar to bring
# it to the foreground. Captures each major v0.1.58 feature in sequence.
# Between each capture, the script prompts you to interact (open Coding mode,
# type @, etc.) and waits for Enter.

set -u
cd "$(dirname "$0")/screenshots"

WID=$(wmctrl -l 2>/dev/null | grep -i 'notepatra' | grep -v Firefox | awk '{print $1}' | head -1)
if [[ -z "$WID" ]]; then
  echo "ERROR: no Notepatra window found. Open Notepatra first." >&2
  exit 1
fi

shoot() {
    local name="$1"
    local label="$2"
    local file="${name}.png"
    rm -f "$file"
    # Re-activate just before each shot in case focus drifted.
    wmctrl -i -a "$WID" 2>/dev/null
    sleep 0.4
    gnome-screenshot --window --file="$file" --include-border 2>/dev/null
    if [[ -f "$file" ]]; then
        size=$(stat -c%s "$file")
        echo "  ✓ $file · $((size/1024)) KB · $label"
    else
        echo "  ✗ $file failed"
    fi
}

prompt() {
    echo
    echo "▶ $1"
    echo "  (press Enter when ready, Ctrl+C to abort)"
    read -r _
}

echo "═══════════════════════════════════════════════════════════"
echo "Notepatra v0.1.58 screenshot capture · wid=$WID"
echo "═══════════════════════════════════════════════════════════"

prompt "Bring Notepatra to front. I'll capture the welcome / current state."
shoot "10-welcome" "welcome / startup state"

prompt "Open a code file (e.g. src/main.cpp). Bring Notepatra to front."
shoot "11-editor-cpp" "editor with C++ syntax highlighting"

prompt "Press Ctrl+Shift+A to open the AI dock. Bring Notepatra to front."
shoot "12-ai-dock-chat" "AI dock — Chat mode"

prompt "Toggle Coding Mode (the toggle in the AI dock). Bring Notepatra to front."
shoot "13-coding-mode-tabs" "Coding mode + Chat | Composer tabs visible"

prompt "Click the Composer tab. Bring Notepatra to front."
shoot "14-composer-empty" "Composer tab — empty Edit Plan list"

prompt "In the chat input, type @ to bring up the file mention picker. Bring Notepatra to front."
shoot "15-at-file-picker" "@file mention picker"

prompt "Press Esc to close picker. Select a few lines in the editor and press Ctrl+I."
shoot "16-inline-edit" "Ctrl+I inline edit modal"

prompt "Cancel the inline edit. Now Alt+drag in the editor to make a column selection."
shoot "17-multi-cursor" "Alt+drag multi-cursor selection"

prompt "Switch to Data Mode (toggle in the AI dock). Bring Notepatra to front."
shoot "18-data-mode" "Data Analyst mode"

prompt "Final shot — full main window."
shoot "19-final" "final state"

echo
echo "═══════════════════════════════════════════════════════════"
echo "Done. Captured screenshots:"
ls -la *.png 2>/dev/null | awk '{printf "  %s · %.1f KB\n", $NF, $5/1024}'
echo "Folder: $(pwd)"
