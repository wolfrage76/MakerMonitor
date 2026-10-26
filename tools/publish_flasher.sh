#!/usr/bin/env bash
# Publishes flasher/dist/ (from tools/build_flasher.sh) to the gh-pages branch,
# which GitHub Pages serves at https://wolfrage76.github.io/MakerMonitor/.
# The branch keeps only the latest flasher; each publish is one commit.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$REPO/flasher/dist"
[ -f "$DIST/boards.json" ] || { echo "Run tools/build_flasher.sh first" >&2; exit 1; }
URL="$(git -C "$REPO" remote get-url origin)"
VERSION="$(sed -n 's/.*"version": "\(.*\)".*/\1/p' "$DIST/boards.json" | head -1)"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
if git ls-remote --exit-code --heads "$URL" gh-pages >/dev/null; then
  git clone -q --depth 1 --branch gh-pages "$URL" "$WORK/site"
else
  git init -q -b gh-pages "$WORK/site"
  git -C "$WORK/site" remote add origin "$URL"
fi
cd "$WORK/site"
git rm -rqf --ignore-unmatch .
cp -r "$DIST"/. .
touch .nojekyll
git add -Af .
if git diff --cached --quiet; then
  echo "gh-pages already has this build"
  exit 0
fi
git commit -qm "Flasher: firmware v$VERSION"
git push -q origin gh-pages
echo "Published v$VERSION to gh-pages"
