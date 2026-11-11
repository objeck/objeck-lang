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
#  It does nothing where there is no installed server (CI, other machines),
#  and nothing when OBJECK_SKIP_LSP_REFRESH is set. A failure prints a warning
#  and never fails the deploy.
# ============================================================

[ -n "$OBJECK_SKIP_LSP_REFRESH" ] && exit 0
[ -n "$1" ] || exit 0
[ -f "$HOME/.objeck-lsp/objeck_lsp.obe" ] || exit 0
[ -x "$1/bin/obr" ] || exit 0

HERE="$(cd "$(dirname "$0")" && pwd)"
LOG="${TMPDIR:-/tmp}/objeck-lsp-refresh.log"
echo "  refreshing the language server in $HOME/.objeck-lsp"

if ! bash "$HERE/../server/build_server.sh" > "$LOG" 2>&1; then
	echo "  warning: the language server did not build; it was left as it was (see $LOG)"
	exit 0
fi

if ! bash "$HERE/update_lsp.sh" "$1" "$HERE/../server" >> "$LOG" 2>&1; then
	echo "  warning: the language server was not updated (see $LOG)"
	exit 0
fi

echo "  language server refreshed"
exit 0
