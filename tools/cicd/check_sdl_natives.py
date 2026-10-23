#!/usr/bin/env python3
"""Cross-check the SDL2 bindings against the native module.

Every native the Objeck SDL libraries (sdl2.obs, sdl_game.obs, sdl_gl.obs) call
through DllProxy->CallFunction is named by a string literal, either assigned to
an @fn_* variable or passed to CallFunction directly. Each such name, outside
comments, must be an exported entry point of core/lib/sdl/sdl.cpp, or the call
fails at run time when the method is first used. This script lists any name
that is called but not exported.

Exit 0 when every called name is exported, 1 otherwise.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OBJECK_SOURCES = [
    ROOT / "core/compiler/lib_src/sdl2.obs",
    ROOT / "core/compiler/lib_src/sdl_game.obs",
    ROOT / "core/compiler/lib_src/sdl_gl.obs",
]
NATIVE_SOURCE = ROOT / "core/lib/sdl/sdl.cpp"

# a binding names its native either through a cached @fn_x := "sdl_..."; or
# directly as CallFunction("sdl_...", ...)
CALLED_RE = re.compile(r'(?:@fn_\w+\s*:=\s*|CallFunction\(\s*)"(\w+)"')
# doc comments in Objeck and comments in C++ hold commented-out bindings
OBJECK_COMMENT_RE = re.compile(r'#~.*?~#|#[^\n]*', re.S)
CPP_COMMENT_RE = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
# void sdl_...(VMContext& context) is how every entry point is declared
EXPORTED_RE = re.compile(r'\bvoid\s+(\w+)\s*\(\s*VMContext\s*&')


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def main():
    exported = set(EXPORTED_RE.findall(CPP_COMMENT_RE.sub("", read(NATIVE_SOURCE))))
    missing = []
    called_total = 0
    for src in OBJECK_SOURCES:
        names = set(CALLED_RE.findall(OBJECK_COMMENT_RE.sub("", read(src))))
        called_total += len(names)
        for name in sorted(names):
            if name not in exported:
                missing.append((src.relative_to(ROOT).as_posix(), name))

    if missing:
        for path, name in missing:
            print(f"MISSING: {path} calls '{name}', which {NATIVE_SOURCE.name} does not export")
        print(f"FAIL: {len(missing)} of {called_total} called natives are not exported")
        return 1

    print(f"OK: all {called_total} called natives are exported by {NATIVE_SOURCE.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
