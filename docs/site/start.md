# Get started

Install, write a program, run it. Or use the [playground](https://playground.objeck.org): no install, libraries chosen from your `use` lines, no network access in the sandbox.

## Install

Current release: **v{{version}}**. All files are on the [releases page](https://github.com/objeck/objeck-lang/releases/latest).

| Platform | Download | Needs |
|---|---|---|
| Windows x64, ARM64 | `.msi` or `.zip` | nothing |
| macOS Apple silicon | `.pkg`, `.tgz` or `.zip` | nothing |
| Ubuntu 22.04+, Debian 12+ x64, ARM64 | `.deb` | nothing |
| Other Linux x64, ARM64 | `.tgz` | glibc 2.28+ |

Ubuntu and Debian:

```bash
curl -LO https://github.com/objeck/objeck-lang/releases/download/v{{version}}/objeck-ubuntu24.04-x64_{{version}}.deb
sudo apt install ./objeck-ubuntu24.04-x64_{{version}}.deb
```

Other Linux:

```bash
tar xzf objeck-linux-x64_{{version}}.tgz
source ./objeck-lang/env.sh       # sets PATH and OBJECK_LIB_PATH
./objeck-lang/install_deps.sh     # optional: SDL2, OpenCV windows, ODBC, LAME
```

The Linux release runs on Ubuntu 22.04 and later, Debian 12 and later, Fedora, Arch and RHEL 8 and its rebuilds. This starts with v2026.11.4; earlier releases run on Ubuntu 24.04 only (see the [README](https://github.com/objeck/objeck-lang#readme)).

## First program

{{example:hello}}

Libraries are named at compile time, for example `obc -src app.obs -lib json,net`. See [Libraries](../web/libraries.html).

## Next

- [Agents](agents.md): declare a tool, serve it over MCP, run an agent
- [Quick start tour](../web/getting_started.html)
- [Guides](../guides/README.md)
- [AI and ML guide](../web/ai_guide.html)
- [Language reference](../FEATURES.md)
- [Editor setup](../guides/language_server.md)
