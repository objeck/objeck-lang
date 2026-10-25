"""What the sandbox refuses: libraries given by hand, and calls inside the source."""

import pytest

from app.services.validator import (
    check_code,
    validate_code,
    validate_demo_id,
    validate_libs,
    validate_share_id,
)


def body(statement: str) -> str:
    return f"class A {{ function : Main(args : String[]) ~ Nil {{ {statement} }} }}"


# ------------------------------------------------------------ manual libraries

def test_empty_override_is_fine():
    assert validate_libs([]) == (True, "", [])


def test_override_normalises_names_and_aliases():
    ok, _, libs = validate_libs(["Collect", "JSON", "csv.obl", "json"])
    assert ok and libs == ["json", "csv"]  # collect -> gen_collect is a default, dropped; no duplicates


def test_old_share_links_still_validate():
    # what the old UI sent: the chips ticked by hand
    for old in (["collect"], ["json", "collect"], ["xml", "regex", "cipher", "csv", "query", "ml", "misc", "nlp", "diags"]):
        ok, err, libs = validate_libs(old)
        assert ok, (old, err)


@pytest.mark.parametrize("lib", ["net_server", "odbc", "sdl2", "opencv", "openai", "gemini", "ollama",
                                 "onnx", "lame", "json_rpc", "rss", "web_server", "query_odbc"])
def test_blocked_libraries_are_refused_with_a_reason(lib):
    ok, err, libs = validate_libs([lib])
    assert not ok and libs == []
    assert "isn't available in the playground" in err and lib in err


def test_net_is_a_dependency_not_a_user_choice():
    ok, err, _ = validate_libs(["net"])
    assert not ok and "isn't available" in err


def test_unknown_library():
    ok, err, _ = validate_libs(["frobnicate"])
    assert not ok and "Unknown library" in err


@pytest.mark.parametrize("name", ["../etc/passwd", "json;rm", "a b", "J$ON", "x" * 40, "@std", "1json"])
def test_invalid_names_are_refused(name):
    ok, err, _ = validate_libs([name])
    assert not ok and "Invalid library name" in err


def test_too_many_libraries():
    ok, err, _ = validate_libs(["json"] * 11)
    assert not ok and "Too many" in err


# ----------------------------------------------------------------- source policy

@pytest.mark.parametrize("statement,what", [
    ('Runtime->Command("ls");', "Runtime->Command"),
    ('x := Runtime->CommandOutput("ls");', "Runtime->CommandOutput"),
    ('System.Runtime->Command("ls");', "Runtime->Command"),
    ('Runtime  ->  Command("ls");', "Runtime->Command"),
    ('Runtime->Execute("ls");', "Runtime->Execute"),
    ('Runtime->IsProcessAlive(1);', "Runtime->IsProcessAlive"),
    ('Runtime->Exit(0);', "Runtime->Exit"),
    ('Runtime->GetEnvironmentVariable("HOME");', "Runtime->GetEnvironmentVariable"),
    ('Runtime->SetEnvironmentVariable("A", "b");', "Runtime->SetEnvironmentVariable"),
    ('p := DllProxy->New("libobjk_x");', "DllProxy"),
    ('p := Pipe->New("x");', "Pipe"),
    ('s := TCPSocket->New("h", 80);', "TCPSocket"),
    ('s := UDPSocket->New("h", 80);', "UDPSocket"),
    ('s := DTLSSocket->New("h", 80);', "DTLSSocket"),
    ('c := HttpsClient->New();', "HttpsClient"),
    ('s := CsvSource->New("a.csv");', "CsvSource"),
    ('s := FilesystemSource->New("/", true);', "FilesystemSource"),
    ('s := ArchiveSource->New("a.zip");', "ArchiveSource"),
    ('s := XlsxSource->New("a.xlsx");', "XlsxSource"),
    ('s := RemoteSource->New("http://x", "t");', "RemoteSource"),
    ('s := HttpJsonSource->New("http://x", "t", "a");', "HttpJsonSource"),
    ('s := JsonSource->New("a.json");', "JsonSource"),
    ('s := TextSource->New("a.log", "x", names);', "TextSource"),
    ('s := TableStore->New("db");', "TableStore"),
    ('f := OtabFile->New("a.otab");', "OtabFile"),
])
def test_blocked_calls_are_named(statement, what):
    refusals = check_code(body(statement))
    assert [r.what for r in refusals] == [what], refusals
    ok, err = validate_code(body(statement))
    assert not ok and err.startswith(what) and "isn't available in the playground" in err


@pytest.mark.parametrize("sql", [
    "select * from 'runs.csv'",
    "SELECT r.id FROM 'runs.csv' r JOIN 'meta.csv' m ON r.id = m.id",
    "select * from  'results/run_*.csv' group by name",
    'select * from \'https://example.org/data.csv\'',
    "select * from\n'x.jsonl'",
])
def test_sql_that_names_a_file_or_url_is_refused(sql):
    refusals = check_code(body(f'catalog->Query("{sql}");'))
    assert len(refusals) == 1 and "file or URL" in refusals[0].what


@pytest.mark.parametrize("sql", [
    "select * from staff where name = 'Ada'",
    "select name from staff s join depts d on s.dept = d.id where d.title = 'Design'",
    "select 'from' as word",
    "insert into t values ('a', 'b')",
])
def test_ordinary_sql_with_quoted_values_is_allowed(sql):
    assert check_code(body(f'catalog->Query("{sql}");')) == []


def test_the_policy_reads_code_not_comments():
    code = (
        '# Runtime->Command("ls")\n'
        '#~ DllProxy->New("x"); TCPSocket ~#\n' + body('"fine"->PrintLine();')
    )
    assert check_code(code) == []


def test_a_call_hidden_in_an_interpolation_is_still_caught():
    refusals = check_code(body('"{$(Runtime->CommandOutput("id"))}"->PrintLine();'))
    assert {r.what for r in refusals} >= {"Runtime->CommandOutput"}


def test_refusals_carry_line_numbers():
    code = "class A {\n  function : Main(args : String[]) ~ Nil {\n\n    Runtime->Command(\"x\");\n  }\n}"
    assert check_code(code)[0].line == 4


def test_each_refusal_is_reported_once():
    code = body('Runtime->Command("a"); Runtime->Command("b");')
    assert len(check_code(code)) == 1


def test_a_name_that_merely_contains_a_blocked_word_is_fine():
    assert check_code(body("x := MyPipeline->New(); y := Bypass->Run();")) == []
    assert check_code(body("t := TableStoreIsNotTheClass;")) == []


def test_normal_programs_pass():
    assert check_code(body('"Hello"->PrintLine(); x := 3 + 4;')) == []


# ----------------------------------------------------------------- code basics

def test_empty_code():
    assert validate_code("") == (False, "Code cannot be empty")
    assert validate_code("  \n ") == (False, "Code cannot be empty")


def test_control_characters():
    ok, err = validate_code("class A {}\x07")
    assert not ok and "control characters" in err
    assert validate_code("class A {}\t\r\n")[0]


def test_oversize_code():
    ok, err = validate_code("x" * 70000)
    assert not ok and "maximum size" in err


def test_id_validators():
    assert validate_share_id("abc_DEF-123") and not validate_share_id("a/b")
    assert validate_demo_id("sql-group-join") and not validate_demo_id("../x")
