# Objeck LSP Server

The LSP server implementation for the Objeck language.

## Build

From `tools/lsp/server/`:

**Windows:**
```cmd
build_server.cmd
```

**Linux / macOS:**
```sh
./build_server.sh
```

Both scripts compile the server and place `objeck_lsp.obe` alongside the script.

**Manual build** (requires Objeck in PATH):
```sh
obc -src frameworks.obs,proxy.obs,server.obs,format_code/scanner.obs,format_code/formatter.obs \
    -lib diags,net,json,regex,cipher -dest objeck_lsp.obe
```

## Run

```sh
# STDIO transport (used by most editors)
obr objeck_lsp.obe objk_apis.json stdio

# Named pipe transport (VS Code on Windows)
obr objeck_lsp.obe objk_apis.json pipe

# TCP socket transport
obr objeck_lsp.obe objk_apis.json 6013
```

Required environment variables:
```sh
export OBJECK_LIB_PATH=<objeck_install>/lib
export OBJECK_STDIO=binary   # required for STDIO transport
```

## CI

The server is rebuilt and packaged on every release via `.github/workflows/release-build.yml` (`build-lsp` job). The VS Code extension is published to the Marketplace automatically via the `publish-vscode` job.
