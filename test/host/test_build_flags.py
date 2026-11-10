#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""One MAX_CONTACTS for every translation unit (EST-341).

BaseChatMesh sizes its contact table from MAX_CONTACTS. MyMesh.h defaults it to 100, but the
meshcore library TU (lib/meshcore/helpers/BaseChatMesh.cpp) is compiled on its own and never sees
MyMesh.h, so it fell back to BaseChatMesh.h's 32. The same class then had two layouts: the global
MyMesh was 15,312 B while MyMesh.cpp wrote it as 28,096 B; `_store` was read from an uninitialised
offset, the first savePrefs() called through a NULL DataStore and the device panicked at
`[mc] 3 store + mesh begin`.

The fix is the build define below. It must be in `build_flags` (applies to libraries too), not in
`build_src_flags` (project sources only), and exactly one value may be defined.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
INI = ROOT / "platformio.ini"
MYMESH = ROOT / "lib" / "meshcore_companion" / "MyMesh.h"
BASECHAT = ROOT / "lib" / "meshcore" / "helpers" / "BaseChatMesh.h"
MESHLINK = ROOT / "src" / "mesh_link.cpp"
MAIN = ROOT / "src" / "main.cpp"

EXPECTED = 100


def ini_block(text: str, key: str) -> str:
    """The continuation lines of `key =` in platformio.ini (up to the next key or section)."""
    m = re.search(
        r"^" + re.escape(key) + r"\s*=\s*$(.*?)(?=^[A-Za-z_][A-Za-z0-9_.]*\s*=|^\[|\Z)",
        text,
        re.M | re.S,
    )
    return m.group(1) if m else ""


def main() -> int:
    failures = 0
    if not INI.exists():
        print("build flags: platformio.ini is missing")
        return 1
    text = INI.read_text()

    # Only the om_lite_l2 environment matters; refuse to test a file that lost it.
    if not re.search(r"^\[env:om_lite_l2\]", text, re.M):
        print("build flags: no [env:om_lite_l2] section")
        return 1

    def flags_of(body: str) -> list[str]:
        """One entry per flag line; PlatformIO accepts `-D X=1` and `-DX=1` alike."""
        out = []
        for raw in body.splitlines():
            line = raw.strip()
            if not line or line.startswith(";") or line.startswith("#"):
                continue
            out.append(line)
        return out

    build_flags = flags_of(ini_block(text, "build_flags"))
    src_flags = flags_of(ini_block(text, "build_src_flags"))
    compact = [f.replace(" ", "") for f in build_flags]
    defines = [f for f in compact if f.startswith("-DMAX_CONTACTS=")]
    if len(defines) == 0:
        print("build flags: -D MAX_CONTACTS=100 is missing (lib/meshcore would compile at 32)")
        failures += 1
    elif len(defines) > 1:
        print(f"build flags: MAX_CONTACTS defined {len(defines)} times: {defines}")
        failures += 1
    else:
        value = defines[0].split("=", 1)[1]
        if not value.isdigit() or int(value) != EXPECTED:
            print(f"build flags: MAX_CONTACTS is {value}, expected {EXPECTED}")
            failures += 1

    if any("MAX_CONTACTS" in f for f in src_flags):
        print("build flags: MAX_CONTACTS in build_src_flags does NOT reach lib/meshcore")
        failures += 1

    # The intent stays documented in both headers; if upstream changes either default the flag
    # above is still what every TU gets, but say it out loud.
    if MYMESH.exists():
        if not re.search(r"#define\s+MAX_CONTACTS\s+100\b", MYMESH.read_text()):
            print("build flags: MyMesh.h no longer defaults MAX_CONTACTS to 100")
            failures += 1
    else:
        print("build flags: lib/meshcore_companion/MyMesh.h is missing")
        failures += 1

    if BASECHAT.exists():
        if not re.search(r"#define\s+MAX_CONTACTS\s+32\b", BASECHAT.read_text()):
            print("build flags: BaseChatMesh.h no longer defaults to 32 (the flag is still required,"
                  " but re-check this test)")
            failures += 1
    else:
        print("build flags: lib/meshcore/helpers/BaseChatMesh.h is missing")
        failures += 1

    # EST-351: the loop task's stack is set through Arduino's own macro at file scope (a -D flag
    # would collide with Arduino.h's function-like definition) and status prints the high-water mark.
    if MAIN.exists():
        main_text = MAIN.read_text()
        if main_text.count("SET_LOOP_TASK_STACK_SIZE(16384)") != 1:
            print("build flags: src/main.cpp must call SET_LOOP_TASK_STACK_SIZE(16384) exactly once")
            failures += 1
        if "loop_stack_free=" not in main_text or "loop_stack_size=" not in main_text:
            print("build flags: status lost loop_stack_free / loop_stack_size")
            failures += 1
        if any("SET_LOOP_TASK_STACK_SIZE" in f for f in build_flags):
            print("build flags: SET_LOOP_TASK_STACK_SIZE must be in source, not a -D flag")
            failures += 1
    else:
        print("build flags: src/main.cpp is missing")
        failures += 1

    # The compile-time guard that names the failure if the define ever disappears.
    if MESHLINK.exists():
        if "MAX_CONTACTS == 100" not in MESHLINK.read_text():
            print("build flags: src/mesh_link.cpp lost its MAX_CONTACTS static_assert")
            failures += 1
    else:
        print("build flags: src/mesh_link.cpp is missing")
        failures += 1

    if failures:
        print(f"build flags: {failures} problem(s)")
        return 1
    print("build flags: MAX_CONTACTS=100 reaches every TU (libraries included)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
