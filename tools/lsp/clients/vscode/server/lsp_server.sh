#!/bin/bash
# Starts the server over stdio, as the extension does (it runs obr directly and
# no longer calls this script); kept for starting it by hand.
#
# The install directory arrives in OBJECK_INSTALL_DIR; $1 is kept as a fallback
# for older clients that still pass it on the command line.
export OBJECK_INSTALL_DIR="${OBJECK_INSTALL_DIR:-$1}"

export OBJECK_LIB_PATH="$OBJECK_INSTALL_DIR/lib"
export PATH="$PATH:$OBJECK_INSTALL_DIR/bin"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec obr "$SCRIPT_DIR/objeck_lsp.obe" "$SCRIPT_DIR/objk_apis.json" stdio
