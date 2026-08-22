# dist/ — the release package

These are the files that go inside `AmiBinkD<version>.lha`, the archive
distributed to other Amiga sysops. They were kept only on the Amiga volume
(`DH4:AmiBinkD/`) until 2026-08-20 and had no history at all — a hand-drawn
Workbench icon and a 2,000-line manual whose only backup was a `.PRE_*` copy
made by hand before each edit. They live here now so they get the same
protection the source does.

| file | what it is |
|---|---|
| `readme.txt` | what a sysop reads first: install, config, changes |
| `manual.txt` | the full reference — every keyword, every switch |
| `AmiBinkD-example.cfg` | starting config, placeholders throughout |
| `AmiBinkD.scr` | sample AmigaDOS startup script |
| `AmiBinkD.info` | Workbench icon, **drawn by hand — binary, do not touch** |
| `File_ID.DIZ` | BBS file-listing description |
| `MakeAmiBinkD` | builds the `.lha`; takes the version as an argument |

## Building a release

The archive **must be built on the Amiga**:

    Execute DH4:MakeAmiBinkD 10_31

The Linux host's `lha` is Lhasa, which only extracts. Every release since
v9 carries the `[Amiga]` OS byte in its archive headers, and building
elsewhere would break that continuity.

## Before tagging a release

The version appears in more places than the binary. Check each:

    readme.txt      version references + the change list
    manual.txt      version references
    File_ID.DIZ     the version on line 1

The binary itself gets its version from `MYVER` in the source, so a stale
number here means the docs disagree with what peers see in the `VER` line.

## Keeping these in step with the code

The docs describe behaviour, so a code change can silently invalidate them.
v10.31 made three statements here wrong at once — the readme claimed the
remote's identity block was at loglevel 3 and that level 2 logged sessions
anonymously, and the manual documented the `-P ALL` per-node line under both
its old name and its old level. Nothing failed; the docs just quietly lied.

**If you change a `Log()` level or a message string, grep this directory for
it before you commit.**
