#!/usr/bin/env bash
# Export every (or the named) diagram to <name>.svg and <name>.png (2x). Needs draw.io desktop
# (scoop install extras/draw.io). Usage: ./export.sh [name ...]
#   English (default): ../*.drawio        -> ../export/
#   SNA_LANG=de:       ../../de/drawio/*  -> ../../de/diagrams/   (run `SNA_LANG=de python build_*.py` first)
set -euo pipefail
cd "$(dirname "$0")/.."
DRAWIO="${DRAWIO:-$HOME/scoop/apps/draw.io/current/draw.io.exe}"
LANG_="${SNA_LANG:-en}"
if [ "$LANG_" = "en" ]; then src=.; out=export; else src="../$LANG_/drawio"; out="../$LANG_/diagrams"; fi
mkdir -p "$out"
names=("$@")
if [ ${#names[@]} -eq 0 ]; then
  for f in "$src"/*.drawio; do n="${f##*/}"; names+=("${n%.drawio}"); done
fi
for n in "${names[@]}"; do
  "$DRAWIO" -x -f svg -o "$out/$n.svg" "$src/$n.drawio" < /dev/null > /dev/null 2>&1
  "$DRAWIO" -x -f png -s 2 -o "$out/$n.png" "$src/$n.drawio" < /dev/null > /dev/null 2>&1
  echo "exported $out/$n"
done
