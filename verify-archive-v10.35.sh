#!/bin/bash
# Verify the .lha that MakeAmiBinkD produced on the Amiga, BEFORE anything
# is published. Lhasa can list and extract, which is all this needs.
set -eu
A=$HOME/Amiberry/HardDrives/DH1/UDBase0/NETWORKING/AmiBinkD10_35.lha
die(){ printf 'FAIL: %s\n' "$*" >&2; exit 1; }

[ -f "$A" ] || die "archive not found: $A  (run 'Execute DH4:MakeAmiBinkD 10_35' on the Amiga)"
echo "== archive: $A ($(stat -c%s "$A") bytes) =="
lha l "$A"
echo

echo "== structure =="
list=$(lha l "$A" | awk '/^\[/{print $NF}')
n=$(printf '%s\n' "$list" | grep -c .)
[ "$n" -eq 8 ] || die "expected 8 entries, got $n"
# The 2026-08-21 incident: a FLAT archive, every file at the root.
printf '%s\n' "$list" | grep -q '^AmiBinkD/AmiBinkD$' || die "FLAT ARCHIVE -- no AmiBinkD/ drawer (see MakeAmiBinkD notes)"
for e in AmiBinkD/AmiBinkD AmiBinkD/AmiBinkD-example.cfg AmiBinkD/AmiBinkD.info \
         AmiBinkD/AmiBinkD.scr AmiBinkD/manual.txt AmiBinkD/readme.txt \
         AmiBinkD/COPYING File_ID.DIZ; do
  printf '%s\n' "$list" | grep -qxF "$e" || die "missing entry: $e"
done
lha l "$A" | grep -q '^\[Amiga\]' || die "archive lacks the [Amiga] OS byte -- was it built with Lhasa instead of DH0:C/lha?"
echo "   8 entries, AmiBinkD/ drawer present, [Amiga] OS byte present"

echo "== extracted binary is really v10.35 =="
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
( cd "$T" && lha xq "$A" >/dev/null 2>&1 )
B="$T/AmiBinkD/AmiBinkD"
[ -f "$B" ] || die "could not extract the binary"
strings -a "$B" | grep -qF 'BEGIN, AmiBinkD v10.35' || die "extracted binary is not v10.35"
strings -a "$B" | grep -qF 'VER AmiBinkD/10.35'     || die "extracted binary has the wrong VER string"
strings -a "$B" | grep -q  '^locale.library'        || die "extracted binary lacks the TZ code"
cmp -s "$B" "$HOME/AmiBinkD/amibinkd" \
  && echo "   byte-identical to the build that soaked" \
  || die "extracted binary DIFFERS from ~/AmiBinkD/amibinkd"
grep -q '^Version:      10.35' "$T/AmiBinkD/readme.txt" || die "archived readme.txt not stamped 10.35"
echo "   readme.txt stamped 10.35"

# GPL-2.0 compliance: shipping the binary without the licence text is the
# one packaging mistake that is a licence violation rather than an
# inconvenience. Check the text is present, unmodified, and pointed at.
echo "== GPL-2.0 compliance =="
cmp -s "$T/AmiBinkD/COPYING" "$HOME/AmiBinkD/LICENSE" \
  || die "archived COPYING is not the repo's verbatim GPL-2.0 text"
grep -q 'GNU GENERAL PUBLIC LICENSE' "$T/AmiBinkD/COPYING" || die "COPYING is not the GPL"
grep -q 'Version 2, June 1991'       "$T/AmiBinkD/COPYING" || die "COPYING is not GPL version 2"
echo "   COPYING present, verbatim GPL-2.0"
grep -q '^LICENCE$'          "$T/AmiBinkD/readme.txt" || die "readme.txt has no LICENCE section"
grep -q 'SpitfireTN/AmiBinkD' "$T/AmiBinkD/readme.txt" || die "readme.txt does not say where the source is"
grep -q '^21. LICENCE$'      "$T/AmiBinkD/manual.txt" || die "manual.txt has no section 21"
echo "   readme.txt and manual.txt state the licence and the source location"
echo
echo "ARCHIVE VERIFIED. Safe to publish."
