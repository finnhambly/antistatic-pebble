#!/bin/sh
# Screenshots of each level on one emulator: scripts/tour.sh flint [steps]
# Assumes the app is installed and running there.
set -e
P=${1:-flint}
OUT=${2:-shots}
mkdir -p "$OUT"
b() { pebble emu-button --emulator "$P" click "$@" >/dev/null 2>&1; }
shot() { pebble screenshot --emulator "$P" --no-open "$OUT/$P-$1.png" >/dev/null 2>&1; }
shot 1-list
b down; b down; b down; shot 2-list-count
b select; sleep 2; shot 3-tuner-count
b down --repeat 3; shot 4-tuner-moved
b select; shot 5-detail-count
b select; shot 6-detail-next
b back; b back; b up; b up; b up
b select; sleep 2; shot 7-tuner-date
b select; shot 8-detail-date
b back; b back
