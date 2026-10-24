from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    # Local dev mode: runs obc/obr directly (no Docker needed)
    local_dev: bool = False
    obc_path: str = ""
    obr_path: str = ""
    objeck_lib_path: str = ""

    # Docker
    sandbox_image: str = "objeck-sandbox:latest"
    max_execution_time: int = 10
    max_code_size: int = 65536
    max_output_size: int = 65536
    # obc linking the Query.Structured closure (query, net, regex, csv, xml, json,
    # misc, cipher) peaks near 60 MB, and 64m left no room for it. Four runs at
    # once is 512 MB, within the 1 GB host.
    container_memory: str = "128m"
    container_cpus: float = 0.5
    container_pids_limit: int = 32
    tmpfs_size: str = "10m"
    host_tmp_dir: str = "/tmp/playground"

    # obc used to run unbounded inside the container; only obr was wrapped in
    # `timeout`. A source that made the compiler spin ran past container.wait().
    compile_timeout: int = 15

    # Total containers in flight across all callers. The rate limit is per-IP and
    # per-process, so it does not bound this.
    max_concurrent_runs: int = 4

    # Version (displayed in frontend header)
    objeck_version: str = "v2026.10.2"

    # Rate limiting
    rate_limit: str = "10/minute"

    # CORS
    allowed_origins: list[str] = [
        "https://www.objeck.org",
        "https://objeck.org",
        "https://playground.objeck.org",
        "http://localhost:8080",
        "http://localhost:3000",
        "http://127.0.0.1:8080",
    ]

    # Share links
    share_db_path: str = "./data/shares.db"

    # Library policy (server-side enforcement). Users never name libraries: the
    # server reads them off the program's `use` lines (services/libmap.py), checks
    # them against these sets, and links their dependencies.
    #
    # allowed: a program may use these. Each is computation in memory with no OS
    # access. cipher, diags and ml also need their native library in the sandbox
    # image (docker/build-sandbox.sh copies it).
    allowed_libs: set[str] = {
        "gen_collect", "json", "xml", "regex", "cipher", "csv",
        "query", "query_ml", "ml", "nlp", "ai", "math", "misc", "diags",
        "concurrent",
    }

    # dependency: linked only because an allowed library was built against it.
    # query and misc need net (and net needs cipher), so net.obl must be in the
    # image. A program that names Web.HTTP is still refused, and the container
    # has no network.
    dependency_libs: set[str] = {"net"}

    # blocked: refused with an explanation. A library in none of the sets is
    # blocked as well, so a new .obl is unavailable until someone decides.
    blocked_libs: set[str] = {
        "net_h2", "net_quic", "net_server", "web_server", "odbc", "query_odbc",
        "query_server", "query_vision", "sdl2", "sdl_game", "sdl_gl", "lame",
        "json_rpc", "json_stream", "rss", "gemini", "ollama", "openai",
        "inference", "models", "onnx", "opencv",
    }

    model_config = {"env_prefix": "PLAYGROUND_"}


settings = Settings()
