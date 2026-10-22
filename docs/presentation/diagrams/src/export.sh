#!/usr/bin/env bash
# Export every (or the named) diagram to export/<name>.svg and export/<name>.png (2x). Needs draw.io desktop
# (scoop install extras/draw.io). Usage: ./export.sh [name ...]
set -euo pipefail
cd "$(dirname "$0")/.."
DRAWIO="${DRAWIO:-$HOME/scoop/apps/draw.io/current/draw.io.exe}"
mkdir -p export
names=("$@")
if [ ${#names[@]} -eq 0 ]; then
  for f in *.drawio; do names+=("${f%.drawio}"); done
fi
for n in "${names[@]}"; do
  "$DRAWIO" -x -f svg -o "export/$n.svg" "$n.drawio" < /dev/null > /dev/null 2>&1
  "$DRAWIO" -x -f png -s 2 -o "export/$n.png" "$n.drawio" < /dev/null > /dev/null 2>&1
  echo "exported export/$n"
done
