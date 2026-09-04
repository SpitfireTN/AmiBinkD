#!/bin/bash
# Create the GitHub release for v10.35 and attach the archive.
# Run ONLY after:  Execute DH4:MakeAmiBinkD 10_35   (on the Amiga)
#              and ~/AmiBinkD/verify-archive-v10.35.sh
set -eu
A=$HOME/Amiberry/HardDrives/DH1/UDBase0/NETWORKING/AmiBinkD10_35.lha
NOTES=$HOME/amibinkd-post-v1035.txt
TAG=v10.35
die(){ printf 'FAIL: %s\n' "$*" >&2; exit 1; }

[ -f "$A" ]     || die "archive missing: $A"
[ -f "$NOTES" ] || die "release notes missing: $NOTES"

echo "== re-verifying the archive before publishing =="
"$HOME/AmiBinkD/verify-archive-v10.35.sh" >/dev/null || die "archive verification failed"
echo "   ok"

cd "$HOME/AmiBinkD"
[ -z "$(git status --porcelain | grep -v '\.o$' || true)" ] || die "working tree dirty -- commit first"
git fetch origin --quiet
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || die "HEAD is not pushed to origin/main"

if gh release view "$TAG" >/dev/null 2>&1; then
  echo "== release $TAG exists; uploading asset =="
  gh release upload "$TAG" "$A" --clobber
else
  echo "== creating release $TAG =="
  gh release create "$TAG" "$A" \
    --title "AmiBinkD v10.35 — polls that do not stall" \
    --notes-file "$NOTES"
fi
echo
gh release view "$TAG" | head -12
echo
echo "Published. Remaining, by hand:"
echo "  - Aminet upload (dist/readme.txt already carries the 10.35 header)"
echo "  - post ~/amibinkd-post-v1035.txt to the networks (also DH4:1034.txt)"
echo "  - remove AmiBinkD10_34.lha from UDbase0 once 10_35 is in place"
