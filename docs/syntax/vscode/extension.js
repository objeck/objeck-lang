const vscode = require('vscode');
const path = require('path');
const fs = require('fs');

function findObd() {
    const obdName = process.platform === 'win32' ? 'obd.exe' : 'obd';
    const candidates = [];

    // 1. OBJECK_HOME environment variable
    const objeckHome = process.env.OBJECK_HOME;
    if(objeckHome) {
        candidates.push(path.join(objeckHome, 'bin', obdName));
    }

    // 2. Workspace-relative deploy directories
    const folders = vscode.workspace.workspaceFolders;
    if(folders) {
        for(const folder of folders) {
            const root = folder.uri.fsPath;
            candidates.push(path.join(root, 'core', 'release', 'deploy-x64', 'bin', obdName));
            candidates.push(path.join(root, 'core', 'release', 'deploy', 'bin', obdName));
        }
    }

    // 3. PATH
    const pathDirs = (process.env.PATH || '').split(path.delimiter);
    for(const dir of pathDirs) {
        candidates.push(path.join(dir, obdName));
    }

    for(const candidate of candidates) {
        if(fs.existsSync(candidate)) {
            return candidate;
        }
    }
    return null;
}

function activate(context) {
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory('objeck', {
            createDebugAdapterDescriptor(_session) {
                const obdPath = findObd();
                if(obdPath) {
                    return new vscode.DebugAdapterExecutable(obdPath, ['--dap']);
                }

                vscode.window.showErrorMessage(
                    'Objeck debugger (obd) not found. Set OBJECK_HOME or add obd to PATH.'
                );
                return undefined;
            }
        })
    );
}

function deactivate() {}

module.exports = { activate, deactivate };
