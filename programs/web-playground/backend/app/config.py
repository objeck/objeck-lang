from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    # Docker
    sandbox_image: str = "objeck-sandbox:latest"
    max_execution_time: int = 10
    max_code_size: int = 65536
    max_output_size: int = 65536
    container_memory: str = "64m"
    container_cpus: float = 0.5
    container_pids_limit: int = 32
    tmpfs_size: str = "10m"
    host_tmp_dir: str = "/tmp/playground"

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

    # Allowed libraries (server-side enforcement)
    allowed_libs: set[str] = {
        "collect", "gen_collect", "json", "xml", "regex",
        "cipher", "csv", "query", "ml", "nlp", "misc", "diags",
    }

    # Blocked libraries (explicit deny-list)
    blocked_libs: set[str] = {
        "net", "net_server", "odbc", "sdl2", "sdl_game",
        "json_rpc", "json_stream", "rss",
        "gemini", "ollama", "openai", "onnx", "opencv", "lame",
    }

    model_config = {"env_prefix": "PLAYGROUND_"}


settings = Settings()
