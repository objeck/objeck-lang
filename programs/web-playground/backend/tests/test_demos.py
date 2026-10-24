"""Every demo must compile and run, with libraries picked by the server.

This goes through the same pipeline /api/run uses (plan -> obc -lib <closure> ->
obr), against a real toolchain, in local mode (no Docker). Skipped when no
toolchain is found; see toolchain.py. To run it by hand and see a table:

    OBJECK_DEPLOY=<deploy dir> python backend/tests/test_demos.py
"""

import asyncio
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import toolchain  # noqa: E402

DEMOS = Path(__file__).resolve().parents[2] / "demos"
INDEX = json.loads((DEMOS / "index.json").read_text(encoding="utf-8"))
ALL = [
    (cat["name"], d["id"], DEMOS / cat["folder"] / d["file"])
    for cat in INDEX["categories"] for d in cat["demos"]
]

# Phrases that mean the run went wrong even if the process exited 0
_FAILURE_MARKERS = (">>>", "Unwinding", "Undefined class", "Unable to resolve", "not defined")


def _configure(tc: "toolchain.Toolchain", tmp: Path) -> None:
    from app.config import settings
    settings.local_dev = True
    settings.obc_path = str(tc.obc)
    settings.obr_path = str(tc.obr)
    settings.objeck_lib_path = str(tc.lib)
    settings.host_tmp_dir = str(tmp)


def run_demo(path: Path):
    from app.services.runner import execute
    return asyncio.run(execute(path.read_text(encoding="utf-8"), [], 10))


@pytest.fixture(scope="session")
def tc(tmp_path_factory):
    found = toolchain.find()
    if found is None:
        pytest.skip("no Objeck toolchain (set OBJECK_DEPLOY or build core/release/deploy-x64)")
    _configure(found, tmp_path_factory.mktemp("playground"))
    return found


def test_index_files_exist():
    missing = [str(p) for _, _, p in ALL if not p.is_file()]
    assert not missing, f"index.json names files that do not exist: {missing}"


def test_every_demo_file_is_indexed():
    indexed = {p.resolve() for _, _, p in ALL}
    on_disk = {p.resolve() for p in DEMOS.glob("*/*.obs")}
    assert on_disk == indexed, f"unlisted demos: {sorted(map(str, on_disk - indexed))}"


def test_demo_ids_are_unique_and_valid():
    from app.services.validator import validate_demo_id
    ids = [i for _, i, _ in ALL]
    assert len(ids) == len(set(ids))
    assert all(validate_demo_id(i) for i in ids)


def test_no_demo_is_refused_by_the_policy():
    """Needs no toolchain: the sandbox policy must accept every shipped demo."""
    from app.services.planning import plan
    for cat, demo_id, path in ALL:
        result = plan(path.read_text(encoding="utf-8"))
        assert result.ok, f"{demo_id} ({cat}) is refused: {result.error}"


@pytest.mark.parametrize("category,demo_id,path", ALL, ids=[i for _, i, _ in ALL])
def test_demo_compiles_and_runs(tc, category, demo_id, path):
    result = run_demo(path)
    assert not result.blocked, result.error
    assert not result.compile_error, f"{demo_id}: {result.output}\nhint: {result.hint}"
    assert result.success, f"{demo_id}: {result.error}\n{result.output}"
    assert result.output.strip(), f"{demo_id} printed nothing"
    for marker in _FAILURE_MARKERS:
        assert marker not in result.output, f"{demo_id}: {marker!r} in output:\n{result.output}"
    # the compiler's chatter must not leak into what the user sees
    assert "Wrote target file" not in result.output


def main() -> int:
    tc_ = toolchain.find()
    if tc_ is None:
        print("no toolchain found")
        return 2
    import tempfile
    _configure(tc_, Path(tempfile.mkdtemp(prefix="pg_demos_")))
    bad = 0
    for category, demo_id, path in ALL:
        r = run_demo(path)
        ok = r.success and not r.compile_error and not r.blocked and r.output.strip() != ""
        bad += not ok
        libs = ",".join(r.libs) or "-"
        print(f"{'ok  ' if ok else 'FAIL'} {demo_id:<22} {r.execution_time_ms:>6} ms  libs={libs}")
        if not ok:
            print("     " + (r.error or r.output)[:400].replace("\n", "\n     "))
    print(f"\n{len(ALL) - bad}/{len(ALL)} demos ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
