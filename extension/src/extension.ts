import * as vscode from 'vscode';
import { RuntimeClient, NodeUpdate } from './runtimeClient';
import { PreviewPanel } from './previewPanel';
import { DecorationManager } from './decorations';
import { StatusBar } from './statusBar';

let runtimeClient: RuntimeClient | undefined;
let previewPanel: PreviewPanel | undefined;
let decorationManager: DecorationManager | undefined;
let statusBar: StatusBar | undefined;
let outputChannel: vscode.OutputChannel;

export function activate(context: vscode.ExtensionContext) {
    outputChannel = vscode.window.createOutputChannel('Vivid');
    outputChannel.appendLine('Vivid extension activated');

    decorationManager = new DecorationManager(context);
    statusBar = new StatusBar();

    // Register commands
    context.subscriptions.push(
        vscode.commands.registerCommand('vivid.startRuntime', () => startRuntime(context)),
        vscode.commands.registerCommand('vivid.stopRuntime', stopRuntime),
        vscode.commands.registerCommand('vivid.reload', reload),
        vscode.commands.registerCommand('vivid.showPreviewPanel', () => showPreviewPanel(context)),
        vscode.commands.registerCommand('vivid.toggleInlineDecorations', toggleInlineDecorations)
    );

    // Watch for editor changes
    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor(editor => {
            if (editor && decorationManager) {
                decorationManager.updateDecorations(editor);
            }
        })
    );

    // Watch for document changes (for live parameter updates)
    context.subscriptions.push(
        vscode.workspace.onDidChangeTextDocument(event => {
            // Could parse and send parameter changes here
        })
    );

    // Auto-connect if enabled and workspace contains Vivid project
    const config = vscode.workspace.getConfiguration('vivid');
    if (config.get<boolean>('autoConnect')) {
        vscode.workspace.findFiles('**/chain.cpp', null, 1).then(files => {
            if (files.length > 0) {
                connectToRuntime();
            }
        });
    }

    context.subscriptions.push(outputChannel, statusBar);
}

function connectToRuntime() {
    const config = vscode.workspace.getConfiguration('vivid');
    const port = config.get<number>('websocketPort') || 9876;

    if (runtimeClient) {
        runtimeClient.disconnect();
    }

    runtimeClient = new RuntimeClient(port);

    runtimeClient.onConnected(() => {
        outputChannel.appendLine('Connected to Vivid runtime');
        statusBar?.setConnected(true);
    });

    runtimeClient.onDisconnected(() => {
        outputChannel.appendLine('Disconnected from Vivid runtime');
        statusBar?.setConnected(false);
    });

    runtimeClient.onNodeUpdate((nodes) => {
        if (decorationManager) {
            decorationManager.updateNodes(nodes);
            const editor = vscode.window.activeTextEditor;
            if (editor && isVividFile(editor.document)) {
                decorationManager.updateDecorations(editor);
            }
        }
        if (previewPanel) {
            previewPanel.updateNodes(nodes);
        }
    });

    runtimeClient.onCompileStatus((success, message) => {
        if (success) {
            vscode.window.setStatusBarMessage('$(check) Vivid: Compiled', 3000);
            outputChannel.appendLine('Compilation successful');
        } else {
            vscode.window.showErrorMessage(`Vivid compile error: ${message}`);
            outputChannel.appendLine(`Compile error: ${message}`);
            showCompileErrors(message);
        }
        statusBar?.setCompileStatus(success);
    });

    runtimeClient.onError((error) => {
        outputChannel.appendLine(`Runtime error: ${error}`);
    });

    runtimeClient.connect();
}

function isVividFile(document: vscode.TextDocument): boolean {
    return document.fileName.endsWith('chain.cpp') ||
           document.fileName.endsWith('.wgsl') ||
           document.getText().includes('vivid/vivid.h');
}

function showCompileErrors(message: string) {
    // Parse error message and show diagnostics
    const diagnostics: vscode.Diagnostic[] = [];

    // Simple regex to parse GCC/Clang error format
    const errorRegex = /([^:]+):(\d+):(\d+):\s*(error|warning):\s*(.+)/g;
    let match;

    while ((match = errorRegex.exec(message)) !== null) {
        const [, file, line, col, severity, text] = match;
        const range = new vscode.Range(
            parseInt(line) - 1, parseInt(col) - 1,
            parseInt(line) - 1, parseInt(col) + 20
        );

        const diagnostic = new vscode.Diagnostic(
            range,
            text,
            severity === 'error'
                ? vscode.DiagnosticSeverity.Error
                : vscode.DiagnosticSeverity.Warning
        );
        diagnostic.source = 'Vivid';
        diagnostics.push(diagnostic);
    }

    // Would need a DiagnosticCollection to actually show these
}

async function startRuntime(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('vivid');
    let runtimePath = config.get<string>('runtimePath');

    if (!runtimePath) {
        runtimePath = await vscode.window.showInputBox({
            prompt: 'Path to vivid-runtime executable',
            placeHolder: '/path/to/vivid-runtime'
        });
        if (!runtimePath) return;
        await config.update('runtimePath', runtimePath, vscode.ConfigurationTarget.Global);
    }

    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (!workspaceFolders) {
        vscode.window.showErrorMessage('No workspace folder open');
        return;
    }

    // Derive vivid root from runtime path (runtime is at vivid/build/bin/vivid-runtime)
    const vividRoot = runtimePath.replace(/\/build\/bin\/vivid-runtime$/, '');

    const terminal = vscode.window.createTerminal('Vivid Runtime');
    terminal.show();
    // Change to vivid root directory so shaders can be found, then run with project path
    terminal.sendText(`cd "${vividRoot}" && "${runtimePath}" "${workspaceFolders[0].uri.fsPath}"`);

    outputChannel.appendLine(`Starting runtime: ${runtimePath}`);

    // Wait for runtime to start, then connect
    setTimeout(() => connectToRuntime(), 2000);
}

function stopRuntime() {
    if (runtimeClient) {
        runtimeClient.disconnect();
        runtimeClient = undefined;
    }
    statusBar?.setConnected(false);
}

function reload() {
    if (runtimeClient) {
        runtimeClient.sendReload();
        outputChannel.appendLine('Reload requested');
    } else {
        vscode.window.showWarningMessage('Not connected to Vivid runtime');
    }
}

function showPreviewPanel(context: vscode.ExtensionContext) {
    if (!previewPanel) {
        previewPanel = new PreviewPanel(context.extensionUri, (nodeId: string) => {
            // Jump to node definition when clicked
            jumpToNode(nodeId);
        });
    }
    previewPanel.reveal();
}

function toggleInlineDecorations() {
    const config = vscode.workspace.getConfiguration('vivid');
    const current = config.get<boolean>('showInlineDecorations');
    config.update('showInlineDecorations', !current, vscode.ConfigurationTarget.Global);

    if (decorationManager) {
        decorationManager.setEnabled(!current);
    }
}

async function jumpToNode(nodeId: string) {
    const editor = vscode.window.activeTextEditor;
    if (!editor || !decorationManager) return;

    const line = decorationManager.getNodeLine(nodeId);
    if (line !== undefined) {
        const position = new vscode.Position(line - 1, 0);
        editor.selection = new vscode.Selection(position, position);
        editor.revealRange(new vscode.Range(position, position), vscode.TextEditorRevealType.InCenter);
    }
}

export function deactivate() {
    stopRuntime();
}
