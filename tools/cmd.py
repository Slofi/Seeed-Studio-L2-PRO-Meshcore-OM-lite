#!/usr/bin/env python3
"""Run one test command on the L2 Pro over USB and print the device's answer.

Usage: tools/cmd.py <command...>
       OM_PORT=/dev/ttyACM1 tools/cmd.py status

The command words are joined into one line; a word containing a space (or a quote) is quoted, so
    tools/cmd.py send "Don't Panic" "hello there"
sends   send "Don't Panic" "hello there"
and the device executes `send` on the channel named Don't Panic with the text `hello there`.

Protocol (device side: src/ota.cpp + src/usb_frames.h, the same reader as OMOTA1/OMSHOT):
    host -> device  "OMCMD1 <len>\\n" then <len> UTF-8 bytes of command text
    device -> host  data lines ([node] ..., [msg] ...) when a command has any, then exactly one
                    "[cmd] ok ..." or "[cmd] error <reason>" line.

The port is opened exactly like tools/shot.py: DTR asserted before anything is written (this CDC
delivers host -> device bytes only with DTR set), RTS never written, DTR never dropped, and no reset
requested. A command is sent ONCE: unlike a screenshot, a retry could send the same message twice.
"""
import sys
import time

MAGIC = b"OMCMD1"
MAX_COMMAND = 300          # src/usb_frames.h kMaxCommandBytes
ANSWER_TIMEOUT = 10.0      # the ticket's 10 s


def quote_arg(arg):
    """One argv word as the device's tokenizer reads it (double quotes, \\" and \\\\)."""
    if arg == "" or any(ch.isspace() or ch in '"\\' for ch in arg):
        return '"' + arg.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return arg


def command_text(argv):
    """The UTF-8 command line for the words after the script name."""
    return " ".join(quote_arg(a) for a in argv)


def frame_for(text):
    """The OMCMD1 frame for one command line. Raises ValueError when it is too long for the device."""
    payload = text.encode("utf-8")
    if len(payload) > MAX_COMMAND:
        raise ValueError(f"command is {len(payload)} bytes, the device accepts {MAX_COMMAND}")
    return MAGIC + b" %d\n" % len(payload) + payload


def is_answer(line):
    """True for the one terminating line the device always sends."""
    text = line.decode("utf-8", "replace").strip()
    return text.startswith("[cmd] ok") or text.startswith("[cmd] error")


def is_ok(line):
    return line.decode("utf-8", "replace").strip().startswith("[cmd] ok")


def answer_timeout(argv):
    """How long to wait for the answer line.

    `gps raw N` answers only when its N-second window closes: the start line is not an answer, so
    the default 10 s would expire long before the diagnostic has printed anything useful."""
    try:
        if len(argv) == 3 and argv[0] == "gps" and argv[1] == "raw":
            seconds = int(argv[2])
            if seconds > 0:
                return max(ANSWER_TIMEOUT, seconds + 5.0)
    except (TypeError, ValueError):
        pass
    return ANSWER_TIMEOUT


def run(dev, argv, clock=time.monotonic, log=print):
    """Send one frame and print lines until the answer line. Returns the process exit code."""
    frame = frame_for(command_text(argv))
    dev.reset_input_buffer()
    # Same resync as shot.py: NUL pad bytes pass through an empty FIFO and cycle any 1-5 byte magic
    # residue, so the matcher starts clean. Idempotent, unlike retrying the command itself.
    dev.write(b"\x00\x00")
    dev.flush()
    time.sleep(0.15)
    dev.write(frame)
    dev.flush()

    shown = None
    timeout = answer_timeout(argv)
    end = clock() + timeout
    while clock() < end:
        line = dev.readline()
        if not line:
            continue                    # read timeout: the deadline check ends the loop
        text = line.decode("utf-8", "replace").rstrip("\r\n")
        if text:
            log(text)
        if is_answer(line):
            shown = line
            break
    if shown is None:
        print(f"device never answered the command within {timeout:g} s", file=sys.stderr)
        return 1
    return 0 if is_ok(shown) else 1


def main(argv):
    if len(argv) < 2:
        print("usage: tools/cmd.py <command...>", file=sys.stderr)
        return 2
    command = argv[1:]
    try:
        frame_for(command_text(command))
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    import os
    import serial  # imported here so the pure functions above stay testable without a port

    port = os.environ.get("OM_PORT", "/dev/ttyACM0")
    dev = serial.Serial(port, 115200, timeout=0.5)
    dev.dtr = True                       # host -> device bytes need DTR; see the module note
    time.sleep(1.5)                      # the app waits for the host to open the port
    try:
        return run(dev, command)
    finally:
        dev.close()                      # closed as opened: nothing is toggled, no reset requested


if __name__ == "__main__":
    sys.exit(main(sys.argv))
