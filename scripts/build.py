"""Dependency-free native build/test entry point for Linux, macOS and Windows."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cl" if sys.platform == "win32" else "cc"))
    parser.add_argument("--cflags", default=os.environ.get("CFLAGS"))
    parser.add_argument("--test", action="store_true")
    args = parser.parse_args()
    (ROOT / "build").mkdir(exist_ok=True)
    compiler = shlex.split(args.cc, posix=sys.platform != "win32")
    msvc = Path(compiler[0]).stem.lower() == "cl"
    suffix = ".dll" if sys.platform == "win32" else ".dylib" if sys.platform == "darwin" else ".so"
    target = "build/libembsim" + suffix
    if msvc:
        flags = shlex.split(args.cflags) if args.cflags else ["/nologo", "/O2", "/W4", "/WX", "/std:c11"]
        command = compiler + flags + ["/DES_BUILD", "/LD", "/Iinclude", "src/kernel.c", "/Fobuild/kernel.obj",
                                      "/Fe" + target, "/link", "/IMPLIB:build/embsim.lib"]
    else:
        flags = shlex.split(args.cflags) if args.cflags else ["-O2", "-g", "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        shared = ["-dynamiclib", "-fPIC"] if sys.platform == "darwin" else ["-shared"] + ([] if sys.platform == "win32" else ["-fPIC"])
        command = compiler + flags + shared + ["-DES_BUILD", "-Iinclude", "src/kernel.c", "-o", target]
    print("Building", target, "with", args.cc, flush=True)
    subprocess.run(command, cwd=ROOT, check=True)
    if args.test:
        subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-v"], cwd=ROOT,
                       env=dict(os.environ, PYTHONPATH=str(ROOT / "python")), check=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"build: {error}", file=sys.stderr)
        sys.exit(1)
