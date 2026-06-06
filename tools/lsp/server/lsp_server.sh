#!/bin/bash
export OBJECK_INSTALL_DIR="$1"

export OBJECK_LIB_PATH="$OBJECK_INSTALL_DIR/lib"
export PATH="$PATH:$OBJECK_INSTALL_DIR/bin"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec obr "$SCRIPT_DIR/objeck_lsp.obe" "$SCRIPT_DIR/objk_apis.json" pipe
