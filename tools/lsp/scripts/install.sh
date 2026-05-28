#!/bin/bash
set -e

# ============================================================
#  Objeck LSP - Install Script (Linux / macOS)
#
#  Usage: ./install.sh <objeck_install_dir> <editor>
#    editor: vscode | sublime | neovim | emacs | helix | vim | all
#
#  Run from the extracted release directory (objeck-lsp-VERSION/)
# ============================================================

usage() {
    echo ""
    echo "  Usage: ./install.sh <objeck_install_dir> <editor>"
    echo ""
    echo "  Arguments:"
    echo "    objeck_install_dir  Path to Objeck installation (e.g. /usr/local/objeck)"
    echo "    editor              One of: vscode, sublime, neovim, emacs, helix, vim, all"
    echo ""
    echo "  Examples:"
    echo "    User install:    ./install.sh ~/objeck vscode"
    echo "    System install:  ./install.sh /usr/local/objeck all"
    echo ""
    exit 1
}

if [ -z "$1" ] || [ -z "$2" ]; then
    usage
fi

OBJECK_DIR="$1"
EDITOR="$2"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RELEASE_DIR="$(dirname "$SCRIPT_DIR")"
LSP_HOME="$HOME/.objeck-lsp"

# validate Objeck install
if [ ! -f "$OBJECK_DIR/bin/obr" ]; then
    echo "ERROR: Cannot find obr in $OBJECK_DIR/bin/"
    echo "Make sure the Objeck install directory is correct."
    exit 1
fi

# validate release directory
if [ ! -f "$RELEASE_DIR/server/objeck_lsp.obe" ]; then
    echo "ERROR: Cannot find server/objeck_lsp.obe in release directory."
    echo "Run this script from the extracted release directory."
    exit 1
fi

echo ""
echo "========================================"
echo " Objeck LSP - Install"
echo "========================================"
echo ""
echo " Objeck:     $OBJECK_DIR"
echo " LSP home:   $LSP_HOME"
echo " Editor:     $EDITOR"
echo ""

