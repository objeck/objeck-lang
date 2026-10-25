"""The pipeline against a real obc/obr (local mode; skipped without a toolchain)."""

import asyncio
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import toolchain  # noqa: E402

from app.config import settings  # noqa: E402
from app.services.libmap import BUNDLE_PROVIDERS, LANG_BLOCKED_BUNDLES, lib_status  # noqa: E402


@pytest.fixture(scope="module")
def tc(tmp_path_factory):
    found = toolchain.find()
    if found is None:
        pytest.skip("no Objeck toolchain (set OBJECK_DEPLOY or build core/release/deploy-x64)")
    settings.local_dev = True
    settings.obc_path, settings.obr_path = str(found.obc), str(found.obr)
    settings.objeck_lib_path = str(found.lib)
    settings.host_tmp_dir = str(tmp_path_factory.mktemp("playground"))
    return found


def run(code, libs=None, timeout=10):
    from app.services.runner import execute
    return asyncio.run(execute(code, libs or [], timeout))


MAIN = 'class T { function : Main(args : String[]) ~ Nil { "ok"->PrintLine(); } }'

# Every bundle a program is allowed to import must really compile with the
# libraries the server picks for it. This is the test that catches a missing
# dependency in the closure ("Unable to resolve external library class").
USABLE_BUNDLES = sorted(
    b for b, libs in BUNDLE_PROVIDERS.items()
    if b not in LANG_BLOCKED_BUNDLES
    and all(lib_status(l) in ("allowed", "default") for l in libs)
)


@pytest.mark.parametrize("bundle", USABLE_BUNDLES)
def test_every_allowed_bundle_compiles_and_runs(tc, bundle):
    result = run(f"use {bundle};\n{MAIN}")
    assert result.success, f"use {bundle}: {result.error}\n{result.output}"
    assert result.output.strip() == "ok"


def test_the_whole_allowed_surface_links_together(tc):
    uses = ", ".join(b for b in USABLE_BUNDLES if b != "System.Concurrency")
    result = run(f"use {uses};\n{MAIN}")
    assert result.success, result.output


def test_compile_error_carries_positions_and_hint(tc):
    result = run(
        "class T {\n  function : Main(args : String[]) ~ Nil {\n"
        '    p := JsonParser->New("1");\n  }\n}'
    )
    assert result.compile_error and not result.success and not result.blocked
    assert [d.line for d in result.diagnostics if d.severity == "error"] == [3]
    assert "use Data.JSON;" in result.hint
    assert "Wrote target file" not in result.output
    assert result.output.startswith("program.obs:(3,")


def test_runtime_error_is_not_a_compile_error(tc):
    result = run(
        "class T {\n  function : Main(args : String[]) ~ Nil {\n"
        '    a := Int->New[2]; a[9] := 1;\n  }\n}'
    )
    assert not result.success and not result.compile_error
    assert "Index out of bounds" in result.output


def test_a_program_that_prints_compiler_words_is_not_a_compile_error(tc):
    result = run('class T { function : Main(args : String[]) ~ Nil { "Undefined class: Expected obc:"->PrintLine(); } }')
    assert result.success and not result.compile_error


def test_output_has_no_compiler_chatter(tc):
    result = run(MAIN)
    assert result.output == "ok"


def test_timeout_is_reported_as_a_timeout(tc):
    result = run("class T { function : Main(args : String[]) ~ Nil { while(true) { }; } }", timeout=1)
    assert not result.success and "timed out" in result.error


def test_query_with_files_in_sql_is_refused_before_running(tc):
    result = run(
        "use Query.Structured;\nclass T { function : Main(args : String[]) ~ Nil {\n"
        "  Catalog->New()->Query(\"select * from 'x.csv'\");\n} }"
    )
    assert result.blocked and "file or URL" in result.error


def test_cipher_natives_load(tc):
    result = run(
        'use Cipher;\nclass T { function : Main(args : String[]) ~ Nil {\n'
        '  Hash->SHA256("abc"->ToByteArray())->ToHexString()->PrintLine();\n} }'
    )
    # SHA-256("abc") = ba7816bf 8f01cfea ... Byte[]->ToHexString printed [0xba,0x78,...]
    # before ByteBuffer landed and plain hex after; accept both.
    digits = result.output.replace("[0x", "").replace(",0x", "")
    assert result.success and digits.startswith("ba7816bf")
