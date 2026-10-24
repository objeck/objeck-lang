"""The library tables must agree with the compiler's libraries and the sandbox image.

These are the tests that fail when someone adds a library, changes a dependency
or edits one list and forgets the other.
"""

import re
import subprocess
import sys
from pathlib import Path

import pytest

from app.config import settings
from app.services.libmap import DEFAULT_LIBS, LIB_DEPS, lib_status

PLAYGROUND = Path(__file__).resolve().parents[2]
REPO = PLAYGROUND.parents[1]


def test_every_library_has_a_decision():
    """A new .obl is unavailable until someone classifies it -- and the test says so."""
    undecided = [
        lib for lib in LIB_DEPS
        if lib not in DEFAULT_LIBS
        and lib not in settings.allowed_libs
        and lib not in settings.dependency_libs
        and lib not in settings.blocked_libs
    ]
    assert not undecided, (
        f"{undecided} are neither allowed nor blocked in app/config.py; decide, then add "
        "them to allowed_libs/dependency_libs (and docker/build-sandbox.sh) or blocked_libs"
    )


def test_the_sets_do_not_overlap():
    sets = {"allowed": settings.allowed_libs, "dependency": settings.dependency_libs,
            "blocked": settings.blocked_libs}
    names = list(sets)
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            assert not (sets[a] & sets[b]), f"{a} and {b} overlap: {sets[a] & sets[b]}"


def test_policy_names_only_real_libraries():
    for name, libs in (("allowed", settings.allowed_libs), ("dependency", settings.dependency_libs),
                       ("blocked", settings.blocked_libs)):
        unknown = [lib for lib in libs if lib not in LIB_DEPS]
        assert not unknown, f"{name}_libs names libraries that do not exist: {unknown}"


def test_everything_an_allowed_library_needs_is_in_the_image():
    """An allowed library built against a blocked one would fail inside the sandbox."""
    def closure(lib, seen):
        for dep in LIB_DEPS[lib]:
            if dep not in seen:
                seen.add(dep)
                closure(dep, seen)
        return seen

    for lib in settings.allowed_libs:
        for dep in closure(lib, set()):
            assert dep in DEFAULT_LIBS or lib_status(dep) in ("allowed", "dependency"), (
                f"{lib} is allowed but needs {dep}, which is {lib_status(dep)}"
            )


def test_dependency_libs_are_only_there_because_something_needs_them():
    needed = {dep for lib in settings.allowed_libs for dep in LIB_DEPS[lib]}
    unused = settings.dependency_libs - needed
    assert not unused, f"{unused} are not needed by any allowed library; block them instead"


def test_sandbox_image_script_ships_exactly_the_policy():
    script = (PLAYGROUND / "docker" / "build-sandbox.sh").read_text(encoding="utf-8")
    shipped = set(re.search(r'^ALLOWED_LIBS="([^"]+)"', script, re.M).group(1).split())
    expected = set(DEFAULT_LIBS) | settings.allowed_libs | settings.dependency_libs
    assert shipped == expected, (
        f"build-sandbox.sh ships {sorted(shipped)} but config.py allows {sorted(expected)}; "
        f"missing {sorted(expected - shipped)}, extra {sorted(shipped - expected)}"
    )


def test_sandbox_image_ships_the_natives_its_libraries_load():
    """cipher -> libobjk_crypto, ml -> libobjk_ml, diags -> libobjk_diags (DllProxy)."""
    src = REPO / "core" / "compiler" / "lib_src"
    if not src.is_dir():
        pytest.skip("compiler sources not available")
    needed = set()
    for lib in settings.allowed_libs:
        for f in [src / f"{lib}.obs"] + ([src / f"ml_{p}.obs" for p in
                  ("core", "linear", "tree", "bayes", "neighbors", "cluster", "data")] if lib == "ml" else []):
            if f.is_file():
                needed |= set(re.findall(r'DllProxy->New\("libobjk_(\w+)"\)', f.read_text(encoding="utf-8")))
    script = (PLAYGROUND / "docker" / "build-sandbox.sh").read_text(encoding="utf-8")
    shipped = set(re.search(r'^NATIVE_LIBS="([^"]+)"', script, re.M).group(1).split())
    assert needed == shipped, f"allowed libraries load natives {sorted(needed)}; the image ships {sorted(shipped)}"


def test_every_allowed_library_has_a_display_name():
    from app.services.libmap import LIB_INFO
    missing = [lib for lib in settings.allowed_libs if lib not in LIB_INFO]
    assert not missing, f"add LIB_INFO entries for {missing}"


def test_generated_libmap_is_current():
    """libmap_data.json is generated from lib_src and build_libs.sh; it must not go stale."""
    if not (REPO / "core" / "compiler" / "build_libs.sh").is_file():
        pytest.skip("compiler sources not available")
    result = subprocess.run(
        [sys.executable, "-I", str(PLAYGROUND / "tools" / "gen_libmap.py"), "--check"],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, result.stderr or "libmap_data.json is stale; run tools/gen_libmap.py"


def test_configobjk_aliases_are_not_needed_by_the_server():
    """The server passes plain library names, so @group aliases never reach obc."""
    from app.services.validator import validate_libs
    ok, _, _ = validate_libs(["@std"])
    assert not ok
