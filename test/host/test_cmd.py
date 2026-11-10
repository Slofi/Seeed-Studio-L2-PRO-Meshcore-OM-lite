#!/usr/bin/env python3
"""Host tests for firmware/tools/cmd.py: argv quoting, the OMCMD1 frame and the answer reader.

The pure functions are what decide what goes on the wire and when the device has finished, so they
are tested without a serial port. run() is exercised with a fake device that returns log lines, data
lines and the final answer.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import cmd  # noqa: E402


class QuoteTest(unittest.TestCase):
    def test_plain_words_are_not_quoted(self):
        self.assertEqual(cmd.quote_arg("status"), "status")
        self.assertEqual(cmd.quote_arg("a1b2c3d4"), "a1b2c3d4")

    def test_space_is_quoted(self):
        self.assertEqual(cmd.quote_arg("Don't Panic"), '"Don\'t Panic"')
        self.assertEqual(cmd.quote_arg("ERA-3 (SI)"), '"ERA-3 (SI)"')

    def test_quote_and_backslash_are_escaped(self):
        self.assertEqual(cmd.quote_arg('say "hi"'), '"say \\"hi\\""')
        self.assertEqual(cmd.quote_arg("a\\b"), '"a\\\\b"')

    def test_empty_word_is_quoted(self):
        self.assertEqual(cmd.quote_arg(""), '""')

    def test_full_command(self):
        self.assertEqual(cmd.command_text(["send", "Don't Panic", "hello world"]),
                         'send "Don\'t Panic" "hello world"')
        self.assertEqual(cmd.command_text(["status"]), "status")


class FrameTest(unittest.TestCase):
    def test_frame_is_magic_length_and_utf8(self):
        self.assertEqual(cmd.frame_for("status"), b"OMCMD1 6\nstatus")
        # The length is UTF-8 BYTES, not characters.
        frame = cmd.frame_for("send x č")
        payload = "send x č".encode("utf-8")
        self.assertTrue(frame.startswith(b"OMCMD1 %d\n" % len(payload)))
        self.assertEqual(frame[-len(payload):], payload)

    def test_too_long_is_refused(self):
        with self.assertRaises(ValueError):
            cmd.frame_for("x" * (cmd.MAX_COMMAND + 1))

    def test_answer_detection(self):
        self.assertTrue(cmd.is_answer(b"[cmd] ok status mesh=up\r\n"))
        self.assertTrue(cmd.is_answer(b"[cmd] error unknown\n"))
        self.assertFalse(cmd.is_answer(b"[node] name=\"X\" type=client\n"))
        self.assertFalse(cmd.is_answer(b"[cmd] something else\n"))
        self.assertTrue(cmd.is_ok(b"[cmd] ok nodes 3\n"))
        self.assertFalse(cmd.is_ok(b"[cmd] error args\n"))


class FakeDevice:
    """Returns one queued line per readline() call; b"" means the port timed out this round."""

    def __init__(self, lines):
        self.lines = list(lines)
        self.writes = []

    def reset_input_buffer(self):
        pass

    def write(self, data):
        self.writes.append(data)

    def flush(self):
        pass

    def readline(self):
        return self.lines.pop(0) if self.lines else b""


class RunTest(unittest.TestCase):
    def test_prints_data_lines_until_ok(self):
        dev = FakeDevice([b"[cmd] nodes\n", b"[node] name=\"X\" type=client key=01020304\n",
                          b"[cmd] ok nodes 1\n"])
        out = []
        rc = cmd.run(dev, ["nodes"], clock=lambda: 0.0, log=out.append)
        self.assertEqual(rc, 0)
        self.assertEqual(out, ['[cmd] nodes', '[node] name="X" type=client key=01020304',
                               '[cmd] ok nodes 1'])
        # One NUL pad write, then exactly one frame; no retry.
        self.assertEqual(len(dev.writes), 2)
        self.assertEqual(dev.writes[0], b"\x00\x00")
        self.assertEqual(dev.writes[1], b"OMCMD1 5\nnodes")

    def test_error_answer_is_a_failure(self):
        dev = FakeDevice([b"[cmd] error unknown\n"])
        rc = cmd.run(dev, ["secret"], clock=lambda: 0.0, log=lambda _: None)
        self.assertEqual(rc, 1)

    def test_timeout_is_a_failure(self):
        now = [0.0]

        def clock():
            now[0] += cmd.ANSWER_TIMEOUT + 1.0
            return now[0]

        dev = FakeDevice([])
        rc = cmd.run(dev, ["status"], clock=clock, log=lambda _: None)
        self.assertEqual(rc, 1)


if __name__ == "__main__":
    unittest.main()
