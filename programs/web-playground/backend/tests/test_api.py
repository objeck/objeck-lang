"""The HTTP contract: /api/run, /api/share, /api/demos, /api/health.

The sandbox is replaced with a recorder, so these run anywhere and check what the
server decided (libraries, refusals) rather than what a compiler said.
"""

import pytest
from fastapi.testclient import TestClient

from app.main import app
from app.models.schemas import RunResponse
from app.services import runner


@pytest.fixture()
def client():
    with TestClient(app) as c:  # the context manager runs the lifespan (share db)
        yield c


@pytest.fixture()
def sandbox(monkeypatch):
    """Record what would have been sent to the container."""
    calls = []

    async def fake_run_code(code, libs, timeout):
        calls.append({"code": code, "libs": libs, "timeout": timeout})
        return RunResponse(success=True, output="ran", execution_time_ms=5)

    monkeypatch.setattr(runner, "run_code", fake_run_code)
    return calls


def run(client, code, **extra):
    return client.post("/api/run", json={"code": code, **extra})


JSON_CODE = 'use Data.JSON;\nclass A { function : Main(args : String[]) ~ Nil { "x"->PrintLine(); } }'


def test_run_works_out_libraries_without_being_told(client, sandbox):
    r = run(client, JSON_CODE)
    assert r.status_code == 200
    body = r.json()
    assert body["success"] and body["output"] == "ran"
    assert body["libs"] == ["json"]
    assert sandbox[0]["libs"] == ["json"]


def test_run_links_the_dependency_closure(client, sandbox):
    run(client, "use Query.Structured;\nclass A {}")
    assert sandbox[0]["libs"] == ["cipher", "net", "regex", "csv", "xml", "json", "misc", "query"]


def test_run_with_no_libraries_passes_none(client, sandbox):
    run(client, 'class A { function : Main(args : String[]) ~ Nil { "hi"->PrintLine(); } }')
    assert sandbox[0]["libs"] == []


def test_old_clients_that_send_libs_still_work(client, sandbox):
    body = run(client, "class A {}", libs=["collect", "csv"]).json()
    assert body["success"] and body["libs"] == ["csv"]
    assert sandbox[0]["libs"] == ["csv"]


def test_manual_libs_are_added_to_detected_ones(client, sandbox):
    run(client, JSON_CODE, libs=["csv"])
    assert sandbox[0]["libs"] == ["json", "csv"]


def test_blocked_library_is_refused_before_a_container_starts(client, sandbox):
    r = run(client, "use Web.HTTP;\nclass A {}")
    body = r.json()
    assert r.status_code == 200
    assert not body["success"] and body["blocked"]
    assert "Web.HTTP isn't available in the playground" in body["error"]
    assert body["output"] == ""
    assert sandbox == []


def test_blocked_manual_library_is_refused(client, sandbox):
    body = run(client, "class A {}", libs=["openai"]).json()
    assert body["blocked"] and "openai" in body["error"]
    assert sandbox == []


def test_blocked_call_is_refused(client, sandbox):
    body = run(client, 'class A { function : Main(args : String[]) ~ Nil { Runtime->Command("id"); } }').json()
    assert body["blocked"] and "Runtime->Command" in body["error"]
    assert sandbox == []


def test_compile_error_gets_positions_and_a_hint(client, monkeypatch):
    async def failing(code, libs, timeout):
        return RunResponse(
            success=False, compile_error=True, error="Compilation failed",
            output="/tmp/program.obs:(4,10): Undefined class: 'JsonParser'",
        )
    monkeypatch.setattr(runner, "run_code", failing)
    body = run(client, "class A {}").json()
    assert body["compile_error"]
    assert body["output"].startswith("program.obs:(4,10)")
    assert body["diagnostics"] == [
        {"line": 4, "column": 10, "severity": "error", "message": "Undefined class: 'JsonParser'"}
    ]
    assert "use Data.JSON;" in body["hint"]


def test_crlf_in_output_is_normalised(client, monkeypatch):
    async def csv(code, libs, timeout):
        return RunResponse(success=True, output="a,b\r\n1,2\r\r\n")
    monkeypatch.setattr(runner, "run_code", csv)
    assert run(client, "class A {}").json()["output"] == "a,b\n1,2\n"


def test_empty_code_is_rejected_by_the_schema(client):
    assert client.post("/api/run", json={"code": ""}).status_code == 422


def test_response_keeps_the_original_fields(client, sandbox):
    body = run(client, "class A {}").json()
    for key in ("success", "output", "error", "compile_error", "execution_time_ms", "truncated"):
        assert key in body


# --------------------------------------------------------------------- share

def test_share_round_trip_without_libs(client):
    r = client.post("/api/share", json={"code": JSON_CODE})
    assert r.status_code == 200
    share = r.json()
    got = client.get(f"/api/share/{share['id']}")
    assert got.status_code == 200
    assert got.json() == {"code": JSON_CODE, "libs": []}


def test_old_share_with_a_libs_list_still_loads(client):
    r = client.post("/api/share", json={"code": "class Old {}", "libs": ["collect", "json"]})
    assert r.status_code == 200
    got = client.get(f"/api/share/{r.json()['id']}").json()
    assert got["libs"] == ["json"]  # canonicalised; collect is a default library


def test_share_of_blocked_code_is_refused(client):
    r = client.post("/api/share", json={"code": "use Web.HTTP;\nclass A {}"})
    assert r.status_code == 400
    assert "isn't available" in r.json()["detail"]


def test_unknown_share_is_404_and_bad_id_is_400(client):
    assert client.get("/api/share/doesnotexist1").status_code == 404
    assert client.get("/api/share/bad%2Fid").status_code in (400, 404)


# --------------------------------------------------------------------- demos

def test_demos_index_lists_detected_libraries(client):
    data = client.get("/api/demos").json()
    demos = {d["id"]: d for c in data["categories"] for d in c["demos"]}
    assert demos["json"]["libs"] == ["json"]
    assert demos["vectors-maps"]["libs"] == ["gen_collect"]
    assert demos["hello"]["libs"] == []
    assert "query" in demos["sql-group-join"]["libs"]
    assert {"typed-errors", "named-args", "method-generics", "ref-params", "jagged",
            "primitive-collections", "sql-indexes"} <= set(demos)


def test_demo_detail_has_code_and_libs(client):
    d = client.get("/api/demos/sql-group-join").json()
    assert "Query.Structured" in d["code"] and "query" in d["libs"]
    assert client.get("/api/demos/nope").status_code == 404
    assert client.get("/api/demos/Bad..Id").status_code in (400, 404)


def test_libs_endpoint_lists_only_what_a_program_may_use(client):
    libs = {l["id"]: l for l in client.get("/api/libs").json()["libs"]}
    assert {"json", "xml", "csv", "query", "ml", "cipher"} <= set(libs)
    assert "net" not in libs and "openai" not in libs and "odbc" not in libs
    assert "gen_collect" not in libs  # always linked
    assert "Data.JSON" in libs["json"]["bundles"]
    assert libs["json"]["name"] == "JSON"


def test_health(client):
    body = client.get("/api/health").json()
    assert body["status"] == "ok" and body["version"]
