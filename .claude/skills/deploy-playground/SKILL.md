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

### 3. Version sanity check

```bash
curl -fsS https://playground.objeck.org/api/health | jq -r '.version'
# must equal v<VERSION>  (the health endpoint returns the 'v' prefix)
```

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

## Note

The release pipeline can do this automatically in CI (a `deploy-playground` job in
`release-publish.yml` using `PLAYGROUND_HOST` / `PLAYGROUND_SSH_KEY` repo secrets,
mirroring the existing `sourceforge-upload` job). When that job is in place,
releasing from any box needs no local playground access — use this skill only for
manual/out-of-band refreshes.
