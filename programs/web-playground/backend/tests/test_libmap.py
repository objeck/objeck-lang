"""Library inference from `use` statements, and the policy on top of it."""

import pytest

from app.services.libmap import (
    BUNDLE_PROVIDERS,
    DEFAULT_LIBS,
    LIB_DEPS,
    analyze,
    canonical_lib,
    lib_status,
    link_order,
    missing_bundle_hint,
)
from app.services.planning import plan


def libs_of(code: str) -> list[str]:
    result = plan(code)
    assert result.ok, result.error
    return result.libs


def link_of(code: str) -> list[str]:
    result = plan(code)
    assert result.ok, result.error
    return result.link_libs


def refusal_of(code: str) -> str:
    result = plan(code)
    assert not result.ok
    return result.error


# ------------------------------------------------------------ bundle -> library

@pytest.mark.parametrize("use,expected", [
    ("Data.JSON", ["json"]),
    ("Data.JSON.Scheme", ["json"]),
    ("Data.XML", ["xml"]),
    ("Data.CSV", ["csv"]),
    ("Query.RegEx", ["regex"]),
    ("Cipher", ["cipher"]),
    ("System.ML", ["ml"]),
    ("System.NLP", ["nlp"]),
    ("System.AI", ["ai"]),
    ("System.Math", ["math"]),
    ("System.Diagnostics", ["diags"]),
    ("System.Utility", ["misc"]),
    ("Query.Structured.ML", ["query_ml"]),
])
def test_single_bundle_maps_to_its_library(use, expected):
    assert [x for x in libs_of(f"use {use};\nclass A {{}}") if x != "gen_collect"] == expected


@pytest.mark.parametrize("use", [
    "Collection", "Collection.Tuple", "System", "System.IO", "System.Introspection",
    "System.Time", "System.Concurrency",
])
def test_bundles_in_default_libraries_need_no_lib(use):
    result = plan(f"use {use};\nclass A {{}}")
    assert result.ok
    assert result.link_libs == []


def test_collection_is_reported_as_used_but_never_linked():
    result = plan("use Collection;\nclass A {}")
    assert result.libs == ["gen_collect"]
    assert result.link_libs == []


def test_system_time_needs_misc_only_for_the_classes_misc_adds():
    assert link_of("use System.Time;\nclass A { function : F() ~ Nil { d := Date->New(); } }") == []
    assert "misc" in link_of("use System.Time;\nclass A { function : F() ~ Nil { d := DateUtility->New(); } }")


def test_system_concurrency_needs_the_concurrent_library_only_for_its_classes():
    assert link_of("use System.Concurrency;\nclass A { function : F() ~ Nil { t := Thread->New(); } }") == []
    assert "concurrent" in link_of("use System.Concurrency;\nclass A { function : F() ~ Nil { s := Semaphore->New(1); } }")


def test_query_structured_links_its_whole_dependency_closure():
    link = link_of("use Query.Structured;\nclass A {}")
    # obc does not chase dependencies; these are exactly what its linker asked for
    assert set(link) == {"query", "net", "regex", "csv", "xml", "json", "misc", "cipher"}
    assert link[-1] == "query"
    # dependencies come before what needs them
    for lib in link:
        for dep in LIB_DEPS[lib]:
            if dep not in DEFAULT_LIBS:
                assert link.index(dep) < link.index(lib), (dep, lib)


def test_query_ml_adds_ml_to_the_closure():
    link = link_of("use Query.Structured, Query.Structured.ML;\nclass A {}")
    assert {"query_ml", "ml", "query", "csv"} <= set(link)


def test_use_list_with_several_bundles_and_continuations():
    code = "use Collection,\n    Data.JSON,\n    Data.CSV;\nclass A {}"
    assert libs_of(code) == ["gen_collect", "json", "csv"]


def test_use_bundle_form():
    assert "json" in libs_of("use bundle Data.JSON;\nclass A {}")


def test_use_class_form_is_not_a_bundle_import():
    result = plan("use class System.IO.Console;\nclass A {}")
    assert result.ok and result.libs == []


def test_fully_qualified_reference_needs_no_use_line():
    code = "class A { function : F() ~ Nil { h := Cipher.Hash->SHA256(\"x\"->ToByteArray()); } }"
    assert libs_of(code) == ["cipher"]


def test_dotted_names_use_the_longest_bundle():
    # Data.JSON.Stream is its own (blocked) library, not Data.JSON plus a suffix:
    # it must be refused as itself, and must not drag in json
    assert "Data.JSON.Stream isn't available" in refusal_of("use Data.JSON.Stream;\nclass A {}")
    assert libs_of("use Data.JSON;\nclass A {}") == ["json"]


def test_a_program_declaring_its_own_bundle_is_not_unknown():
    result = analyze("use Shapes;\nbundle Shapes { class Circle {} }")
    assert result.unknown_bundles == []


def test_unknown_bundle_is_reported_and_left_to_the_compiler():
    result = analyze("use Made.Up;\nclass A {}")
    assert result.unknown_bundles == ["Made.Up"]
    assert plan("use Made.Up;\nclass A {}").ok


def test_comments_and_strings_do_not_trigger_detection():
    code = (
        "# use Web.HTTP;\n"
        "#~ use Database.ODBC; ~#\n"
        'class A { function : F() ~ Nil { "use Data.JSON; # not a comment"->PrintLine(); } }'
    )
    result = plan(code)
    assert result.ok and result.libs == []


def test_a_hash_inside_a_string_does_not_start_a_comment():
    # if the # opened a comment, the reference after it on the same line would vanish
    code = 'class A { function : F() ~ Nil { x := "#"; j := Data.JSON.JsonParser->New("1"); } }'
    assert libs_of(code) == ["json"]


