#!/usr/bin/env python3
"""Verify a logtest run. Every line must be exactly 64 characters, well
formed, and each (id, seq) pair must appear exactly once."""
import sys, re, collections

path = sys.argv[1] if len(sys.argv) > 1 else \
    '/home/spitfiretn/Amiberry/HardDrives/DH4/logtest-out.txt'
WRITERS, PER = 5, 200
EXPECT = WRITERS * PER
pat = re.compile(r'^LINE id=(\d{2}) seq=(\d{4}) ([A-Z])\3{43}$')

raw = open(path, 'rb').read().decode('latin-1')
lines = raw.split('\n')
if lines and lines[-1] == '':
    lines.pop()

good, bad, seen = 0, [], collections.Counter()
for n, l in enumerate(lines, 1):
    m = pat.match(l)
    if m and len(l) == 64 and m.group(3) == chr(ord('A') + int(m.group(1))):
        good += 1
        seen[(int(m.group(1)), int(m.group(2)))] += 1
    else:
        bad.append((n, len(l), l[:56]))

missing = [k for i in range(WRITERS) for s in range(PER)
           if (k := (i, s)) not in seen]
dupes = [k for k, v in seen.items() if v > 1]

print(f"  file        : {path}")
print(f"  lines       : {len(lines)}  (expected {EXPECT})")
print(f"  well formed : {good}")
print(f"  MALFORMED   : {len(bad)}")
print(f"  MISSING     : {len(missing)}")
print(f"  DUPLICATED  : {len(dupes)}")
for n, ln, txt in bad[:8]:
    print(f"      line {n:5d} len={ln:3d}  {txt!r}")
if missing[:6]:
    print(f"      missing e.g. {missing[:6]}")
ok = (len(lines) == EXPECT and not bad and not missing and not dupes)
print(f"\n  RESULT: {'PASS -- append is honoured' if ok else 'FAIL -- writes are landing wrong'}")
sys.exit(0 if ok else 1)
