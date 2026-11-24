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
obc -src frameworks.obs,proxy.obs,server.obs,inline_completion.obs,format_code/scanner.obs,format_code/formatter.obs \
    -lib diags,net,json,regex,cipher,net_server,misc,ollama,anthropic,openai,gemini -dest objeck_lsp.obe
```

## Run

```sh
# STDIO transport (used by most editors)
obr objeck_lsp.obe objk_apis.json stdio

# Named pipe transport
obr objeck_lsp.obe objk_apis.json pipe

# TCP socket transport
obr objeck_lsp.obe objk_apis.json 6013
```

The TCP transport listens on `127.0.0.1` only (IPv4 loopback, so a client must connect to `127.0.0.1`, not `localhost` where that resolves to `::1`) and serves at most 8 connections at once; a ninth is closed immediately. A message whose `Content-Length` is not a plain number, or is over 64 MB, ends that connection.

If `objk_apis.json` is missing or unreadable the server still runs, without hover and completion documentation, and tells the editor with a `window/showMessage` error at `initialized`. It also warns when the server, the running Objeck, or the index's `version` stamp are not the same release.

Required environment variables:
```sh
export OBJECK_LIB_PATH=<objeck_install>/lib
export OBJECK_STDIO=binary   # required for STDIO transport
```

## CI

The server is rebuilt and packaged on every release via `.github/workflows/release-build.yml` (`build-lsp` job). The same job packages the VS Code extension with `vsce package` and ships the `.vsix` inside `objeck-lsp_<version>.zip`; it is not published to the VS Code Marketplace (see `tools/lsp/clients/vscode/README.md` for installing it). On every push to `master` and every pull request against it, the `tools` job in `.github/workflows/ci-build.yml` builds the server, runs the formatter and LSP regression tests, and compiles and lints the VS Code extension.
