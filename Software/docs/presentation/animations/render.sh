#!/usr/bin/env bash
# Render every (or the named) scene at 1080p60 and copy the MP4s next to the other slides material.
#   ./render.sh [Scene ...]              English -> videos/
#   SNA_LANG=de ./render.sh [Scene ...]  German  -> ../de/videos/  (exported only, not used in the repo docs)
set -euo pipefail
cd "$(dirname "$0")"
PY=.venv/Scripts/python.exe
[ -x "$PY" ] || PY=.venv/bin/python
LANG_="${SNA_LANG:-en}"
if [ "$LANG_" = "en" ]; then media=media; out=videos; else media="media_$LANG_"; out="../$LANG_/videos"; fi
mkdir -p "$out"
declare -A file=(
  [PllSynthesis]=hardware_scenes [DirectionalCoupler]=hardware_scenes [LogDetector]=hardware_scenes
  [ImpedancePads]=hardware_scenes [PowerTree]=hardware_scenes
  [MeasurementFlow]=firmware_scenes [SweepPipeline]=firmware_scenes [Calibration]=firmware_scenes
  [SourceSafety]=firmware_scenes [ModesAndCommands]=firmware_scenes
)
scenes=("$@")
[ ${#scenes[@]} -eq 0 ] && scenes=(PllSynthesis DirectionalCoupler LogDetector ImpedancePads PowerTree
                                   MeasurementFlow SweepPipeline Calibration SourceSafety ModesAndCommands)
for s in "${scenes[@]}"; do
  f="${file[$s]}"
  SNA_LANG="$LANG_" "$PY" -m manim -qh --media_dir "$media" "$f.py" "$s" > /dev/null
  cp "$media/videos/$f/1080p60/$s.mp4" "$out/$s.mp4"
  echo "rendered $out/$s.mp4"
done
