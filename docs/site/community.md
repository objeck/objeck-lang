# Community

## Talk

- [Discussions](https://github.com/objeck/objeck-lang/discussions): questions and ideas
- [Issues](https://github.com/objeck/objeck-lang/issues): bugs and requests (templates provided; a small failing program helps most)
- [Rosetta Code](https://rosettacode.org/wiki/Category:Objeck): add a solution

Security problems: do not open an issue; follow [SECURITY.md](../../SECURITY.md).

## Contribute

[CONTRIBUTING.md](../../CONTRIBUTING.md) has the detail. In short: branch off `master`, keep the change focused, say what changed and which platforms you tested, never commit credentials.

Start with the [architecture map](../ARCHITECTURE_MAP.md) and [adding an intrinsic](../ADDING_AN_INTRINSIC.md).

## Build and test

- CI is the authoritative recipe: [`ci-build.yml`](../../.github/workflows/ci-build.yml) and [`setup-objeck-deps`](../../.github/actions/setup-objeck-deps/action.yml).
- Regression suite: [`programs/regression`](../../programs/regression/README.md). `./run_regression.sh x64` (or `arm64`) on Linux and macOS, `run_regression.cmd x64` on Windows.
- New library: add one line to `LIBS` in `tools/docs/guides_to_html.py`; the generator fails until you do.

## Conduct

[Contributor Covenant](../../CODE_OF_CONDUCT.md).
