import * as vscode from 'vscode';
import { RuntimeClient, NodeUpdate } from './runtimeClient';
import { DecorationManager } from './decorations';
import { StatusBar } from './statusBar';
import { OperatorTreeProvider } from './operatorTreeView';

let runtimeClient: RuntimeClient | undefined;
let decorationManager: DecorationManager | undefined;
let statusBar: StatusBar | undefined;
let operatorTreeProvider: OperatorTreeProvider | undefined;
let outputChannel: vscode.OutputChannel;
let diagnosticCollection: vscode.DiagnosticCollection;

export function activate(context: vscode.ExtensionContext) {
    outputChannel = vscode.window.createOutputChannel('Vivid');
    outputChannel.appendLine('Vivid extension activated');

    // Create diagnostic collection for compile errors
    diagnosticCollection = vscode.languages.createDiagnosticCollection('vivid');
    context.subscriptions.push(diagnosticCollection);

    decorationManager = new DecorationManager(context);
    statusBar = new StatusBar();

    // Register operator tree view
    operatorTreeProvider = new OperatorTreeProvider();
    context.subscriptions.push(
        vscode.window.registerTreeDataProvider('vividOperators', operatorTreeProvider)
    );

    // Register commands
    context.subscriptions.push(
        vscode.commands.registerCommand('vivid.startRuntime', () => startRuntime(context)),
        vscode.commands.registerCommand('vivid.stopRuntime', stopRuntime),
        vscode.commands.registerCommand('vivid.reload', reload),
        vscode.commands.registerCommand('vivid.toggleInlineDecorations', toggleInlineDecorations),
        vscode.commands.registerCommand('vivid.goToOperator', goToOperator),
        vscode.commands.registerCommand('vivid.refreshOperators', () => {
            // Request fresh operator list from runtime
            if (runtimeClient) {
                runtimeClient.sendReload();
            }
        })
    );

    // Watch for editor changes
    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor(editor => {
            if (editor && decorationManager) {
                decorationManager.updateDecorations(editor);
            }
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
        operatorTreeProvider?.clear();
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
    });

    runtimeClient.onCompileStatus((success, message) => {
        if (success) {
            vscode.window.setStatusBarMessage('$(check) Vivid: Compiled', 3000);
            outputChannel.appendLine('Compilation successful');
            diagnosticCollection.clear();
        } else {
            vscode.window.showErrorMessage(`Vivid compile error: ${message}`);
            outputChannel.appendLine(`Compile error: ${message}`);
            showCompileErrors(message);
        }
        statusBar?.setCompileStatus(success);
    });

    runtimeClient.onOperatorList((operators) => {
        outputChannel.appendLine(`Received ${operators.length} operators`);
        operatorTreeProvider?.updateOperators(operators);
    });

    runtimeClient.onParamValues((params) => {
        operatorTreeProvider?.updateParams(params);
    });

    runtimeClient.onError((error) => {
        outputChannel.appendLine(`Runtime error: ${error}`);
        if (error.includes('ECONNREFUSED')) {
            vscode.window.showWarningMessage('Vivid: Cannot connect to runtime. Is it running?');
        } else if (!error.includes('Parse error')) {
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
    diagnosticCollection.clear();

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

    // Open and reveal first error
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
                    () => { /* File might not exist */ }
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
            prompt: 'Path to vivid executable',
            placeHolder: '/path/to/vivid'
        });
        if (!runtimePath) return;
        await config.update('runtimePath', runtimePath, vscode.ConfigurationTarget.Global);
    }

    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (!workspaceFolders) {
        vscode.window.showErrorMessage('No workspace folder open');
        return;
    }

    // Derive vivid root from runtime path (runtime is at vivid/build/bin/vivid)
    const vividRoot = runtimePath.replace(/\/build\/bin\/vivid$/, '');

    const terminal = vscode.window.createTerminal('Vivid Runtime');
    terminal.show();
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

function toggleInlineDecorations() {
    const config = vscode.workspace.getConfiguration('vivid');
    const current = config.get<boolean>('showInlineDecorations');
    config.update('showInlineDecorations', !current, vscode.ConfigurationTarget.Global);

    if (decorationManager) {
        decorationManager.setEnabled(!current);
    }
}

async function goToOperator(line: number) {
    // Find chain.cpp in workspace
    const files = await vscode.workspace.findFiles('**/chain.cpp', null, 1);
    if (files.length === 0) {
        vscode.window.showWarningMessage('chain.cpp not found in workspace');
        return;
    }

    const doc = await vscode.workspace.openTextDocument(files[0]);
    const editor = await vscode.window.showTextDocument(doc);

    // Go to line (0-indexed in VS Code)
    const lineIndex = Math.max(0, line - 1);
    const range = new vscode.Range(lineIndex, 0, lineIndex, 0);
    editor.selection = new vscode.Selection(range.start, range.start);
    editor.revealRange(range, vscode.TextEditorRevealType.InCenter);
}

export function deactivate() {
    stopRuntime();
}
