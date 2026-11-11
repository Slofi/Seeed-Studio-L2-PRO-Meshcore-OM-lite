"""PlatformIO pre-build script: give every image a build id (EST-379).

The id is `git describe --always --dirty --abbrev=7` run in the firmware folder: the short commit
hash, plus "-dirty" when the tree has changes. It carries no date and no path, so two builds of the
same tree get the same id. `git describe` only reads `.git`, so this works where `.git` is
read-only. If git is missing or fails, the id is "unknown" and the build still runs (with a
warning line).

Exported to the C++ code as the string macro OM_BUILD_ID, through CPPDEFINES.
"""
import subprocess
from pathlib import Path

UNKNOWN = "unknown"

GIT_DESCRIBE = ["git", "describe", "--always", "--dirty", "--abbrev=7"]


def git_build_id(firmware_dir, run=subprocess.run):
    """Return the short commit id (with -dirty), or "unknown" if git cannot answer.

    `run` is injectable so the host test can fake git (success, dirty, failure, missing binary)
    without a repository. A warning is printed on the fallback path.
    """
    try:
        proc = run(
            GIT_DESCRIBE,
            cwd=str(firmware_dir),
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        print("build_id: WARNING git unavailable (%s), OM_BUILD_ID = %s" % (exc, UNKNOWN))
        return UNKNOWN

    out = (proc.stdout or "").strip()
    if proc.returncode != 0 or not out:
        reason = (proc.stderr or "").strip() or "exit %d" % proc.returncode
        print("build_id: WARNING git describe failed (%s), OM_BUILD_ID = %s" % (reason, UNKNOWN))
        return UNKNOWN

    # One token is the contract; keep only the first line if a wrapper ever prints more.
    return out.splitlines()[0].strip()


def configure(env):
    """Pre-build hook: find the id and define OM_BUILD_ID for every translation unit."""
    firmware_dir = Path(env.subst("$PROJECT_DIR"))
    build_id = git_build_id(firmware_dir)
    # PlatformIO's StringifyMacro makes the C string literal's quotes survive CPPDEFINES (a plain
    # '"%s"' loses them, and `1ee888c-dirty` then compiles as arithmetic).
    env.Append(CPPDEFINES=[("OM_BUILD_ID", env.StringifyMacro(build_id))])
    print("build_id: OM_BUILD_ID = %s" % build_id)


try:
    Import("env")          # provided by SCons/PlatformIO; NameError in plain Python (host test)
except NameError:
    env = None

if env is not None:
    configure(env)
