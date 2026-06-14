v2026.6.1
* Cooperative stop-the-world GC, VM serialization/deserialization hardening, TLS cert verification, compiler/debugger fixes
* Bumped to support Objeck v2026.6.1
---

v2026.6.0
* New System.AI library, System.ML overhaul, record types, JIT/compiler fixes
* Bumped to support Objeck v2026.6.0
---

v2026.5.3
* JIT select dispatch, API doc overhaul, ODBC improvements, bug fixes
* Bumped to support Objeck v2026.5.3
---

v2026.5.2
* HTTP/2+3/QUIC clients, Gemini/OpenAI API expansion, ARM64 Windows support, WebSocket hardening
* Bumped to support Objeck v2026.5.2
---

v2026.5.0
* Face recognition API (SCRFD+ArcFace/InsightFace buffalo_l)
* Windows emoji support (full Unicode supplementary plane)
* LSP enhancements: typeHierarchy, selectionRange, documentHighlight, foldingRange
* Bumped to support Objeck v2026.5.0
---

v2026.4.3
* DAP debugger hover with instance field display
* Editor setup refresh for VSCode, Sublime, and gvim (Win/Linux/macOS)
* LSP crash fixes and null guards
* Bumped to support Objeck v2026.4.3
---

v2026.4.2
* DAP (Debug Adapter Protocol) support for VS Code debugging
* Breakpoints, stepping, variable inspection, conditional breakpoints
* Bumped to support Objeck v2026.4.2
---

v2025.7.0
* Bumped to support latest version

v2025.3.0
* Pipe support for VS Code on macOS and Linux
* Improved stability switch between source programs

v2025.2.2
* Included support for named pipes in VS Code on Windows.

[Installation]
===
Support and tested with VS Code, Sublime, Kate, ecode, Neovim, Emacs and Helix.
See docs/install_guide.html for detailed instructions with screenshots.

The install scripts create a self-contained deployment at ~/.objeck-lsp/ with
the Objeck runtime and LSP server. Environment variables and paths are configured
automatically. No admin rights required for user installs.

VS Code
--
Automated: scripts/install.cmd <objeck_install_dir> vscode  (Windows)
           scripts/install.sh <objeck_install_dir> vscode    (Linux/macOS)

  User install:    scripts/install.cmd C:\Users\you\objeck vscode
  System install:  scripts/install.cmd "C:\Program Files\Objeck" vscode

