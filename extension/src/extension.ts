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
let diagnosticCollection: vscode.DiagnosticCollection;

export function activate(context: vscode.ExtensionContext) {
    outputChannel = vscode.window.createOutputChannel('Vivid');
    outputChannel.appendLine('Vivid extension activated');

    // Store extension path for native module loading
    extensionPath = context.extensionPath;

    // Create diagnostic collection for compile errors
    diagnosticCollection = vscode.languages.createDiagnosticCollection('vivid');
    context.subscriptions.push(diagnosticCollection);

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

let extensionPath: string = '';

function connectToRuntime() {
    const config = vscode.workspace.getConfiguration('vivid');
    const port = config.get<number>('websocketPort') || 9876;

    if (runtimeClient) {
        runtimeClient.disconnect();
    }

    runtimeClient = new RuntimeClient(port);

    // Initialize shared memory support
    if (extensionPath) {
        const hasSharedMem = runtimeClient.initSharedMemory(extensionPath);
        outputChannel.appendLine(`Shared memory support: ${hasSharedMem ? 'enabled' : 'disabled (native module not found)'}`);
    }

    runtimeClient.onConnected(() => {
        outputChannel.appendLine('Connected to Vivid runtime');
        statusBar?.setConnected(true);
    });

    runtimeClient.onDisconnected(() => {
        outputChannel.appendLine('Disconnected from Vivid runtime');
        statusBar?.setConnected(false);
        vscode.window.setStatusBarMessage('$(warning) Vivid: Disconnected from runtime', 5000);
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
            // Clear any previous compile errors
            diagnosticCollection.clear();
        } else {
            vscode.window.showErrorMessage(`Vivid compile error: ${message}`);
            outputChannel.appendLine(`Compile error: ${message}`);
            showCompileErrors(message);
        }
        statusBar?.setCompileStatus(success);
    });

    runtimeClient.onError((error) => {
        outputChannel.appendLine(`Runtime error: ${error}`);
        // Show error to user for important errors
        if (error.includes('ECONNREFUSED')) {
            vscode.window.showWarningMessage('Vivid: Cannot connect to runtime. Is it running?');
        } else if (!error.includes('Parse error')) {
            // Show other errors that aren't just JSON parse issues
            vscode.window.showErrorMessage(`Vivid runtime error: ${error}`);
        }
    });

    runtimeClient.connect();
}

function isVividFile(document: vscode.TextDocument): boolean {
    return document.fileName.endsWith('chain.cpp') ||
           document.fileName.endsWith('.wgsl') ||
           document.getText().includes('vivid/vivid.h');
}

function showCompileErrors(message: string) {
    // Clear previous diagnostics
    diagnosticCollection.clear();

    // Group diagnostics by file
    const diagnosticsByFile = new Map<string, vscode.Diagnostic[]>();

    // Parse GCC/Clang error format: file:line:col: error/warning: message
    const errorRegex = /([^:\s][^:]*):(\d+):(\d+):\s*(error|warning|note):\s*(.+)/g;
    let match;

    while ((match = errorRegex.exec(message)) !== null) {
        const [, file, line, col, severity, text] = match;
        const lineNum = parseInt(line) - 1;
        const colNum = parseInt(col) - 1;

        const range = new vscode.Range(
            lineNum, colNum,
            lineNum, colNum + Math.min(text.length, 50)
        );

        let diagSeverity: vscode.DiagnosticSeverity;
        switch (severity) {
            case 'error':
                diagSeverity = vscode.DiagnosticSeverity.Error;
                break;
            case 'warning':
                diagSeverity = vscode.DiagnosticSeverity.Warning;
                break;
            case 'note':
                diagSeverity = vscode.DiagnosticSeverity.Information;
                break;
            default:
                diagSeverity = vscode.DiagnosticSeverity.Error;
        }

        const diagnostic = new vscode.Diagnostic(range, text, diagSeverity);
        diagnostic.source = 'Vivid';

        // Group by file
        if (!diagnosticsByFile.has(file)) {
            diagnosticsByFile.set(file, []);
        }
        diagnosticsByFile.get(file)!.push(diagnostic);
    }

    // Set diagnostics for each file
    for (const [file, diagnostics] of diagnosticsByFile) {
        const uri = vscode.Uri.file(file);
        diagnosticCollection.set(uri, diagnostics);
    }

    // If we found any errors, try to open and reveal the first one
    if (diagnosticsByFile.size > 0) {
        const firstFile = diagnosticsByFile.keys().next().value as string | undefined;
        if (firstFile) {
            const firstDiag = diagnosticsByFile.get(firstFile)?.[0];
            if (firstDiag) {
                const uri = vscode.Uri.file(firstFile);
                vscode.workspace.openTextDocument(uri).then(
                    doc => {
                        vscode.window.showTextDocument(doc).then(editor => {
                            editor.revealRange(firstDiag.range, vscode.TextEditorRevealType.InCenter);
                        });
                    },
                    () => {
                        // File might not exist or not be accessible
                    }
                );
            }
        }
    }
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
