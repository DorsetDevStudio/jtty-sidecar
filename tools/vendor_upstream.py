#!/usr/bin/env python3
"""Re-vendor the JTTY sources from an unpacked WSJT-X (or WS) source tree.

Usage:  python tools/vendor_upstream.py <path-to-wsjtx-source-root> <version-label>

Copies, byte for byte, the files listed in FILES into upstream/wsjtx/ keeping
their upstream relative paths, normalises line endings to LF (the WS mirror
ships CRLF, WSJT-X ships LF; the code is otherwise identical), and rewrites
upstream/UPSTREAM.md with the version label and a SHA-256 per file so a later
drop can be diffed against the one before. Nothing in upstream/ is ever edited
by hand: all glue lives in src/.
"""
import hashlib
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DEST = os.path.join(REPO, "upstream", "wsjtx")

# Everything under lib/jtty (the mode itself, including its design notes and
# the standalone test programs we do not build), plus the handful of shared
# WSJT-X routines the mode calls out to.
DIRS = ["lib/jtty"]
FILES = [
    "COPYING",
    "lib/four2a.f90",
    "lib/db.f90",
    "lib/fftw3mod.f90",
    "lib/77bit/packjt77.f90",
    "lib/77bit/packjt77_grammar.f90",
    "lib/77bit/packjt77_schema.f90",
    "lib/chkcall.f90",
    "lib/fmtmsg.f90",
    "lib/twkfreq.f90",
    "lib/smo121.f90",
    "lib/wavhdr.f90",
    "lib/gfsk_pulse.f90",
    "lib/pctile.f90",
    "lib/shell.f90",
    "lib/ft8/watterson.f90",
]
# Text files whose line endings we normalise. Binary (png) are copied as is.
TEXT_EXT = {".f90", ".F90", ".c", ".h", ".md", ".txt", ".ini", ".mc", ".py", ""}


def copy_one(src_root, rel, out_lines):
    src = os.path.join(src_root, rel)
    dst = os.path.join(DEST, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    data = open(src, "rb").read()
    ext = os.path.splitext(rel)[1]
    if ext in TEXT_EXT:
        data = data.replace(b"\r\n", b"\n")
    open(dst, "wb").write(data)
    out_lines.append("%s  %s" % (hashlib.sha256(data).hexdigest(), rel.replace(os.sep, "/")))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src_root, label = sys.argv[1], sys.argv[2]
    if not os.path.isdir(os.path.join(src_root, "lib", "jtty")):
        sys.exit("no lib/jtty under " + src_root)
    if os.path.isdir(DEST):
        shutil.rmtree(DEST)
    lines = []
    for d in DIRS:
        for dirpath, _, names in os.walk(os.path.join(src_root, d)):
            for n in sorted(names):
                rel = os.path.relpath(os.path.join(dirpath, n), src_root)
                copy_one(src_root, rel, lines)
    for rel in FILES:
        copy_one(src_root, rel, lines)
    lines.sort(key=lambda s: s.split("  ", 1)[1])
    with open(os.path.join(REPO, "upstream", "UPSTREAM.md"), "w", newline="\n") as f:
        f.write("# Vendored upstream sources\n\n")
        f.write("Source: WSJT-X by Joe Taylor K1JT and the WSJT Development Group, GPLv3 (see COPYING).\n")
        f.write("Version label: **%s**\n\n" % label)
        f.write("Files are copied unmodified apart from CRLF -> LF normalisation. Re-run\n")
        f.write("`python tools/vendor_upstream.py <source-root> <label>` to take a new drop;\n")
        f.write("the hashes below let you diff one drop against the next.\n\n```\n")
        f.write("\n".join(lines))
        f.write("\n```\n")
    print("vendored %d files from %s as %s" % (len(lines), src_root, label))


if __name__ == "__main__":
    main()
