import * as fs from 'fs';
import * as path from 'path';

import { workspace, window, ExtensionContext, debug, DebugAdapterDescriptorFactory, DebugSession, DebugAdapterDescriptor, DebugAdapterExecutable, DebugAdapterExecutableOptions, ProviderResult } from 'vscode';

import {
    Executable,
    LanguageClient,
    LanguageClientOptions,
    ServerOptions
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export function activate(context: ExtensionContext) {
    const config = workspace.getConfiguration();
    const isWindows = process.platform === 'win32';
    const installDir = isWindows
        ? config.get<string>('objk.win.install.dir', 'C:\\Program Files\\Objeck')
        : config.get<string>('objk.posix.install.dir', '/usr/local/objeck');

    const obr = path.join(installDir, 'bin', isWindows ? 'obr.exe' : 'obr');
    if(!fs.existsSync(obr)) {
        window.showErrorMessage(`Objeck LSP: no Objeck runtime at '${obr}'. Set ${isWindows ? 'objk.win.install.dir' : 'objk.posix.install.dir'} to your Objeck install directory.`);
    }
    else {
        // One server per window, over stdio, started and stopped by the language
        // client. The extension used to start a server itself on a fixed named
        // pipe ('objk-pipe' under \\.\pipe on Windows, /tmp/objk-pipe elsewhere), so a second window either
        // failed to create it or connected to the first window's server, and the
        // two windows' workspaces and documents mixed (#1233).
        //
        // obr is started directly, not through a shell: the install directory
        // comes from settings and is never parsed as a command line. 'transport'
        // is left unset on purpose -- TransportKind.stdio would append '--stdio'
        // to the arguments.
        const serverDir = context.asAbsolutePath('server');
        const executable: Executable = {
            command: obr,
            args: [path.join(serverDir, 'objeck_lsp.obe'), path.join(serverDir, 'objk_apis.json'), 'stdio'],
            options: {
                cwd: serverDir,
                env: {
                    ...process.env,
                    OBJECK_LIB_PATH: path.join(installDir, 'lib'),
                    // the VM reads the framed byte stream as bytes only in this mode
                    OBJECK_STDIO: 'binary',
                    PATH: path.join(installDir, 'bin') + path.delimiter + (process.env.PATH ?? '')
                }
            }
        };
        const serverOptions: ServerOptions = { run: executable, debug: executable };

        const clientOptions: LanguageClientOptions = {
            documentSelector: [{ scheme: 'file', language: 'objeck' }],
            synchronize: {
                fileEvents: workspace.createFileSystemWatcher('**/build.json')
            }
        };

        client = new LanguageClient('objeck_lsp', 'Objeck Language Server', serverOptions, clientOptions);
        client.start();
    }

    // Register debug adapter
    const debugAdapterFactory = new ObjeckDebugAdapterFactory();
    context.subscriptions.push(debug.registerDebugAdapterDescriptorFactory('objeck', debugAdapterFactory));
}

class ObjeckDebugAdapterFactory implements DebugAdapterDescriptorFactory {
    createDebugAdapterDescriptor(session: DebugSession): ProviderResult<DebugAdapterDescriptor> {
        const config = workspace.getConfiguration();
        let obdPath: string;
        let installDir: string;

        if(process.platform === 'win32') {
            installDir = config.get<string>('objk.win.install.dir', 'C:\\Program Files\\Objeck');
            obdPath = path.join(installDir, 'bin', 'obd.exe');
        }
        else {
            installDir = config.get<string>('objk.posix.install.dir', '/usr/local/objeck');
            obdPath = path.join(installDir, 'bin', 'obd');
        }

        const libPath = path.join(installDir, 'lib');
        const options: DebugAdapterExecutableOptions = {
            env: {
                ...process.env,
                OBJECK_LIB_PATH: libPath
            }
        };

        return new DebugAdapterExecutable(obdPath, ['--dap'], options);
    }
}

export function deactivate(): Thenable<void> | undefined {
    // the client stops the server it started
    return client ? client.stop() : undefined;
}
