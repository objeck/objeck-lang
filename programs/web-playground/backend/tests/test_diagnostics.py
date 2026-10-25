"""Reading obc's error output."""

import os
import shutil
import subprocess
import sys

import pytest

from app.services.diagnostics import parse_diagnostics, tidy_output
from app.services.sandbox import EXIT_COMPILE_ERROR, EXIT_COMPILE_TIMEOUT, container_command

SAMPLE = (
    "/tmp/program.obs:(3,10): Undefined class: 'Vector'\n"
    "\t'Vector' is defined in bundle 'Collection' -- add: use Collection;\n"
    "/tmp/program.obs:(3,5): Generic size mismatch, ensure calling parameters and generic types match\n"
    "/tmp/program.obs:(4,10): Undefined class: 'JsonParser'\n"
    "/tmp/program.obs:(4,5): Warning: Variable 'j' is unreferenced\n"
)


def test_parses_line_column_and_message():
    d = parse_diagnostics(SAMPLE)
    assert [(x.line, x.column) for x in d] == [(3, 10), (3, 5), (4, 10), (4, 5)]
    assert d[0].message.startswith("Undefined class: 'Vector'")


def test_indented_detail_lines_belong_to_the_message_above():
    d = parse_diagnostics(SAMPLE)
    assert "add: use Collection;" in d[0].message
    assert "add:" not in d[1].message


def test_warnings_are_marked():
    d = parse_diagnostics(SAMPLE)
    assert [x.severity for x in d] == ["error", "error", "error", "warning"]
    assert d[3].message == "Variable 'j' is unreferenced"


def test_windows_paths_are_handled():
    text = r"C:\Users\x y\AppData\Local\Temp\playground\ab12\program.obs:(7,2): Expected ';'"
    d = parse_diagnostics(text)
    assert (d[0].line, d[0].column) == (7, 2)
    assert tidy_output(text) == "program.obs:(7,2): Expected ';'"


def test_tidy_leaves_other_text_alone():
    assert tidy_output("Compiled 1 class.\nboom") == "Compiled 1 class.\nboom"
    assert tidy_output(SAMPLE).startswith("program.obs:(3,10): Undefined class")


def test_output_without_positions_gives_no_diagnostics():
    assert parse_diagnostics("internal error: out of memory during compilation") == []


# ------------------------------------------------------- the container command

def test_container_command_links_exactly_the_libraries_it_is_given():
    cmd = container_command(["cipher", "net", "json"], 10)
    assert "obc -src /tmp/program.obs -lib cipher,net,json -dest /tmp/program.obe" in cmd


def test_container_command_without_libraries_has_no_lib_flag():
    cmd = container_command([], 10)
    assert "-lib" not in cmd and "obc -src /tmp/program.obs -dest /tmp/program.obe" in cmd


def test_container_command_reports_compile_failure_with_its_own_exit_code():
    cmd = container_command(["json"], 7)
    assert f"exit {EXIT_COMPILE_ERROR}" in cmd and f"exit {EXIT_COMPILE_TIMEOUT}" in cmd
    assert "timeout 7 obr /tmp/program.obe" in cmd
    # the compiler's chatter goes to a log that is shown only on failure
    assert "> /tmp/compile.log 2>&1" in cmd and "cat /tmp/compile.log" in cmd
    assert EXIT_COMPILE_ERROR not in (0, 1, 124) and EXIT_COMPILE_TIMEOUT not in (0, 1, 124)


@pytest.mark.skipif(sys.platform == "win32" or shutil.which("sh") is None, reason="needs a POSIX shell")
@pytest.mark.parametrize("obc,obr,expected_exit,expected_out", [
    ("echo Compiled 1 class.; exit 0", "echo program output", 0, "program output"),
    ("echo 'program.obs:(1,1): bad'; exit 1", "echo SHOULD NOT RUN", EXIT_COMPILE_ERROR, "program.obs:(1,1): bad"),
    ("exit 124", "echo SHOULD NOT RUN", EXIT_COMPILE_TIMEOUT, ""),
    ("exit 0", "echo boom; exit 3", 3, "boom"),
])
def test_container_command_exit_codes(tmp_path, obc, obr, expected_exit, expected_out):
    """Run the real command with stand-in obc/obr: only a compile failure uses the reserved codes."""
    bindir = tmp_path / "bin"
    bindir.mkdir()
    for name, body in (("obc", obc), ("obr", obr)):
        script = bindir / name
        script.write_text("#!/bin/sh\n" + body + "\n")
        script.chmod(0o755)
    src = tmp_path / "program.obs"
    src.write_text("x")
    work = tmp_path / "work"
    work.mkdir()
    # Substitute /tmp/ FIRST: on Linux pytest's tmp_path itself lives under /tmp/,
    # so replacing it after the source path went in would mangle that path.
    cmd = container_command(["json"], 5).replace("/tmp/", str(work) + "/").replace("/input/program.obs", str(src))
    result = subprocess.run(["sh", "-c", cmd], capture_output=True, text=True,
                            env={**os.environ, "PATH": f"{bindir}:{os.environ['PATH']}"})
    assert result.returncode == expected_exit
    assert result.stdout.strip() == expected_out
    assert "SHOULD NOT RUN" not in result.stdout
