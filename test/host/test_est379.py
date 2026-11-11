#!/usr/bin/env python3
"""EST-379: tools/build_id.py, with a fake git.

The device must be able to say which build it runs, so the pre-build script's one piece of logic is
pinned here: the exact `git describe` command, the cwd, the result, and the fallback to "unknown"
with a warning line when git fails or is missing. Run by test/host/run.sh.
"""
import contextlib
import importlib.util
import io
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "build_id.py"

spec = importlib.util.spec_from_file_location("om_build_id", SCRIPT)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

problems = []


class Result:
    def __init__(self, returncode=0, stdout="", stderr=""):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


class FakeGit:
    def __init__(self, result=None, raises=None):
        self.result = result
        self.raises = raises
        self.calls = []

    def __call__(self, argv, **kwargs):
        self.calls.append((list(argv), dict(kwargs)))
        if self.raises is not None:
            raise self.raises
        return self.result


def run_case(name, result=None, raises=None):
    git = FakeGit(result, raises)
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        got = mod.git_build_id(ROOT, run=git)
    return got, git, out.getvalue()


# 1. A clean tree: the short hash, exactly.
got, git, out = run_case("clean", Result(stdout="1ee888c\n"))
if got != "1ee888c":
    problems.append("clean: id is %r, expected '1ee888c'" % (got,))
if out.strip():
    problems.append("clean: unexpected output %r" % (out,))
if len(git.calls) != 1:
    problems.append("clean: git called %d times, expected 1" % len(git.calls))
else:
    argv, kwargs = git.calls[0]
    if argv != ["git", "describe", "--always", "--dirty", "--abbrev=7"]:
        problems.append("clean: command is %r" % (argv,))
    if kwargs.get("cwd") != str(ROOT):
        problems.append("clean: cwd is %r, expected the firmware folder" % (kwargs.get("cwd"),))
    if not kwargs.get("capture_output") or not kwargs.get("text"):
        problems.append("clean: git output is not captured as text")

# 2. A dirty tree: --dirty is what adds the suffix, so the raw answer is kept.
got, _, _ = run_case("dirty", Result(stdout="1ee888c-dirty\n"))
if got != "1ee888c-dirty":
    problems.append("dirty: id is %r, expected '1ee888c-dirty'" % (got,))

# 3. git ran but failed (not a repository): "unknown" and a warning line.
got, _, out = run_case("failure", Result(returncode=128, stderr="fatal: not a git repository\n"))
if got != "unknown":
    problems.append("failure: id is %r, expected 'unknown'" % (got,))
if "WARNING" not in out or "unknown" not in out:
    problems.append("failure: no warning line in %r" % (out,))

# 4. git is not installed: the build still gets an id and a warning.
got, _, out = run_case("missing", raises=FileNotFoundError("git"))
if got != "unknown":
    problems.append("missing: id is %r, expected 'unknown'" % (got,))
if "WARNING" not in out or "unknown" not in out:
    problems.append("missing: no warning line in %r" % (out,))

# 5. git answers with nothing at all: not a valid id either.
got, _, out = run_case("empty", Result(stdout="  \n"))
if got != "unknown":
    problems.append("empty: id is %r, expected 'unknown'" % (got,))
if "WARNING" not in out:
    problems.append("empty: no warning line in %r" % (out,))

if problems:
    for p in problems:
        print("FAIL " + p)
    print("est379_build_id: %d problem(s)" % len(problems))
    sys.exit(1)

print("est379_build_id: git describe success / dirty / failure / missing git -> 'unknown'")
