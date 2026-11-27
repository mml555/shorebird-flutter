#!/usr/bin/env bash
# Downloads a release's overlay tar and checks it against SHA256SUMS.
#
#   fetch_overlay.sh <engine> <release-dir>
#
# <release-dir> already holds SHA256SUMS, checked against this repository's pin
# (release_sums). On success it also holds overlay-<engine>.tar,
# experimental_hashes-<engine>.map and engine_stamp-<engine>.json, all checked
# against that SHA256SUMS. Needs GH_TOKEN and GITHUB_REPOSITORY.
#
# A release carries the overlay tar in one of two shapes:
#   * one asset, overlay-<engine>.tar (older engines);
#   * parts, overlay-<engine>.tar.part-aa, -ab, ... (GitHub refuses assets of
#     2 GiB or more), with PARTS.sha256 listing the sha256 of each part.
#     `cat` of the parts in name order is the tar. SHA256SUMS lists the joined
#     tar, so the pinned file vouches for the parts' bytes through it.
# Both shapes are checked the same way in the end: SHA256SUMS must name the
# tar and match the file that is extracted. For parts, each part is also
# checked against PARTS.sha256 first, so a damaged download is named as such
# before 3 GB are joined.
#
# Do not write `cmd | grep -q`: see install_extra.sh.
set -euo pipefail
export LC_ALL=C

engine="$1" rel="$2"
tar_name="overlay-$engine.tar"
fail() { echo "::error::Release engine-$engine: $*"; exit 1; }

assets="$(gh release view "engine-$engine" -R "$GITHUB_REPOSITORY" --json assets --jq '.assets[].name')" \
  || fail "cannot list its assets."
has() { grep -qxF -- "$1" <<< "$assets"; }
part_re="^${tar_name//./\\.}\\.part-[a-z]{2}\$"
parts="$(grep -E "$part_re" <<< "$assets" | sort || true)"

rm -f "$rel/$tar_name" "$rel/PARTS.sha256" "$rel"/"$tar_name".part-*

if has "$tar_name"; then
  gh release download "engine-$engine" -R "$GITHUB_REPOSITORY" -D "$rel" --clobber -p "$tar_name"
elif [ -n "$parts" ]; then
  has PARTS.sha256 || fail "has overlay parts but no PARTS.sha256."
  gh release download "engine-$engine" -R "$GITHUB_REPOSITORY" -D "$rel" --clobber \
    -p PARTS.sha256 -p "$tar_name.part-*"

  # The parts must be aa, ab, ac, ... with no gap, and exactly the ones
  # PARTS.sha256 lists, each with a matching digest.
  letters=abcdefghijklmnopqrstuvwxyz
  i=0 expected=""
  while IFS= read -r p; do
    want="$tar_name.part-${letters:$((i / 26)):1}${letters:$((i % 26)):1}"
    [ "$p" = "$want" ] || fail "parts are not a complete aa, ab, ... run: found $p where $want was expected."
    expected+="$p"$'\n'
    i=$((i + 1))
  done <<< "$parts"
  sums="$(< "$rel/PARTS.sha256")"
  sums="${sums//$'\r'/}"
  listed="$(awk 'NF == 2 { print $2 }' <<< "$sums" | sort)"
  [ "$listed" = "${expected%$'\n'}" ] \
    || fail "PARTS.sha256 does not list exactly the parts the release has."
  (cd "$rel" && shasum -a 256 -c - <<< "$sums") \
    || fail "a part does not match PARTS.sha256 (a damaged download, or a part replaced in place)."

  # Append and delete part by part, so the disk holds the tar and one part
  # rather than the parts and the whole tar.
  : > "$rel/$tar_name"
  while IFS= read -r p; do
    cat "$rel/$p" >> "$rel/$tar_name"
    rm -f "$rel/$p"
  done <<< "$parts"
else
  fail "has neither $tar_name nor $tar_name.part-* assets."
fi

gh release download "engine-$engine" -R "$GITHUB_REPOSITORY" -D "$rel" --clobber \
  -p "experimental_hashes-$engine.map" -p "engine_stamp-$engine.json"
# The tar's digest here is the joined file's, whichever shape it was released in.
(cd "$rel" && shasum -a 256 -c SHA256SUMS) \
  || fail "the downloaded files (or the joined parts) do not match SHA256SUMS."
