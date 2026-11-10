#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The vendored MeshCore folders must match lib/MESHCORE_VERSION's sha256 manifest.

A hand edit inside lib/meshcore, lib/meshcore_companion or reference/meshcore-companion-<tag>
changes a hash and fails here; an added or deleted file changes the file set and also fails.
tools/update_meshcore.py is the only writer of the manifest.
"""

import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
MANIFEST = ROOT / "lib" / "MESHCORE_VERSION"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 256), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    if not MANIFEST.exists():
        print("manifest: lib/MESHCORE_VERSION is missing")
        return 1

    tag = commit = None
    sections = {}
    current = None
    for line in MANIFEST.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        if line.startswith("tag "):
            tag = line[4:].strip()
        elif line.startswith("commit "):
            commit = line[7:].strip()
        elif line.startswith("section "):
            current = line[8:].strip()
            sections.setdefault(current, {})
        elif current is not None:
            parts = line.split("  ", 1)
            if len(parts) != 2 or len(parts[0]) != 64:
                print(f"manifest: bad line {line!r}")
                return 1
            sections[current][parts[1]] = parts[0]

    if not tag or not commit:
        print("manifest: missing tag or commit")
        return 1
    if set(sections) != {
        "lib/meshcore",
        "lib/meshcore_companion",
        f"reference/meshcore-companion-{tag[len('companion-'):] if tag.startswith('companion-') else tag}",
    }:
        print(f"manifest: unexpected sections {sorted(sections)}")
        return 1

    failures = 0
    total = 0
    for section, expected in sections.items():
        base = ROOT / section
        if not base.exists():
            print(f"manifest: section {section} is missing on disk")
            failures += 1
            continue
        actual = {
            p.relative_to(ROOT).as_posix(): p
            for p in base.rglob("*")
            if p.is_file()
        }
        for rel, want in sorted(expected.items()):
            total += 1
            p = ROOT / rel
            if not p.exists():
                print(f"manifest: MISSING {rel}")
                failures += 1
                continue
            got = sha256_file(p)
            if got != want:
                print(f"manifest: EDITED  {rel}")
                failures += 1
        for rel in sorted(set(actual) - set(expected)):
            print(f"manifest: ADDED   {rel}")
            failures += 1

    if failures:
        print(f"meshcore manifest: {failures} problem(s), {total} files checked")
        return 1
    print(f"meshcore manifest: all {total} files match {tag} ({commit})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
