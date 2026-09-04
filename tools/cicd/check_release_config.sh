#!/usr/bin/env bash
#
# check_release_config.sh
#
# Pre-flight: is the release pipeline actually CONFIGURED to do what it claims?
#
# WHY THIS EXISTS
# ---------------
# Every other pre-flight gate checks the repository -- version consistency, tag
# state, docs, api.zip. None of them checked whether the pipeline could perform
# the steps it advertises. It could not, and had not for many releases.
#
# v2026.8.4 published with all of these reporting completed/success:
#
#     Upload to Sourceforge        -> uploaded nothing   (no SOURCEFORGE_SSH_KEY)
#     Publish VS Code Extension    -> published nothing  (no VSCE_PAT)
#     Deploy playground            -> deployed nothing   (no PLAYGROUND_SSH_KEY)
#     Deploy API Documentation     -> deployed nothing   (no OBJECK_ORG_SSH_KEY)
#
# The playground was left serving a three-month-old engine and objeck.org a
# release-old set of API docs; both were repaired by hand after the fact. The
# jobs now fail instead of skipping, but a failure DURING a release is still the
# expensive place to learn this. Learn it before the tag.
#
# Run it before tagging. Exit 0 = every advertised step can run, or is declared
# manual. Exit 1 = a gap that would silently or noisily cost you a release step.
set -uo pipefail

REPO="${REPO:-objeck/objeck-lang}"
WF_DIR=".github/workflows"
FAIL=0
note() { echo "  $*"; }
bad()  { echo "  GAP: $*"; FAIL=1; }

echo "=============================================================="
echo " release configuration pre-flight ($REPO)"
echo "=============================================================="

# Which secrets do the release workflows actually reference?
# [A-Z0-9_]+, not [A-Z_]+: without the digits this truncates APPLE_CERTIFICATE_BASE64
# to APPLE_CERTIFICATE_BASE and reports a set secret as a gap -- a checker that
# fails like the defect it hunts, which is the whole family of bug this file is
# here to end.
REFERENCED=$(grep -ohE 'secrets\.[A-Z0-9_]+' \
  "$WF_DIR/release-build.yml" "$WF_DIR/release-publish.yml" 2>/dev/null \
  | sed 's/secrets\.//' | sort -u | grep -v '^GITHUB_TOKEN$')

# Which are set? (names only; values are never readable, by design)
CONFIGURED=$(gh secret list -R "$REPO" --json name --jq '.[].name' 2>/dev/null | sort -u)
if [ -z "$CONFIGURED" ]; then
  echo "  cannot read the secret list for $REPO (auth/permissions?) -- aborting"
  exit 1
fi

# Steps declared manual, via the repository VARIABLE the workflows honour.
MANUAL=$(gh variable list -R "$REPO" --json name,value \
           --jq '.[] | select(.name=="RELEASE_MANUAL_STEPS") | .value' 2>/dev/null)
echo
echo "RELEASE_MANUAL_STEPS = '${MANUAL:-<unset>}'"

# secret -> the release step it powers, and the RELEASE_MANUAL_STEPS token that
# legitimately excuses its absence.
step_for() {
  case "$1" in
    SOURCEFORGE_*)  echo "Sourceforge upload|sourceforge" ;;
    PLAYGROUND_*)   echo "playground deploy|playground" ;;
    OBJECK_ORG_*)   echo "objeck.org API docs deploy|docs" ;;
    VSCE_PAT)       echo "VS Code Marketplace publish|vscode" ;;
    APPLE_*|CODESIGN_*|KEYCHAIN_*) echo "macOS signing/notarization|" ;;
    *)              echo "unknown step|" ;;
  esac
}

echo
echo "[secrets referenced by the release workflows]"
for s in $REFERENCED; do
  info=$(step_for "$s"); desc="${info%%|*}"; token="${info##*|}"
  if printf '%s\n' "$CONFIGURED" | grep -qx "$s"; then
    note "set        $s  ($desc)"
  elif [ -n "$token" ] && printf '%s' "$MANUAL" | grep -q "$token"; then
    # Declaring a step manual only helps if a workflow actually READS the
    # declaration. It did not for 'docs': this gate printed "manual ... declared
    # in RELEASE_MANUAL_STEPS" and exited 0, and the v2026.9.0 publish then failed
    # in Deploy API Documentation, because that job had a hard-fail on the missing
    # secret and no contains(vars.RELEASE_MANUAL_STEPS, 'docs') branch beside it.
    # A declaration nothing honours is worse than none: it reads as handled.
    if grep -rq "contains(vars.RELEASE_MANUAL_STEPS, '$token')" "$WF_DIR"; then
      note "manual     $s  ($desc -- declared in RELEASE_MANUAL_STEPS)"
    else
      bad "INERT  $s  ($desc -- '$token' is declared but NO workflow honours it)"
      echo "         the step will still fail; add a guarded skip beside its"
      echo "         hard-fail:  if: <secret> == '' && contains(vars.RELEASE_MANUAL_STEPS, '$token')"
    fi
  else
    bad "UNSET  $s  ($desc)"
    if [ -n "$token" ]; then
      echo "         fix:  gh secret set $s < <keyfile>"
      echo "         or:   gh variable set RELEASE_MANUAL_STEPS --body '<existing>,$token'"
    else
      echo "         fix:  gh secret set $s"
    fi
  fi
done

# A step declared manual is a REMINDER, not a free pass: it still has to happen.
if [ -n "$MANUAL" ]; then
  echo
  echo "[manual steps -- CI will not do these, you must]"
  IFS=',' read -ra STEPS <<< "$MANUAL"
  for t in "${STEPS[@]}"; do
    case "$(echo "$t" | tr -d ' ')" in
      sourceforge) note "sourceforge  -> upload the release assets to Sourceforge by hand" ;;
      vscode)      note "vscode       -> publish the .vsix from the build run's artifacts" ;;
      playground)  note "playground   -> ssh <host> 'bash /opt/playground/repo/programs/web-playground/deploy/update.sh <VERSION>'" ;;
      docs)        note "docs         -> rsync api/ to /var/www/objeck.org/api/v<VERSION>/ and repoint 'latest'" ;;
      "")          ;;
      *)           bad "$t (unrecognised token -- no workflow honours it)" ;;
    esac
  done
fi

echo
echo "=============================================================="
if [ "$FAIL" -eq 0 ]; then
  echo " release configuration OK -- every advertised step can run or is declared manual"
else
  echo " *** CONFIGURATION GAPS -- fix or declare before tagging ***"
fi
echo "=============================================================="
exit "$FAIL"
