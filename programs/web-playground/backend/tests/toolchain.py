"""Find an Objeck toolchain to run the demos against.

Order: $OBJECK_DEPLOY (a directory holding bin/ and lib/), then the deploy trees a
repository checkout builds (core/release/deploy-x64 on Windows, core/release/deploy
elsewhere). Returns None when there is none, and the demo tests skip.
"""

import os
import sys
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]


@dataclass(frozen=True)
class Toolchain:
    root: Path
    obc: Path
    obr: Path
    lib: Path


def find() -> Toolchain | None:
    exe = ".exe" if sys.platform == "win32" else ""
    candidates = []
    if os.environ.get("OBJECK_DEPLOY"):
        candidates.append(Path(os.environ["OBJECK_DEPLOY"]))
    candidates += [REPO / "core" / "release" / "deploy-x64", REPO / "core" / "release" / "deploy"]

    for root in candidates:
        obc, obr, lib = root / "bin" / f"obc{exe}", root / "bin" / f"obr{exe}", root / "lib"
        if obc.is_file() and obr.is_file() and (lib / "lang.obl").is_file():
            return Toolchain(root, obc, obr, lib)
    return None