def test_manual_override_adds_libraries():
    result = plan("class A {}", ["json", "collect", "csv"])
    assert result.ok
    assert result.libs == ["json", "csv"]  # collect is a default; it is not extra
    assert result.link_libs == ["json", "csv"]


def test_manual_override_cannot_unlock_a_blocked_library():
    assert not plan("class A {}", ["net_server"]).ok
    assert not plan("class A {}", ["net"]).ok  # dependency-only, not user-selectable


# ------------------------------------------------------------------- refusals

@pytest.mark.parametrize("use,fragment", [
    ("Web.HTTP", "Web.HTTP isn't available in the playground"),
    ("Web.HTTP.Server", "Web.HTTP.Server isn't available"),
    ("Web.Server", "Web.Server isn't available"),
    ("Data.JSON.RPC", "Data.JSON.RPC isn't available"),
    ("Web.RSS", "Web.RSS isn't available"),
    ("Database.ODBC", "Database.ODBC isn't available"),
    ("Query.Structured.Odbc", "Query.Structured.Odbc isn't available"),
    ("Query.Structured.Server", "Query.Structured.Server isn't available"),
    ("Query.Structured.Vision", "Query.Structured.Vision isn't available"),
    ("Game.SDL2", "Game.SDL2 isn't available"),
    ("Game.Framework", "Game.Framework isn't available"),
    ("Game.OpenGL", "Game.OpenGL isn't available"),
    ("API.OpenCV", "API.OpenCV isn't available"),
    ("API.Onnx", "API.Onnx isn't available"),
    ("API.OpenAI", "API.OpenAI isn't available"),
    ("API.OpenAI.Chat", "API.OpenAI.Chat isn't available"),
    ("API.Google.Gemini", "API.Google.Gemini isn't available"),
    ("API.Ollama", "API.Ollama isn't available"),
    ("System.IO.Net", "System.IO.Net isn't available"),
    ("System.IO.Net.OAuth", "System.IO.Net.OAuth isn't available"),
    ("System.IO.Filesystem", "System.IO.Filesystem isn't available"),
    ("System.API", "System.API isn't available"),
])
def test_blocked_bundles_get_a_clear_message(use, fragment):
    message = refusal_of(f"use {use};\nclass A {{}}")
    assert fragment in message
    assert "line 1" in message


def test_blocked_bundle_in_a_list_names_only_that_bundle():
    message = refusal_of("use Collection, Web.HTTP, Data.JSON;\nclass A {}")
    assert "Web.HTTP" in message and "Collection" not in message and "Data.JSON" not in message


def test_blocked_fully_qualified_reference_is_caught_without_a_use_line():
    message = refusal_of('class A { function : F() ~ Nil { c := Web.HTTP.HttpClient->New(); } }')
    assert "Web.HTTP" in message


def test_refusal_reports_the_line_it_is_on():
    message = refusal_of("class A {}\n\n\nuse Web.HTTP;\n")
    assert "line 4" in message


def test_the_network_message_says_why():
    assert "no network" in refusal_of("use Web.HTTP;\nclass A {}")
    assert "display or audio" in refusal_of("use Game.SDL2;\nclass A {}")
    assert "database" in refusal_of("use Database.ODBC;\nclass A {}")


# ------------------------------------------------------------- helpers/policy

def test_canonical_lib_names():
    assert canonical_lib("collect") == "gen_collect"
    assert canonical_lib(" JSON ") == "json"
    assert canonical_lib("csv.obl") == "csv"


def test_link_order_puts_dependencies_first_and_drops_defaults():
    assert link_order(["query"]) == ["cipher", "net", "regex", "csv", "xml", "json", "misc", "query"]
    assert link_order(["json", "json"]) == ["json"]
    assert link_order(["gen_collect", "lang"]) == []


def test_status_of_known_libraries():
    assert lib_status("lang") == "default"
    assert lib_status("gen_collect") == "default"
    assert lib_status("json") == "allowed"
    assert lib_status("net") == "dependency"
    assert lib_status("openai") == "blocked"
    assert lib_status("not_a_library") == "blocked"


# ---------------------------------------------------------------------- hints

def test_hint_for_a_class_whose_use_line_is_missing():
    result = analyze("class A {}")
    hint = missing_bundle_hint("program.obs:(4,10): Undefined class: 'JsonParser'", result)
    assert "use Data.JSON;" in hint


def test_hint_for_a_class_in_a_blocked_bundle():
    result = analyze("class A {}")
    hint = missing_bundle_hint("program.obs:(2,3): Undefined class: 'HttpClient'", result)
    assert "isn't available in the playground" in hint


def test_hint_suggests_a_close_class_name_for_a_typo():
    result = analyze("class A {}")
    hint = missing_bundle_hint("program.obs:(2,3): Undefined class: 'JsonParsr'", result)
    assert "JsonParser" in hint


def test_hint_for_a_misspelled_bundle():
    result = analyze("class A {}")
    hint = missing_bundle_hint("program.obs:(1,1): Bundle name 'Data.JSN' not defined in program or linked libraries", result)
    assert "Data.JSON" in hint


def test_no_hint_for_a_class_the_user_just_made_up():
    result = analyze("class A {}")
    assert missing_bundle_hint("program.obs:(5,10): Undefined class: 'Foo'", result) == ""


def test_every_indexed_bundle_has_a_provider():
    assert all(BUNDLE_PROVIDERS[b] for b in BUNDLE_PROVIDERS)