Manual:
1. Download and install the latest version of Objeck (https://github.com/objeck/objeck-lang)
2. Download and unzip the VS Code LSP plugin (https://github.com/objeck/objeck-lsp)
3. In VS Code click the extensions button (or Ctrl+Shift+X) and drag-and-drop "objeck-lsp-xxx.vsix" file
4. Edit the 'objk.xxx.install.dir' property in VS Code Settings to point to the root Objeck install directory
  4a. For macOS and Linux ensure the '<user-root>/.vscode/extensions/objeck-lsp.objeck-lsp-xxxx.x.x/server/lsp_server.sh' file is Unix formatted and has '+x' permission
5. Restart VS Code

Sublime
---
Automated: scripts/install.cmd <objeck_install_dir> sublime  (Windows)
           scripts/install.sh <objeck_install_dir> sublime    (Linux/macOS)

  User install:    scripts/install.cmd C:\Users\you\objeck sublime
  System install:  scripts/install.cmd "C:\Program Files\Objeck" sublime

Manual:
1. Download and install the latest version of Objeck (https://github.com/objeck/objeck-lang)
2. Copy clients/sublime/objeck.sublime-syntax to your Packages/Objeck/ directory
3. Install the LSP package via Package Control
4. Open Preferences > Package Settings > LSP > Settings and add the "objeck" client configuration:
{
	"clients": {
		"objeck": {
			"enabled": true,
			"command": [
				"<objeck_lsp_path>/bin/obr.exe",
				"<objeck_lsp_path>/objeck_lsp.obe",
				"<objeck_lsp_path>/objk_apis.json",
				"stdio"
			],
			"env": {
				"OBJECK_LIB_PATH": "<objeck_lsp_path>/lib",
				"OBJECK_STDIO": "binary"
			},
			"selector": "source.objeck-obs"
		}
	}
}
Replace <objeck_lsp_path> with your deployment path (e.g. ~/.objeck-lsp).
5. Open Tools > LSP > "Enable Language Server Globally" and select "objeck"
6. Open the directory of the file you want to edit, then open the file. For projects, read below.

Kate
---
Settings -> Configure Kate... -> LSP Client -> User Sever Settings
Create a new settings file with the content:

{
    "servers": {
        "objeck": {
            "command": [
                "<objeck_lsp_path>/bin/obr.exe",
                "<objeck_lsp_path>/objeck_lsp.obe",
                "<objeck_lsp_path>/objk_apis.json",
                "stdio"
            ],
            "url": "https://github.com/objeck/objeck-lsp",
            "highlightingModeRegex": "^Objeck$"
        }
    }
}
Note: Kate doesn't support per-server environment variables. Set OBJECK_LIB_PATH
and OBJECK_STDIO system-wide on Windows, or in your shell profile on Linux / macOS.

ecode
---
1. Install the LSP plugin
2. Edit the "%userprofile%\AppData\Roaming\ecode\plugins\lspclient.json" file and add the block under "servers" below

{
  "config": {
    "hover_delay": "1s",
    "semantic_highlighting": false,
    "server_close_after_idle_time": "1m"
  },
  "keybindings": {
    "lsp-go-to-declaration": "",
    "lsp-go-to-definition": "f2",
    "lsp-go-to-implementation": "",
    "lsp-go-to-type-definition": "",
    "lsp-memory-usage": "",
    "lsp-rename-symbol-under-cursor": "ctrl+shift+r",
    "lsp-switch-header-source": "",
    "lsp-symbol-code-action": "alt+return",
    "lsp-symbol-info": "f1",
    "lsp-symbol-references": ""
  },
  "servers": [
    {
      "command": "obr <lsp_server_path>/objeck_lsp.obe <lsp_server_path>/objk_apis.json stdio",
      "file_patterns": [
        "%.obs"
      ],
      "language": "objeck",
      "name": "objeck",
      "url": "https://objeck.org/"
    }
  ]
}

Neovim
---
Automated: scripts/install.cmd <objeck_install_dir> neovim  (Windows)
           scripts/install.sh <objeck_install_dir> neovim    (Linux/macOS)

  User install:    scripts/install.cmd C:\Users\you\objeck neovim
  System install:  scripts/install.cmd "C:\Program Files\Objeck" neovim

The install script copies the LSP config and syntax highlighting file. It also
sets the full path to obr and configures environment variables automatically.

Manual:
Requires Neovim 0.11+ with built-in LSP support.

1. Download and install the latest version of Objeck (https://github.com/objeck/objeck-lang)
2. Copy clients/neovim/objeck.lua to your Neovim LSP config directory:
   Windows:    %LOCALAPPDATA%\nvim\lsp\objeck.lua
   Linux/macOS: ~/.config/nvim/lsp/objeck.lua
3. Copy clients/neovim/objeck.vim to your Neovim syntax directory:
   Windows:    %LOCALAPPDATA%\nvim\syntax\objeck.vim
   Linux/macOS: ~/.config/nvim/syntax/objeck.vim
4. Edit the cmd and cmd_env paths in objeck.lua to point to your installation
5. Add to your init.lua:
   vim.filetype.add({ extension = { obs = 'objeck' } })
   vim.lsp.enable('objeck')
6. Open a .obs file to activate the language server

Emacs
---
Automated: scripts/install.cmd <objeck_install_dir> emacs  (Windows)
           scripts/install.sh <objeck_install_dir> emacs    (Linux/macOS)

  User install:    scripts/install.cmd C:\Users\you\objeck emacs
  System install:  scripts/install.cmd "C:\Program Files\Objeck" emacs

The install script copies objeck-mode.el with patched paths, sets environment
variables, and creates init.el if needed. Includes syntax highlighting and
Eglot LSP integration.

Manual:
Requires Emacs 29+ with built-in Eglot.

1. Download and install the latest version of Objeck (https://github.com/objeck/objeck-lang)
2. Copy clients/emacs/objeck-mode.el to your Emacs load-path:
   Windows:    %APPDATA%\.emacs.d\lisp\
   Linux/macOS: ~/.emacs.d/lisp/
3. Edit the server command paths and OBJECK_LIB_PATH in the file
4. Add to your init.el:
   (add-to-list 'load-path (expand-file-name "lisp" user-emacs-directory))
   (require 'objeck-mode)
   (add-hook 'objeck-mode-hook 'eglot-ensure)
5. Open a .obs file - Eglot will start the LSP server automatically

Helix
---
1. Download and install the latest version of Objeck (https://github.com/objeck/objeck-lang)
2. Set environment variables: OBJECK_LIB_PATH and OBJECK_STDIO=binary
3. Merge the contents of clients/helix/languages.toml into ~/.config/helix/languages.toml
4. Edit the args paths to point to your Objeck installation
5. Open a .obs file - Helix will start the language server automatically

[Workspaces]
===
Workspaces enable the LSP server to compile and examine all files in a project workspace. This functionality aids in managing projects that involve several files or require particular libraries for code compilation.

1. To set up projects with multiple files, create a "build.json" file in the directory of the files you want to be scanned.
2. The structure of the "build.json" file is as follows:
{
	"files": [
		"file_1.obs",
		"file_2.obs"
	],
	"libs": [
		"gen_collect.obl",
		"regex.obl",
		"net.obl",
		"misc.obl",
		"odbc.obl"
	],
	"flags": ""
}
3. To enable the project file, Close all open files and open the directory that contains the "build.json" file in either VS Code or Sublime

[Updating the LSP Server]
===
After rebuilding Objeck or updating the LSP server, refresh the self-contained deployment:

User install:
  Windows:  scripts\update_lsp.cmd C:\Users\you\objeck
  Linux:    scripts/update_lsp.sh ~/objeck

System-wide install:
  Windows:  scripts\update_lsp.cmd "C:\Program Files\Objeck"
  Linux:    scripts/update_lsp.sh /usr/local/objeck

This copies the latest runtime and server files to ~/.objeck-lsp/ without changing editor configs.
