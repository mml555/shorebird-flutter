#!/usr/bin/env bash
# Stops what start_native.sh started and removes what it left in the runner's
# temp directory. Safe to run when nothing was started. Run it in an
# `if: always()` step: a composite action has no post step of its own.
#
#   stop_native.sh <runner temp dir>
#
# It keeps <state>/bin (Caddy, which actions/cache saves in its own post step,
# after this one) and removes the rest, which includes the extracted overlay
# (about 3 GB) and the downloaded release.
set -uo pipefail

tmp="$1"
state="$tmp/cdn-native"
if [ -x "$tmp/selfhost/selfhost/cdn/run_native.sh" ] && [ -d "$state" ]; then
  "$tmp/selfhost/selfhost/cdn/run_native.sh" stop --state "$state" || true
fi
rm -rf "$tmp/engine-release" "$tmp/selfhost"
if [ -d "$state" ]; then
  # Go's module cache is read-only unless built with -modcacherw.
  chmod -R u+w "$state" 2>/dev/null || true
  find "$state" -mindepth 1 -maxdepth 1 ! -name bin -exec rm -rf {} +
fi
exit 0
