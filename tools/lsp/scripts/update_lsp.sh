#!/bin/bash
set -e

# ============================================================
#  Objeck LSP - Update Script (Linux / macOS)
#
#  Refreshes the self-contained LSP deployment at ~/.objeck-lsp/
#  with updated runtime and server files.
#
#  Usage: ./update_lsp.sh <objeck_install_dir> [lsp_server_dir]
#    objeck_install_dir  Path to Objeck installation
#    lsp_server_dir      Path to LSP server files (default: ../server)
# ============================================================

if [ -z "$1" ]; then
    echo ""
    echo "  Usage: ./update_lsp.sh <objeck_install_dir> [lsp_server_dir]"
    echo ""
    echo "  Arguments:"
    echo "    objeck_install_dir  Path to Objeck installation"
    echo "    lsp_server_dir      Path to LSP server files (default: ../server)"
    echo ""
    echo "  Examples:"
    echo "    User install:    ./update_lsp.sh ~/objeck"
    echo "    System install:  ./update_lsp.sh /usr/local/objeck"
    echo ""
    exit 1
fi

OBJECK_DIR="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVER_DIR="${2:-$(dirname "$SCRIPT_DIR")/server}"
LSP_HOME="$HOME/.objeck-lsp"

# validate inputs
if [ ! -f "$OBJECK_DIR/bin/obr" ]; then
    echo "ERROR: Cannot find obr in $OBJECK_DIR/bin/"
    exit 1
fi

if [ ! -f "$SERVER_DIR/objeck_lsp.obe" ]; then
    echo "ERROR: Cannot find objeck_lsp.obe in $SERVER_DIR/"
    exit 1
fi

if [ ! -d "$LSP_HOME" ]; then
    echo "ERROR: $LSP_HOME does not exist. Run install.sh first."
    exit 1
fi

echo ""
echo "========================================"
echo " Objeck LSP - Update"
echo "========================================"
echo ""

# update runtime
echo "[1/3] Updating runtime from $OBJECK_DIR..."
cp "$OBJECK_DIR/bin/obr" "$LSP_HOME/bin/"
chmod +x "$LSP_HOME/bin/obr"
[ -f "$OBJECK_DIR/bin/obd" ] && cp "$OBJECK_DIR/bin/obd" "$LSP_HOME/bin/" && chmod +x "$LSP_HOME/bin/obd"
cp "$OBJECK_DIR"/lib/*.obl "$LSP_HOME/lib/"
cp "$OBJECK_DIR"/lib/*.pem "$LSP_HOME/lib/" 2>/dev/null
cp "$OBJECK_DIR"/lib/*.ini "$LSP_HOME/lib/" 2>/dev/null
echo "   Done."

# update native libraries (includes libobjk_diags which has the compiler)
echo "[2/3] Updating native libraries from $OBJECK_DIR..."
if [ -d "$OBJECK_DIR/lib/native" ]; then
    mkdir -p "$LSP_HOME/lib/native"
    cp "$OBJECK_DIR"/lib/native/*.so "$LSP_HOME/lib/native/" 2>/dev/null
    cp "$OBJECK_DIR"/lib/native/*.dylib "$LSP_HOME/lib/native/" 2>/dev/null
    echo "   Done."
else
    echo "   Skipped (no native directory found)."
fi

# update server
echo "[3/3] Updating LSP server from $SERVER_DIR..."
cp "$SERVER_DIR/objeck_lsp.obe" "$LSP_HOME/"
cp "$SERVER_DIR/objk_apis.json" "$LSP_HOME/"
echo "   Done."

echo ""
echo "========================================"
echo " Update complete"
echo "========================================"
echo ""
echo " LSP home: $LSP_HOME"
echo ""
