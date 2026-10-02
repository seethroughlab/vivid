#!/usr/bin/env bash
# Gate 1 — set up the two-minute measurement session (docs/roadmap/gate1-measurement.md).
#
# Run this FIRST THING, before the creator sits down. It builds main, launches Vivid on a FRESH
# scratch copy of the song sketch, publishes one round (baseline + two directions the creator has
# not seen), and leaves the app in CREATE with the pending badge showing. It never opens Review.
#
#   bash mcp/gate1/setup_measurement.sh            # fresh round (default)
#   bash mcp/gate1/setup_measurement.sh --no-build # skip the build (binary already current)
#
# After it prints READY, say the scenario aloud, start the clock when the creator touches the
# mouse, and observe silently. Do not narrate the UI.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PROJECT="${VIVID_GATE1_PROJECT:-$HOME/Music/vivid-gate1/session-$(date +%Y%m%d-%H%M)}"
BIN="$REPO/app/build/Vivid.app/Contents/MacOS/Vivid"
BUILD=1
[[ "${1:-}" == "--no-build" ]] && BUILD=0

cd "$REPO"

if [[ $BUILD -eq 1 ]]; then
  echo "== building main =="
  cmake -S app -B app/build >/dev/null
  cmake --build app/build -j 8 --target vivid 2>&1 | tail -1
fi

# The export needs the display awake and the window frontmost (a sleeping/occluded surface collapses
# the frame loop), so hold the display for the session.
pgrep -f "caffeinate -dimsu" >/dev/null || (caffeinate -dimsu -t 7200 >/dev/null 2>&1 &)

echo "== launching Vivid =="
pkill -x Vivid 2>/dev/null || true
sleep 2
VIVID_NO_RECOVER=1 "$BIN" > /tmp/vivid-gate1-measurement.log 2>&1 &
sleep 7
osascript -e 'tell application "System Events" to set frontmost of (first process whose unix id is '"$(pgrep -x Vivid | head -1)"') to true' 2>/dev/null || true

echo "== publishing the round into $PROJECT =="
uv run --directory mcp python gate1/publish_review.py \
    --project "$PROJECT" --from-example --scene 0 --bars 8

# Confirm the app is in Create with exactly one open decision — never open Review here.
STATUS=$(curl -s -m 20 -X POST http://127.0.0.1:9876/work_status -d '{}')
OPEN=$(python3 -c "import json,sys; print(json.loads(sys.argv[1]).get('open_reviews', 0))" "$STATUS")

cat <<EOF

================================ READY ================================
Project : $PROJECT
Open decisions : $OPEN   (the badge on the Create | Review switch)
Log     : /tmp/vivid-gate1-measurement.log

Say this aloud, once, then stop talking:

  "Yesterday you asked for the chorus to feel more expansive, with the
   visuals opening up, and for the drums and the chord part to stay as
   they are. Two directions came back. Decide."

Start the clock when they touch the mouse. Stop at 'done' or 2:00.
Record against docs/roadmap/gate1-measurement.md; probes come after.
=======================================================================
EOF
