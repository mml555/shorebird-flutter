#!/usr/bin/env bash
# Starts the self-host CDN mirror on a self-hosted macOS runner, which is
# somebody's own machine, so nothing here may outlast the job or leave a trace
# outside the runner's temp directory:
#
#   start_native.sh <selfhost/cdn dir> <state dir>
#
# Differences from the GitHub-hosted path (action.yml):
#   * No Docker, no root and no root CA. Nothing is added to the OS or JDK
#     trust store. The mirror serves plain http on 127.0.0.1: Gradle (9.3.1 and
#     later, which Flutter 3.50 requires) lets plain http through to 127.0.0.1
#     only, and that is all the jobs need. Dart and curl do not mind http.
#   * Free ports, chosen here, so a second mirror or any other server on the
#     machine is not in the way. Caddy listens on 127.0.0.1 only; artifact_proxy
#     (no bind-address setting) listens on every interface.
#   * Go's module and build caches, pub's cache and the temp dir are redirected
#     under <state dir>, where stop_native.sh removes them.
#
# Writes FLUTTER_STORAGE_BASE_URL to $GITHUB_ENV once the mirror answers.
# Needs ENGINE, GITHUB_ENV and the programs run_native.sh needs (go unless a
# Caddy is cached in <state dir>/bin, dart, curl).
set -euo pipefail

cdn="$1" state="$2"
fail() { echo "::error::$*"; exit 1; }

mkdir -p "$state"
export TMPDIR="$state/tmp" GOPATH="$state/go/path" GOCACHE="$state/go/build" \
  GOMODCACHE="$state/go/mod" GOFLAGS=-modcacherw PUB_CACHE="$state/pub-cache"
mkdir -p "$TMPDIR"

busy() { (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null; }
# pick_port <var>: sets <var> to a free port no earlier call chose. It assigns
# rather than echoes: in a $(...) subshell its record of taken ports, and its
# fail, would not reach this shell, and two calls could pick the same port.
pick_port() {
  local p
  for _ in $(seq 1 200); do
    p=$((20000 + RANDOM % 30000))
    if ! busy "$p" && [[ " $taken " != *" $p "* ]]; then
      taken="$taken $p"
      printf -v "$1" '%s' "$p"
      return 0
    fi
  done
  fail "no free port found"
}
taken=""
pick_port http_port
pick_port proxy_port

# run_native.sh fixes artifact_proxy's port at 8081. Make it ours. A different
# run_native.sh (a new selfhost-ref) that already reads PROXY_PORT needs no
# patch; anything else is refused rather than run on 8081.
rn="$cdn/run_native.sh"
if grep -qE '^PROXY_PORT="\$\{PROXY_PORT:-' "$rn"; then
  :
elif grep -qxF 'PROXY_PORT=8081' "$rn"; then
  # shellcheck disable=SC2016 # the ${...} is meant for run_native.sh, not for us
  sed 's/^PROXY_PORT=8081$/PROXY_PORT="${PROXY_PORT:-8081}"/' "$rn" > "$rn.new"
  mv "$rn.new" "$rn"
  chmod +x "$rn"
else
  fail "run_native.sh at this selfhost-ref sets PROXY_PORT in a way this action does not know; update start_native.sh."
fi

# Caddy's http site is `http://:8080`, every interface. Keep it on loopback and
# on our port; run_native.sh's own rewrite of that line then no longer applies.
cf="$cdn/Caddyfile"
grep -qxF 'http://:8080 {' "$cf" || fail "the Caddyfile at this selfhost-ref has no 'http://:8080 {' line to bind to 127.0.0.1."
sed "s|^http://:8080 {\$|http://127.0.0.1:$http_port {|" "$cf" > "$cf.new"
mv "$cf.new" "$cf"
# The admin endpoint (localhost:2019) would collide with any other Caddy here.
grep -qE '^[[:space:]]+admin off$' "$cf" || fail "the Caddyfile does not turn Caddy's admin endpoint off; it would claim port 2019 on this machine."

# xcaddy puts a buildenv_* directory in its working directory.
mkdir -p "$state/caddy-build"
(cd "$state/caddy-build" \
  && HTTP_PORT="$http_port" HTTPS_PORT=0 PROXY_PORT="$proxy_port" \
     "$rn" start --state "$state")

base="http://127.0.0.1:$http_port"
curl -fsS --retry 30 --retry-all-errors --retry-delay 2 \
  "$base/flutter_infra_release/flutter/$ENGINE/engine_stamp.json"
echo
echo "FLUTTER_STORAGE_BASE_URL=$base" >> "$GITHUB_ENV"
