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
#
# Do not write `cmd | grep -q`: grep -q exits at its first match, cmd then dies
# of SIGPIPE, and under pipefail the whole check reads as failed or, in a
# negated test, as passed. Capture output in a variable and grep that.
set -euo pipefail
export LC_ALL=C

engine="$1" pin="$2" rel="$3" overlay="$4"
tar_name="overlay-extra-$engine.tar"
txt_name="overlay-extra-$engine.txt"
fail() { echo "::error::Release engine-$engine: $*"; exit 1; }

# No stale file from an earlier download may stand in for a missing asset.
rm -f "$rel/SHA256SUMS.extra" "$rel/$tar_name" "$rel/$txt_name"
gh release download "engine-$engine" -R "$GITHUB_REPOSITORY" -D "$rel" --clobber \
  -p SHA256SUMS.extra -p "$tar_name" -p "$txt_name" || true
for f in SHA256SUMS.extra "$tar_name" "$txt_name"; do
  [ -f "$rel/$f" ] || fail "this repository pins SHA256SUMS.extra, but the release has no $f."
done

# Everything below reads private copies, so the bytes that are hashed are the
# bytes that are vetted and extracted.
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp "$rel/SHA256SUMS.extra" "$rel/$tar_name" "$rel/$txt_name" "$work/"

got="$(shasum -a 256 "$work/SHA256SUMS.extra" | cut -d' ' -f1)"
[ "$got" = "$pin" ] || fail "SHA256SUMS.extra is $got; this repository pins $pin."

# It must be exactly one line for the tar and one for the txt, nothing else.
# A third asset (such as a .sig) in SHA256SUMS.extra must update this rule.
# CRLF line endings are accepted; the pin above covers the raw bytes.
sums="$(< "$work/SHA256SUMS.extra")"
sums="${sums//$'\r'/}"
tar_re="${tar_name//./\\.}" txt_re="${txt_name//./\\.}"
tar_line="$(grep -E "^[0-9a-f]{64}  $tar_re\$" <<< "$sums" || true)"
txt_line="$(grep -E "^[0-9a-f]{64}  $txt_re\$" <<< "$sums" || true)"
[ -n "$tar_line" ] && [ -n "$txt_line" ] \
  || fail "SHA256SUMS.extra does not list $tar_name and $txt_name."
if [ "$(grep -c '' <<< "$sums")" != 2 ] \
    || [ "$(grep -c '' <<< "$tar_line")" != 1 ] || [ "$(grep -c '' <<< "$txt_line")" != 1 ] \
    || { [ "$sums" != "$tar_line"$'\n'"$txt_line" ] && [ "$sums" != "$txt_line"$'\n'"$tar_line" ]; }; then
  fail "SHA256SUMS.extra must list exactly $tar_name and $txt_name and nothing else."
fi
(cd "$work" && shasum -a 256 -c - <<< "$sums") \
  || fail "$tar_name or $txt_name does not match SHA256SUMS.extra."

# Vet the tar before extracting anything.
vtar="$work/$tar_name"
listing="$(tar -tf "$vtar")" || fail "$tar_name cannot be read."
verbose="$(tar -tvf "$vtar")" || fail "$tar_name cannot be read."
[ -n "$listing" ] || fail "$tar_name is empty."
# Only a conservative character set, so no newline or control character can
# hide a path from the checks below.
if grep -qvE '^[A-Za-z0-9._/+@=-]+$' <<< "$listing"; then
  fail "$tar_name has an entry whose name has unexpected characters."
fi
# One leading ./ is how the release writes its paths; compare without it.
norm="$(awk '{ sub(/^\.\//, "") } $0 != ""' <<< "$listing")"
if grep -qE '^/|//|(^|/)\.\.?(/|$)' <<< "$norm"; then
  fail "$tar_name has an absolute path or a '.', '..' or empty component."
fi
dups="$(sort <<< "$norm" | uniq -d)"
[ -z "$dups" ] || fail "$tar_name lists the same path twice."
# Links could redirect a later entry out of the overlay: only files and dirs,
# and owner-readable ones (dirs also owner-searchable), so a mode-000 entry
# cannot break later steps. Read-only files and r-x dirs are fine: tar sets a
# directory's mode after its children.
if grep -qvE '^(d[r]([w-])x|-r)' <<< "$verbose"; then
  fail "$tar_name has an entry that is not a regular file or directory, or whose owner cannot read it."
fi
if ! diff <(sort <<< "$listing") <(sort "$work/$txt_name") > /dev/null; then
  fail "$tar_name does not match its listing $txt_name."
fi
while IFS= read -r p; do
  q="${p%/}"
  if [ -e "$overlay/$q" ] || [ -L "$overlay/$q" ]; then
    # A directory the main overlay already has is fine; a file is not.
    if [ "$q" != "$p" ] && [ -d "$overlay/$q" ] && [ ! -L "$overlay/$q" ]; then
      continue
    fi
    fail "$q is also in the main overlay; the extra tar must not overwrite it."
  fi
done <<< "$norm"

tar -xf "$vtar" -C "$overlay" --no-same-permissions
echo "Installed $(grep -vc '/$' <<< "$listing") extra overlay files for engine $engine."
