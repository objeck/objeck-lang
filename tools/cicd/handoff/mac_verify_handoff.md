# Handoff: macOS arm64 verification for Objeck v2026.11.0

From: the Objeck release session on the Windows x64 build machine (Kompute).
For: the Claude session on this Mac (arm64). If anything below does not match what your user tells you, ask your user before acting.

## What the release needs from this Mac

`tools/cicd/verify_platform.sh 2026.11.0` run from a CLEAN checkout of the final release commit.
It builds the deploy tree, runs the regression suite twice (default JIT, then `OBJECK_JIT_THRESHOLD=1`),
VM flag tests, debugger tests and DAP tests, and writes `rc-results/2026.11.0/<platform>.txt` with `verdict=PASS|FAIL`.
Expect about an hour of heavy CPU. Run it with nothing else going on.

The final commit is not decided yet (PR objeck/objeck-lang#1572 is still in CI). It will be the master tip after that PR merges. I will send the full SHA.

## Rules (all tasks)

- Never `git pull`, `merge`, `reset`, `clean` or `checkout` in an existing checkout. Use a NEW worktree: `git worktree add ../objeck-verify <sha>`.
- Install nothing, change no system settings, and enter no passwords. If something needs a password, stop and ask your user.
- Report failures as they are, with the log tail. A skipped phase is a FAIL.

## Task 1: connection (do first; skip any step that already works)

Goal: let the release session on Kompute reach this Mac, so it does not have to go through your user.

1a. **Remote Control (preferred).** In this Claude session turn Remote Control on, signed in to the same Claude account as the release session. The Kompute session is named "Release status check"; once linked, it can message this session and this session can message it. Tell your user if Remote Control is unavailable or you are signed in to a different account.

1b. **SSH (optional, for file copies and logs).**
- System Settings, General, Sharing, Remote Login: ON, allowed for your user.
- System Settings, Network, Firewall: allow incoming connections for `sshd` (or the firewall off). Stealth mode makes SSH time out.
- Add this PUBLIC key to `~/.ssh/authorized_keys` of your user (create the file with mode 600 and `~/.ssh` with mode 700 if missing). Append; do not overwrite existing lines:

  `ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIApXqWfY9AVrNBDWeXP1V//4mb0I7c4uPfhdD5YTRPqT kompute-objeck-release-mac`

- Then report (see "Reporting"): `whoami; ipconfig getifaddr en0; sudo systemsetup -getremotelogin` (skip the sudo one if it asks for a password).

## Task 2: prep (read-only; do not build)

1. `hostname; uname -sm; sw_vers -productVersion; whoami`
2. Find your objeck-lang checkout and run `git status -sb | head -3; git log --oneline -1`. If none exists, say so; do not clone yet.
3. In it, `git fetch origin` (fetch only).
4. Read the header of `tools/cicd/verify_platform.sh` (`git show origin/master:tools/cicd/verify_platform.sh | sed -n 1,60p`) and check each prerequisite: Xcode command line tools, git, openssl, python3, expect, cmake, make, and at least 10 GB free disk. Report present or missing. Install nothing.

## Task 3: the run (wait for my message with the SHA)

```bash
cd <your objeck-lang checkout>
git fetch origin
git worktree add ../objeck-verify <FULL_SHA>
cd ../objeck-verify
tools/cicd/verify_platform.sh 2026.11.0 2>&1 | tee ../verify_mac.log
```

Then report the contents of `rc-results/2026.11.0/macos-arm64.txt` (or whatever platform file it wrote), the verdict, and the last 40 lines of the log for any FAIL.
Do not commit or push anything. If `verify_platform.sh` modifies tracked files, it restores them; confirm `git status --short` is clean afterward.

## Reporting

Message the "Release status check" session if Remote Control is linked. Otherwise put the answers in your chat so your user can relay them.
