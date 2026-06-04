# Security Policy

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

Instead, use GitHub's private vulnerability reporting:

1. Go to the [Security tab](https://github.com/objeck/objeck-lang/security/advisories) of this repository.
2. Click **Report a vulnerability** and provide as much detail as you can:
   - The affected component (compiler, VM/JIT, a bundled library, tooling).
   - The platform and architecture (e.g. Windows x64, Linux arm64).
   - Steps to reproduce, and the impact you observed.

If you cannot use the security advisory flow, email the maintainer at `objeck@gmail.com`
with the subject line `SECURITY: <short summary>`.

We aim to acknowledge reports within a few days and will keep you updated as we
investigate and prepare a fix.

## Supported Versions

Security fixes target the latest released version. Please upgrade to the most
recent release before reporting, where practical.

## Secrets and Credentials

Never commit API keys, tokens, passwords, or other credentials. The framework
examples read keys from local files (e.g. `*_api_key.dat`) that are excluded by
`.gitignore` — keep your keys there and out of version control. If a secret is
ever committed, **rotate it immediately**: rewriting git history does not
un-expose a key that has already been pushed.
