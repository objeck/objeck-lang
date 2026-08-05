import * as net from 'net';
import * as path from 'path';
import * as child_process from 'child_process';

import { workspace, env, ExtensionContext, debug, DebugAdapterDescriptorFactory, DebugSession, DebugAdapterDescriptor, DebugAdapterExecutable, DebugAdapterExecutableOptions, ProviderResult } from 'vscode';

import {
    LanguageClient,
    LanguageClientOptions,
    StreamInfo
} from 'vscode-languageclient/node';

let client: LanguageClient;
let serverProcess: child_process.ChildProcess;

export function activate(context: ExtensionContext) {
    let objkInstallDir;
    const config = workspace.getConfiguration();
    if(process.platform === 'win32') {
        objkInstallDir = config.get('objk.win.install.dir');
    }
    else {
        objkInstallDir = config.get('objk.posix.install.dir');
    }

    // Start the external pipe server
    startExternalServer(context, objkInstallDir);

    let connectionInfo;
    if(process.platform === 'win32') {
        connectionInfo = {
            path: "\\\\.\\pipe\\objk-pipe"
        };
    }
    else {
        connectionInfo = {
            path: "/tmp/objk-pipe"
        };
    }

    const serverOptions = () => {
        return new Promise<StreamInfo>((resolve, reject) => {
            let attempts = 0;
            const maxAttempts = 10;
            const retryDelay = 500;

            function tryConnect() {
                attempts++;
                const pipe = net.connect(connectionInfo);

                pipe.on('connect', () => {
                    const result: StreamInfo = {
                        writer: pipe,
                        reader: pipe
                    };
                    resolve(result);
                });

                pipe.on('error', (err) => {
                    if(attempts < maxAttempts) {
                        setTimeout(tryConnect, retryDelay);
                    }
                    else {
                        reject(new Error(`Failed to connect to Objeck LSP server after ${maxAttempts} attempts: ${err.message}`));
                    }
                });
            }

            tryConnect();
        });
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'objeck' }],
        synchronize: {
            fileEvents: workspace.createFileSystemWatcher('**/build.json')
        }
    };

    client = new LanguageClient('objeck_lsp', 'Objeck Language Server', serverOptions, clientOptions);
    client.start();

    // Register debug adapter
    const debugAdapterFactory = new ObjeckDebugAdapterFactory();
    context.subscriptions.push(debug.registerDebugAdapterDescriptorFactory('objeck', debugAdapterFactory));
}

function startExternalServer(context: ExtensionContext, objkInstallDir) {
    let serverScript;

    if(process.platform === 'win32') {
        serverScript = context.asAbsolutePath(path.join('server', 'lsp_server.cmd'));
    }
    else {
        serverScript = context.asAbsolutePath(path.join('server', 'lsp_server.sh'));
    }

    // path to plugin install directory
    const pluginDir = context.extensionPath;

    // Pass the directories through the environment rather than as command line
    // arguments. The Windows launcher is a .cmd file, so it has to be started
    // through a shell, and user configured paths interpolated into a shell
    // command line could be parsed as shell metacharacters.
    const serverEnv = {
        ...process.env,
        OBJECK_INSTALL_DIR: objkInstallDir ? String(objkInstallDir) : '',
        OBJECK_PLUGIN_DIR: pluginDir
    };

    serverProcess = child_process.spawn(serverScript, [], {
        shell: process.platform === 'win32',
        env: serverEnv
    });

    serverProcess.on('error', (err) => {
        console.error(`Failed to start Objeck LSP server: ${err.message}`);
    });

    serverProcess.stdout.on('data', (data) => {
        console.log(`Server stdout: ${data}`);
    });

    serverProcess.stderr.on('data', (data) => {
        console.error(`Server stderr: ${data}`);
    });

    serverProcess.on('close', (code) => {
        console.log(`Server process exited with code ${code}`);
    });
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
    if(!client) {
        if(serverProcess) {
            serverProcess.kill();
        }
        return undefined;
    }

    return client.stop().then(() => {
        if(serverProcess) {
            serverProcess.kill();
        }
    });
}
