#!/usr/bin/env bash
# Installs a release's overlay-extra-<engine>.tar into the mirror's overlay.
#
#   install_extra.sh <engine> <extra-sums-pin> <release-dir> <overlay-dir>
#
# <extra-sums-pin> is the sha256 pinned (release_sums) for the release's
# SHA256SUMS.extra. The release's files can be replaced in place, so that file
# is checked against the pin, and the tar and its listing against that file.
# The tar is extracted only if every entry is a plain file or directory under
# a relative path with no "..", and none of its files already exists in the
# overlay: the main tar's files are never overwritten. Any failure exits
# non-zero. Needs GH_TOKEN and GITHUB_REPOSITORY for `gh release download`.
set -euo pipefail

engine="$1" pin="$2" rel="$3" overlay="$4"
tar_name="overlay-extra-$engine.tar"
txt_name="overlay-extra-$engine.txt"
fail() { echo "::error::Release engine-$engine: $*"; exit 1; }

gh release download "engine-$engine" -R "$GITHUB_REPOSITORY" -D "$rel" --clobber \
  -p SHA256SUMS.extra -p "$tar_name" -p "$txt_name" || true
for f in SHA256SUMS.extra "$tar_name" "$txt_name"; do
  [ -f "$rel/$f" ] || fail "this repository pins SHA256SUMS.extra, but the release has no $f."
done

got="$(shasum -a 256 "$rel/SHA256SUMS.extra" | cut -d' ' -f1)"
[ "$got" = "$pin" ] || fail "SHA256SUMS.extra is $got; this repository pins $pin."
for f in "$tar_name" "$txt_name"; do
  grep -qE "^[0-9a-f]{64}  $f\$" "$rel/SHA256SUMS.extra" || fail "SHA256SUMS.extra does not list $f."
done
(cd "$rel" && grep -E "  (${tar_name//./\\.}|${txt_name//./\\.})\$" SHA256SUMS.extra | shasum -a 256 -c -) \
  || fail "$tar_name or $txt_name does not match SHA256SUMS.extra."

# Vet the tar before extracting anything.
listing="$(tar -tf "$rel/$tar_name")" || fail "$tar_name cannot be read."
[ -n "$listing" ] || fail "$tar_name is empty."
# Only a conservative character set, so no newline or control character can
# hide a path from the checks below.
if grep -qvE '^[A-Za-z0-9._/+@=-]+$' <<< "$listing"; then
  fail "$tar_name has an entry whose name has unexpected characters."
fi
if grep -qE '^/|(^|/)\.\.(/|$)' <<< "$listing"; then
  fail "$tar_name has an absolute path or a '..' component."
fi
if [ -n "$(sort <<< "$listing" | uniq -d)" ]; then
  fail "$tar_name lists the same path twice."
fi
# Links could redirect a later entry out of the overlay: only files and dirs.
if tar -tvf "$rel/$tar_name" | grep -qvE '^[-d]'; then
  fail "$tar_name has an entry that is not a regular file or directory."
fi
# Listing must be exactly what overlay-extra-<engine>.txt says.
if ! diff <(sort <<< "$listing") <(sort "$rel/$txt_name") > /dev/null; then
  fail "$tar_name does not match its listing $txt_name."
fi
while IFS= read -r p; do
  case "$p" in */) continue ;; esac
  if [ -e "$overlay/$p" ] || [ -L "$overlay/$p" ]; then
    fail "$p is also in the main overlay; the extra tar must not overwrite it."
  fi
done <<< "$listing"

tar -xf "$rel/$tar_name" -C "$overlay"
echo "Installed $(grep -vc '/$' <<< "$listing") extra overlay files for engine $engine."
