#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-401, the source half: this firmware writes ONE register of the USB-C port controller, and it is
# POWER (0x0B). The arithmetic is test_est401.cpp; this file reads the tree.
#
# Why a scan and not a comment: a write to SWITCHES0 (0x02) or to a CC/termination/PD register can
# drop the board's USB sink role and stop it charging, and the one thing a reviewer must be able to
# check without a device is that no such write exists. So:
#   1. every I2C transmission to 0x22 is classified (one byte = a register pointer for a read, two
#      bytes = a register + a data byte, i.e. a real write);
#   2. exactly ONE two-byte transmission exists in src/, it is `usbPdWritePower` in board.cpp, its
#      register byte is `diag::kRegPower`, and its data byte is that function's parameter;
#   3. it is file-static, and `usbPdPowerBegin()` is called once, from `boardBegin()`;
#   4. nothing else in src/ builds a transmission to 0x22 (a literal 0x22 included), and the
#      one-minute reader really is wired to STATUS0 + the auxiliary ADC channels.
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
FAIL = 0


def fail(msg):
    global FAIL
    print("FAIL", msg)
    FAIL += 1


def strip_comments(text):
    """Remove // and /* */ comments (keeps the code, drops the prose: a comment about SWITCHES0 is
    not a write to it, and a commented-out write must not count as code either)."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def call_arg(text, open_paren_index):
    """The argument text of a call whose '(' is at open_paren_index, and the index after its ')'."""
    depth = 0
    i = open_paren_index
    while i < len(text):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[open_paren_index + 1:i], i + 1
        i += 1
    return "", len(text)


def transmissions(text):
    """Every Wire.beginTransmission(...) block: (index, address expression, [write arguments])."""
    found = []
    for m in re.finditer(r"Wire\.beginTransmission\s*\(", text):
        address, after = call_arg(text, m.end() - 1)
        end = text.find("Wire.endTransmission", after)
        body = text[after:] if end < 0 else text[after:end]
        writes = []
        for w in re.finditer(r"Wire\.write\s*\(", body):
            arg, _ = call_arg(body, w.end() - 1)
            writes.append(arg.strip())
        found.append((m.start(), address.strip(), writes))
    return found


def main():
    files = {}
    for path in sorted(SRC.rglob("*.cpp")) + sorted(SRC.rglob("*.h")):
        files[path.relative_to(ROOT).as_posix()] = strip_comments(
            path.read_text(encoding="utf-8"))

    # 1. classify every transmission to 0x22 anywhere in src/.
    writes_to_022 = []
    reads_to_022 = 0
    for rel, text in files.items():
        for at, address, writes in transmissions(text):
            is_022 = address in ("diag::kUsbPdAddr", "kUsbPdAddr", "0x22", "0x22U") or \
                address.endswith("kUsbPdAddr")
            if not is_022:
                if "0x22" in address:
                    fail(f"{rel}: a transmission to {address!r} does not use diag::kUsbPdAddr")
                continue
            if len(writes) == 1:
                reads_to_022 += 1
            elif len(writes) == 2:
                writes_to_022.append((rel, writes[0], writes[1]))
            else:
                fail(f"{rel}: a transmission to 0x22 with {len(writes)} Wire.write calls")

    # 2. exactly one real write, and it is POWER.
    if len(writes_to_022) != 1:
        fail(f"expected exactly 1 data write to 0x22 in src/, found {len(writes_to_022)}: "
             f"{writes_to_022}")
    else:
        rel, reg, data = writes_to_022[0]
        if rel != "src/board.cpp":
            fail(f"the one write to 0x22 lives in {rel}, expected src/board.cpp")
        if reg != "diag::kRegPower":
            fail(f"the one write to 0x22 writes register {reg!r}, expected diag::kRegPower (0x0B)")
        if data != "value":
            fail(f"the one write to 0x22 sends {data!r}, expected the helper's `value` parameter")

    # The register constants themselves: 0x0B POWER and never 0x02 SWITCHES0.
    diag_h = files["src/diag_logic.h"]
    if "constexpr uint8_t kRegPower = 0x0B;" not in diag_h:
        fail("diag_logic.h: kRegPower is not 0x0B")
    if "constexpr uint8_t kUsbPdAddr = 0x22;" not in diag_h:
        fail("diag_logic.h: kUsbPdAddr is not 0x22")
    if re.search(r"kRegSwitches|SWITCHES0\s*=", diag_h):
        fail("diag_logic.h: a SWITCHES0 register constant exists (nothing may name it)")

    # 3. the helper is file-static and the boot call exists once.
    board_cpp = files["src/board.cpp"]
    if "static bool usbPdWritePower(uint8_t value) {" not in board_cpp:
        fail("board.cpp: usbPdWritePower() is not static (it must be unreachable from other files)")
    if board_cpp.count("void usbPdPowerBegin() {") != 1:
        fail("board.cpp: usbPdPowerBegin() is not defined exactly once")
    calls = sum(text.count("usbPdPowerBegin();") for rel, text in files.items()
                if rel != "src/board.h")
    if calls != 1:
        fail(f"usbPdPowerBegin() has {calls} call sites in src/, expected exactly 1 (boardBegin)")
    begin_at = board_cpp.find("bool boardBegin() {")
    if begin_at < 0:
        fail("board.cpp: boardBegin() not found")
    else:
        end = board_cpp.find("\n}\n", begin_at)
        body = board_cpp[begin_at:end if end >= 0 else len(board_cpp)]
        if "usbPdPowerBegin();" not in body:
            fail("board.cpp: boardBegin() does not call usbPdPowerBegin()")
        # After the display init re-owns the bus: the write must not run before Wire.begin().
        if body.find("usbPdPowerBegin();") < body.find("s_display.init();"):
            fail("board.cpp: the POWER write runs before the display init re-owned the I2C bus")

    # A read-back is part of the attempt: "accepted" is never claimed from endTransmission() alone.
    attempt = board_cpp.find("static bool usbPdMeasureAttempt(")
    if attempt < 0:
        fail("board.cpp: usbPdMeasureAttempt() not found")
    else:
        end = board_cpp.find("\n}\n", attempt)
        body = board_cpp[attempt:end if end >= 0 else len(board_cpp)]
        if "usbPdWritePower(want)" not in body:
            fail("board.cpp: the attempt does not write through usbPdWritePower()")
        if body.count("usbPdReadRegister(diag::kRegPower") < 2:
            fail("board.cpp: the attempt does not read POWER before AND after the write")
        if "powerBitsPresent" not in body:
            fail("board.cpp: the attempt does not verify the read-back")
        if "powerValueToWrite" not in body:
            fail("board.cpp: the attempt does not preserve the read-back bits")
        if "powerReadSane" not in body:
            fail("board.cpp: the attempt does not refuse an implausible POWER read")
        elif body.find("powerReadSane") > body.find("usbPdWritePower(want)"):
            fail("board.cpp: the POWER sanity check runs after the write, not before it")

    # The boot line the ticket names.
    if '"[pmu] measure block on (POWER=0x%02X)\\n"' not in board_cpp:
        fail("board.cpp: the `[pmu] measure block on (POWER=0x..)` line is missing")

    # 4. the once-a-minute reader is wired: STATUS0 -> vbus, AIN1..3 -> the three columns.
    plog = files["src/power_log.cpp"]
    for needle, what in (
        ("om::usbPdStatus0(&status0)", "STATUS0 read"),
        ("om::diag::status0VbusOk(status0)", "VBUSOK decode"),
        ("om::adcAuxProbe(aux)", "auxiliary ADC read"),
        ("s.vbus = haveStatus0", "vbus column"),
        ("s.ain1Mv = aux.mv[0]", "ain1 column"),
        ("s.ain2Mv = aux.mv[1]", "ain2 column"),
        ("s.ain3Mv = aux.mv[2]", "ain3 column"),
    ):
        if needle not in plog:
            fail(f"power_log.cpp: {what} is missing ({needle})")

    # The reader (`log power tail`) is untouched by design: it prints the file's lines verbatim.
    if "printTail" not in files["src/power_log.cpp"]:
        fail("power_log.cpp: printTail() is gone")

    if FAIL == 0:
        print(f"est401 (source): 1 write to 0x22 = POWER 0x0B, {reads_to_022} read block, "
              "boot call, STATUS0/ADC wiring OK")
        return 0
    print(f"est401 (source): {FAIL} failure(s)")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