# --- create self-contained deployment ---
setup_lsp_home() {
    echo "[1] Creating self-contained deployment at $LSP_HOME..."
    mkdir -p "$LSP_HOME/bin" "$LSP_HOME/lib"

    # copy runtime
    cp "$OBJECK_DIR/bin/obr" "$LSP_HOME/bin/"
    chmod +x "$LSP_HOME/bin/obr"
    if [ -f "$OBJECK_DIR/bin/obd" ]; then
        cp "$OBJECK_DIR/bin/obd" "$LSP_HOME/bin/"
        chmod +x "$LSP_HOME/bin/obd"
    fi
    # copy lib recursively (includes native/ subdir)
    cp -r "$OBJECK_DIR"/lib/* "$LSP_HOME/lib/"

    # copy LSP server files
    cp "$RELEASE_DIR/server/objeck_lsp.obe" "$LSP_HOME/"
    cp "$RELEASE_DIR/server/objk_apis.json" "$LSP_HOME/"
    cp "$RELEASE_DIR/server/lsp_server.cmd" "$LSP_HOME/" 2>/dev/null || true
    cp "$RELEASE_DIR/server/lsp_server.sh"  "$LSP_HOME/" 2>/dev/null || true
    chmod +x "$LSP_HOME/lsp_server.sh" 2>/dev/null || true

    echo "   Done: $LSP_HOME"
}

# --- VS Code ---
install_vscode() {
    echo ""
    echo "[VS Code] Installing extension..."

    VSIX_FILE=$(find "$RELEASE_DIR/clients/vscode" -name "*.vsix" 2>/dev/null | head -1)
    if [ -z "$VSIX_FILE" ]; then
        echo "   WARNING: No .vsix file found in clients/vscode/"
        return
    fi

    if ! command -v code &>/dev/null; then
        echo "   WARNING: 'code' command not found. Install the extension manually:"
        echo "   Open VS Code, Extensions panel, drag-and-drop $VSIX_FILE"
        return
    fi

    code --install-extension "$VSIX_FILE" --force
    echo "   Extension installed."

    # Sync rebuilt server files into deployed extension to avoid version mismatch
    for ext_dir in "$HOME/.vscode/extensions"/objeck-lsp.objeck-lsp-*; do
        if [ -d "$ext_dir/server" ]; then
            cp "$RELEASE_DIR/server/objeck_lsp.obe"               "$ext_dir/server/objeck_lsp.obe"
            cp "$RELEASE_DIR/server/objk_apis.json"               "$ext_dir/server/objk_apis.json"
            cp "$RELEASE_DIR/clients/vscode/server/lsp_server.cmd" "$ext_dir/server/lsp_server.cmd"
            cp "$RELEASE_DIR/clients/vscode/server/lsp_server.sh"  "$ext_dir/server/lsp_server.sh"
            chmod +x "$ext_dir/server/lsp_server.sh"
            echo "   Server synchronized: $ext_dir/server"
        fi
    done

    echo ""
    echo "   NOTE: Set the Objeck install path in VS Code settings:"
    echo "     \"objk.posix.install.dir\": \"$OBJECK_DIR\""
}

# --- Sublime Text ---
install_sublime() {
    echo ""
    echo "[Sublime Text] Installing..."

    # detect platform-specific Sublime path
    if [ "$(uname)" = "Darwin" ]; then
        SUBLIME_PKG="$HOME/Library/Application Support/Sublime Text/Packages"
    else
        SUBLIME_PKG="$HOME/.config/sublime-text/Packages"
    fi
    SUBLIME_OBJECK="$SUBLIME_PKG/Objeck"

    # install syntax highlighting
    mkdir -p "$SUBLIME_OBJECK"
    if [ -f "$RELEASE_DIR/clients/sublime/objeck.sublime-syntax" ]; then
        cp "$RELEASE_DIR/clients/sublime/objeck.sublime-syntax" "$SUBLIME_OBJECK/"
        echo "   Syntax file installed."
    fi

    # opt the package into Python 3.8 (required by the Debugger package)
    if [ -f "$RELEASE_DIR/clients/sublime/.python-version" ]; then
        cp "$RELEASE_DIR/clients/sublime/.python-version" "$SUBLIME_OBJECK/"
    fi

    # install LSP package if not present
    SUBLIME_LSP="$SUBLIME_PKG/LSP"
    if [ ! -d "$SUBLIME_LSP" ]; then
        if command -v git &>/dev/null; then
            echo "   Cloning LSP package..."
            git clone --depth 1 https://github.com/sublimelsp/LSP.git "$SUBLIME_LSP" >/dev/null 2>&1
        else
            echo "   NOTE: Install the LSP package via Package Control."
        fi
    fi

    # write LSP settings
    LSP_SETTINGS="$SUBLIME_PKG/User/LSP.sublime-settings"
    mkdir -p "$(dirname "$LSP_SETTINGS")"
    echo "   Writing LSP settings..."
    cat > "$LSP_SETTINGS" << EOLSP
{
	"clients": {
		"objeck": {
			"enabled": true,
			"command": [
				"$LSP_HOME/bin/obr",
				"$LSP_HOME/objeck_lsp.obe",
				"$LSP_HOME/objk_apis.json",
				"stdio"
			],
			"env": {
				"OBJECK_LIB_PATH": "$LSP_HOME/lib",
				"OBJECK_STDIO": "binary"
			},
			"selector": "source.objeck-obs"
		}
	}
}
EOLSP
    echo "   LSP settings written to $LSP_SETTINGS"

    # install DAP adapter (for the "Debugger" Sublime package)
    if [ -f "$RELEASE_DIR/clients/sublime/dap/objeck_dap_adapter.py" ]; then
        cp "$RELEASE_DIR/clients/sublime/dap/objeck_dap_adapter.py" "$SUBLIME_OBJECK/"
        echo "   DAP adapter installed: $SUBLIME_OBJECK/objeck_dap_adapter.py"

        # write Objeck.sublime-settings with obd path so the adapter can
        # find the debugger binary on launch
        OBJK_SETTINGS="$SUBLIME_PKG/User/Objeck.sublime-settings"
        cat > "$OBJK_SETTINGS" << EOOBJK
{
    "obd_path": "$OBJECK_DIR/bin/obd",
    "objeck_lib_path": "$OBJECK_DIR/lib"
}
EOOBJK
        echo "   Objeck settings written to $OBJK_SETTINGS"
    fi

    echo ""
    echo "   Next steps:"
    echo "     1. Tools > LSP > Enable Language Server Globally > objeck"
    echo "     2. (For debugging) install the \"Debugger\" package via Package Control"
    echo "     3. See clients/sublime/dap/objeck.sublime-project.example"
}

# --- Neovim ---
install_neovim() {
    echo ""
    echo "[Neovim] Installing..."

    NVIM_LSP="$HOME/.config/nvim/lsp"
    NVIM_SYNTAX="$HOME/.config/nvim/syntax"
    mkdir -p "$NVIM_LSP" "$NVIM_SYNTAX"

    if [ -f "$RELEASE_DIR/clients/neovim/objeck.lua" ]; then
        sed "s|<objeck_server_path>|$LSP_HOME|g" \
            "$RELEASE_DIR/clients/neovim/objeck.lua" > "$NVIM_LSP/objeck.lua"
        echo "   Installed: $NVIM_LSP/objeck.lua"
    else
        echo "   WARNING: clients/neovim/objeck.lua not found in release."
    fi

    if [ -f "$RELEASE_DIR/clients/neovim/objeck.vim" ]; then
        cp "$RELEASE_DIR/clients/neovim/objeck.vim" "$NVIM_SYNTAX/"
        echo "   Installed: $NVIM_SYNTAX/objeck.vim"
    fi

    echo ""
    echo "   Add to your init.lua:"
    echo "     vim.filetype.add({ extension = { obs = 'objeck' } })"
    echo "     vim.lsp.enable('objeck')"
}

# --- Helix ---
install_helix() {
    echo ""
    echo "[Helix] Installing..."

    HELIX_DIR="$HOME/.config/helix"
    mkdir -p "$HELIX_DIR"
    HELIX_LANGS="$HELIX_DIR/languages.toml"

    if [ ! -f "$RELEASE_DIR/clients/helix/languages.toml" ]; then
        echo "   WARNING: clients/helix/languages.toml not found in release."
        return
    fi

    if [ -f "$HELIX_LANGS" ]; then
        echo "   NOTE: $HELIX_LANGS already exists."
        echo "   Merge contents of clients/helix/languages.toml manually,"
        echo "   replacing <objeck_server_path> with $LSP_HOME"
    else
        sed "s|<objeck_server_path>|$LSP_HOME|g" \
            "$RELEASE_DIR/clients/helix/languages.toml" > "$HELIX_LANGS"
        echo "   Installed: $HELIX_LANGS"
    fi
}

# --- Emacs ---
install_emacs() {
    echo ""
    echo "[Emacs] Installing..."

    EMACS_LISP="$HOME/.emacs.d/lisp"
    mkdir -p "$EMACS_LISP"

    if [ -f "$RELEASE_DIR/clients/emacs/objeck-mode.el" ]; then
        sed "s|<objeck_server_path>|$LSP_HOME|g" \
            "$RELEASE_DIR/clients/emacs/objeck-mode.el" > "$EMACS_LISP/objeck-mode.el"
        echo "   Installed: $EMACS_LISP/objeck-mode.el"
    else
        echo "   WARNING: clients/emacs/objeck-mode.el not found in release."
    fi

    echo ""
    echo "   Add to your init.el:"
    echo "     (add-to-list 'load-path \"~/.emacs.d/lisp\")"
    echo "     (require 'objeck-mode)"
}

# --- Vim / gvim ---
install_vim() {
    echo ""
    echo "[Vim/gvim] Installing..."

    # POSIX vim runtime dir is ~/.vim
    VIM_DIR="$HOME/.vim"
    mkdir -p "$VIM_DIR/syntax" "$VIM_DIR/ftdetect" \
             "$VIM_DIR/plugin" "$VIM_DIR/pack/objeck/start"

    # Locate the syntax files. Prefer the bundled clients/vim/ copy if
    # the release ships it, otherwise fall back to the objeck-lang docs
    # tree if the user is running from a source checkout.
    SYNTAX_DIR=""
    if [ -f "$RELEASE_DIR/clients/vim/syntax/objeck.vim" ]; then
        SYNTAX_DIR="$RELEASE_DIR/clients/vim"
    elif [ -f "$RELEASE_DIR/../objeck-lang/docs/syntax/vim/objeck.vim" ]; then
        SYNTAX_DIR="$RELEASE_DIR/../objeck-lang/docs/syntax/vim"
    fi

    if [ -n "$SYNTAX_DIR" ]; then
        cp "$SYNTAX_DIR/objeck.vim" "$VIM_DIR/syntax/" 2>/dev/null || true
        cp "$SYNTAX_DIR/ftdetect/objeck.vim" "$VIM_DIR/ftdetect/" 2>/dev/null || true
        echo "   Syntax + ftdetect installed."
    else
        echo "   WARNING: vim syntax files not found in release."
    fi

    # Clone yegappan/lsp and vimspector if not already present
    LSP_PLUGIN="$VIM_DIR/pack/objeck/start/lsp"
    if [ ! -d "$LSP_PLUGIN" ]; then
        if command -v git &>/dev/null; then
            git clone --depth 1 https://github.com/yegappan/lsp "$LSP_PLUGIN" >/dev/null 2>&1
            echo "   Installed: yegappan/lsp"
        else
            echo "   NOTE: git not found, skipping yegappan/lsp clone"
        fi
    fi

    VS_PLUGIN="$VIM_DIR/pack/objeck/start/vimspector"
    if [ ! -d "$VS_PLUGIN" ]; then
        if command -v git &>/dev/null; then
            git clone --depth 1 https://github.com/puremourning/vimspector "$VS_PLUGIN" >/dev/null 2>&1
            echo "   Installed: puremourning/vimspector"
        else
            echo "   NOTE: git not found, skipping vimspector clone"
        fi
    fi

    # Drop the Objeck vim config into the auto-loaded plugin dir.
    if [ -f "$RELEASE_DIR/clients/vim/objeck_vimrc.vim" ]; then
        cp "$RELEASE_DIR/clients/vim/objeck_vimrc.vim" "$VIM_DIR/plugin/objeck.vim"
        echo "   Installed: $VIM_DIR/plugin/objeck.vim"
    fi

    # Install the vimspector adapter file. Use ~/.vimspector.json (the
    # standard path vimspector reads on startup).
    if [ -f "$RELEASE_DIR/clients/vim/vimspector.json" ]; then
        if [ -f "$HOME/.vimspector.json" ]; then
            echo "   NOTE: $HOME/.vimspector.json already exists, leaving it."
        else
            cp "$RELEASE_DIR/clients/vim/vimspector.json" "$HOME/.vimspector.json"
            echo "   Installed: $HOME/.vimspector.json"
        fi
    fi

    echo ""
    echo "   Next: open a .obs file in gvim, set a breakpoint with <F9>,"
    echo "         start debugging with <F5>. See clients/vim/README.md."
}

# --- main ---
setup_lsp_home

case "$EDITOR" in
    vscode)  install_vscode ;;
    sublime) install_sublime ;;
    neovim)  install_neovim ;;
    emacs)   install_emacs ;;
    helix)   install_helix ;;
    vim)     install_vim ;;
    all)
        install_vscode
        install_sublime
        install_neovim
        install_emacs
        install_helix
        install_vim
        ;;
    *)
        echo "ERROR: Unknown editor \"$EDITOR\""
        usage
        ;;
esac

echo ""
echo "========================================"
echo " Install complete"
echo "========================================"
echo ""
