#!/bin/bash

# ============================================================
#  Objeck LSP - refresh a local language server after a deploy
#
#  Usage: refresh_local_lsp.sh <deploy_dir>
#    deploy_dir  the freshly built tree (core/release/deploy)
#
#  An editor's language server is a self-contained copy in ~/.objeck-lsp
#  (see install.sh), so it keeps running the release it was installed from
#  while the toolchain moves on. A server three releases behind crashed on a
#  program the current libraries compile. This rebuilds the server and runs
#  update_lsp.sh, so a deploy leaves the two in step.
#
#  Linux will not let cp replace a running executable ("Text file busy"), but it
#  will rename one, so the old runtime files are moved aside first: a running
#  server finishes on them (the kernel keeps them until it exits) and the
#  editor's next start loads the new ones. The result is checked against the
#  deploy tree.
#
#  It does nothing where there is no installed server (CI, other machines),
#  and nothing when OBJECK_SKIP_LSP_REFRESH is set. A failure prints a warning
#  and never fails the deploy.
# ============================================================

[ -n "$OBJECK_SKIP_LSP_REFRESH" ] && exit 0
[ -n "$1" ] || exit 0
LSP_HOME="$HOME/.objeck-lsp"
[ -f "$LSP_HOME/objeck_lsp.obe" ] || exit 0
[ -x "$1/bin/obr" ] || exit 0

HERE="$(cd "$(dirname "$0")" && pwd)"
LOG="${TMPDIR:-/tmp}/objeck-lsp-refresh.log"
echo "  refreshing the language server in $LSP_HOME"

if ! bash "$HERE/../server/build_server.sh" > "$LOG" 2>&1; then
	echo "  warning: the language server did not build; it was left as it was (see $LOG)"
	exit 0
fi

# move the runtime aside, then remove it: a running server keeps its open files either way
ASIDE="$LSP_HOME/.refresh-old"
rm -rf "$ASIDE"; mkdir -p "$ASIDE"
for f in "$LSP_HOME"/bin/obr "$LSP_HOME"/bin/obd "$LSP_HOME"/lib/native/*; do
	[ -f "$f" ] && mv -f "$f" "$ASIDE/$(echo "$f" | tr '/' '_')" 2>/dev/null
done

if ! bash "$HERE/update_lsp.sh" "$1" "$HERE/../server" >> "$LOG" 2>&1; then
	echo "  warning: the language server was not updated (see $LOG)"
	exit 0
fi
rm -rf "$ASIDE"

# prove the files that matter arrived
STALE=""
cmp -s "$1/bin/obr" "$LSP_HOME/bin/obr" || STALE="$STALE bin/obr"
cmp -s "$1/lib/lang.obl" "$LSP_HOME/lib/lang.obl" || STALE="$STALE lib/lang.obl"
cmp -s "$HERE/../server/objeck_lsp.obe" "$LSP_HOME/objeck_lsp.obe" || STALE="$STALE objeck_lsp.obe"
if [ -n "$STALE" ]; then
	echo "  warning: not updated:$STALE (see $LOG)"
	echo "           close the editor and run: tools/lsp/scripts/update_lsp.sh \"$1\""
	exit 0
fi

echo "  language server refreshed"
if pgrep -f "$LSP_HOME/bin/obr" >/dev/null 2>&1; then
	echo "  a server is still running on the old files: restart it in the editor"
fi
exit 0
