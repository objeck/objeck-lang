---
name: deploy-playground
description: Deploy/refresh the Objeck web playground (playground.objeck.org) on its VPS over SSH — git pull, rebuild the sandbox Docker image, restart the service, health-check. Host and SSH access are held out-of-band; this skill contains no secrets.
allowed-tools: Read Bash
argument-hint: "[version] e.g. 2026.6.1  (optional — for the version sanity check)"
---

Deploy or refresh the DigitalOcean-hosted Objeck playground at
`playground.objeck.org`. Use after a release, or any time the server needs to
pull the latest `master`.

## Prerequisites — held out-of-band (NEVER commit or paste secrets)

- **`$PLAYGROUND_HOST`** — the deploy target (hostname or IP). It is intentionally
  not in the repo. Export it before running:
  ```bash
  export PLAYGROUND_HOST=playground.objeck.org   # or the VPS IP if DNS is down
  ```
- **SSH access** — this box's *public* key must be authorized on the VPS as root.
  If it isn't yet, authorize it once (no private key ever leaves the box):
  ```bash
  # ON THIS BOX — print the PUBLIC key (safe to share); never the private one:
  cat ~/.ssh/id_ed25519.pub      # or ~/.ssh/id_rsa.pub

  # ON THE VPS — append that one line to root's authorized_keys. Optionally lock
  # the key to just the deploy so it can do nothing else:
  #   command="bash /opt/playground/repo/programs/web-playground/deploy/update.sh",no-pty,no-port-forwarding <pubkey>
  ```

**If `$PLAYGROUND_HOST` is unset or SSH fails: STOP and tell the user.** Do not
guess a host, do not read or print any key material, do not paste secrets into
output.

## Steps

### 1. Deploy

```bash
ssh -o StrictHostKeyChecking=accept-new root@$PLAYGROUND_HOST \
  'bash /opt/playground/repo/programs/web-playground/deploy/update.sh'
```

`update.sh` does: `git pull origin master`, update the Python venv, rebuild the
sandbox Docker image, restart the systemd `playground` service, and run a
`curl -sf http://localhost:8000/api/health` check.

### 2. Recover a stuck git tree (only if `update.sh`'s pull fails)

The server can accumulate local modifications (`.obl` regenerated in place,
`config.py` touched) and untracked files (artifacts later committed to master):

```bash
ssh root@$PLAYGROUND_HOST 'cd /opt/playground/repo && \
  chmod -R u+w . && \
  git stash && \
  git clean -fd && \
  git pull origin master && \
  git stash pop'
```

Then re-run step 1. If it still fails, report the SSH output verbatim and stop.

### 3. Version sanity check -- BOTH of them

`/api/health` is a **label**, not evidence. It reports a hand-maintained constant
in `backend/app/config.py`, which `git pull` updates on its own -- so it flips to
the new version whether or not a single binary changed. It read `v2026.8.4` over
a June engine for months, and it read `v2026.9.0` over a `2026.8.4` engine within
seconds of the v2026.9.0 deploy.

Always run both, and treat only the second as the answer:

```bash
# the label
curl -fsS https://playground.objeck.org/api/health | jq -r '.version'

# the engine -- what actually executes
curl -fsS -X POST https://playground.objeck.org/api/run   -H 'Content-Type: application/json'   -d '{"code":"class M { function : Main(a:String[])~Nil { System.Runtime->GetVersion()->PrintLine(); } }"}'
```

If they disagree, the deploy did not install a toolchain. Do not report success.

### 3b. Two failure modes that look like success

**`update.sh` updates itself.** It lives in the repo it pulls, so a run that
pulls a *new* `update.sh` keeps executing the old one -- and exits 0. At
v2026.9.0 the first deploy printed `=== Update complete ===`, moved the health
label, and never touched the toolchain; the identical command run a second time
downloaded and installed the tarball. Fixed by re-exec'ing after the pull, but if
you are ever on a server whose script predates that fix, **run the deploy twice**
and compare -- a second run that does real work means the first one did not.

A tell: the current script ends `=== Update complete (Objeck <VERSION>) ===`.
A bare `=== Update complete ===` means an older script ran.

**Do not pipe the ssh deploy through `head`.** `head` closes the pipe and can
SIGPIPE the ssh mid-`docker build`, leaving a half-built image behind an
apparently normal exit. Redirect to a file and read that, or use `tail` on the
saved output:

```bash
ssh root@$PLAYGROUND_HOST 'bash /opt/playground/.../update.sh <VERSION>' 2>&1 | tee /tmp/deploy.log
```

The old single-shot health check (`sleep 3` then one curl) also cried wolf on
every deploy, because uvicorn's workers need longer than 3s to bind. It polls for
20s now; a failure there is real.

> **If the version is stale after a successful deploy**, the reported string comes
> from `programs/web-playground/backend/app/config.py` (`objeck_version`), a
> hand-maintained constant — not from `version.h`. Bump it in the repo, commit,
> push, and re-deploy. Watch for a **stale local pin on the server**: the VPS may
> carry a hand-edited `config.py` (older version) that a `git stash pop` will
> conflict against — resolve with `git checkout HEAD -- <config.py>` to take the
> committed value, then restart `playground.service`. A bad resolution leaves
> `<<<<<<<` conflict markers in the file and uvicorn dies with `SyntaxError`
> (health stays `502`).

If `playground.objeck.org` is unreachable, try the host directly:
`curl -fsS https://$PLAYGROUND_HOST/api/health`.

### 4. Report

- **Deployed**: `$PLAYGROUND_HOST` (do NOT print the resolved IP), service restarted
- **Health**: OK / failure detail
- **Version**: `v<VERSION>` (and whether it matches the expected release)

## Security

- No host, key, or credential is stored in this skill. `$PLAYGROUND_HOST` is an
  environment variable; SSH authenticates with this box's own already-authorized
  key. Never copy a private key into a file in the repo, into output, or into a
  chat — a private key in a transcript is a leaked root credential.
- Prefer a **dedicated** deploy key with a forced `command=` over your personal
  key, so a compromise can only run the deploy.

## Sourceforge is not part of this

Sourceforge mirrors the GitHub release through a **GitHub webhook** and updates
itself when a release is published. It is not an SSH deploy, it is not this
skill's job, and it needs no manual upload -- earlier notes calling it a manual
post-release step are stale. Nothing to run; confirm the files appeared if you
want, but do not wait on it.

## Note

The release pipeline can do this automatically in CI (a `deploy-playground` job in
`release-publish.yml` using `PLAYGROUND_HOST` / `PLAYGROUND_SSH_KEY` repo secrets,
mirroring the existing `sourceforge-upload` job). When that job is in place,
releasing from any box needs no local playground access — use this skill only for
manual/out-of-band refreshes.
