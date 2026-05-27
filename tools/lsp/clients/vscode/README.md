# Objeck LSP

Visual Studio Code Objeck LSP client/server and syntax highlighter.

## Running client

* Edit the 'package.json' file in the '%HOMEPATH%/.vscode/extensions/objeck-lsp.objeck-lsp-xxx/package.json' or '~/.vscode/extensions/objeck-lsp.objeck-lsp-xxx/package.json' directory so it points to the Objeck install directory. Depending upon your platform edit the 'objk.win.install.dir' for Windows or 'objk.posix.install.dir' for macOS and Linux.
* Open a \*.obs source file or a workspace containing \*.obs files and a build.json files (see below)

## Workspaces

Workspaces allow the LSP to build and scan all files within a project configuration. This feature supports solutions that span multiple files or require specific libraries to be built and inspected correctly.

1. To set up projects with multiple files, create a "build.json" file in the directory of the files you want to be scanned.
2. The structure of the "build.json" file is as follows:
```
{
  "files": [
    "file_1.obs",
    "file_2.obs"
  ],
  "libs": [
    "gen_collect.obl",
    "regex.obl",
    "net.obl",
    "json.obl",
    "misc.obl"
  ],
  "flags": ""
}
```
3. To enable the project file, Close all open files and open the directory that contains the "build.json" file in either VS Code or Sublime


## Features

#### Notifications
* Initialized `initialized`
* Cancel Request `$/cancelRequest`
* File Open `textDocument/didOpen`
* File Changed `textDocument/didChange`
* File Save `textDocument/didSave`
* File Close `textDocument/didClose`

#### Callbacks
* Initialize `initialize`
* Code completion `textDocument/completion`
* Code resolution `completionItem/resolve`
* Code symbol `textDocument/documentSymbol`
* Method/Function signature help `textDocument/signatureHelp`
* Goto code references `textDocument/references`
* Goto code definitions `textDocument/definition`
* Goto code declaration `textDocument/declaration`
* Variable rename `textDocument/rename`
* Format document `textDocument/formatting`
* Format selection `textDocument/rangeFormatting`
* Editor shutdown `shutdown`

#### Workspaces
  * JSON configured workspaces to support multi-file projects
  * Find symbol in workspace `workspace/symbol`
  * Watch file changed `workspace/didChangeWatchedFiles`
  * Watch workspace changed `workspace/didChangeWorkspaceFolders`