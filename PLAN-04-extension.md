# Vivid Implementation Plan — Part 4: VS Code Extension

This document covers the VS Code extension that provides inline previews and editor integration.

---

## Extension Package Configuration

### extension/package.json

```json
{
  "name": "vivid",
  "displayName": "Vivid",
  "description": "Live preview for Vivid creative coding framework",
  "version": "0.1.0",
  "publisher": "vivid",
  "engines": {
    "vscode": "^1.85.0"
  },
  "categories": ["Visualization", "Other"],
  "activationEvents": [
    "workspaceContains:**/chain.cpp",
    "onLanguage:cpp"
  ],
  "main": "./out/extension.js",
  "contributes": {
    "commands": [
      {
        "command": "vivid.startRuntime",
        "title": "Vivid: Start Runtime"
      },
      {
        "command": "vivid.stopRuntime",
        "title": "Vivid: Stop Runtime"
      },
      {
        "command": "vivid.reload",
        "title": "Vivid: Force Reload"
      },
      {
        "command": "vivid.showPreviewPanel",
        "title": "Vivid: Show Preview Panel"
      },
      {
        "command": "vivid.toggleInlineDecorations",
        "title": "Vivid: Toggle Inline Decorations"
      }
    ],
    "views": {
      "explorer": [
        {
          "type": "webview",
          "id": "vivid.previewPanel",
          "name": "Vivid Previews"
        }
      ]
    },
    "configuration": {
      "title": "Vivid",
      "properties": {
        "vivid.runtimePath": {
          "type": "string",
          "default": "",
          "description": "Path to vivid-runtime executable"
        },
        "vivid.websocketPort": {
          "type": "number",
          "default": 9876,
          "description": "WebSocket port for runtime communication"
        },
        "vivid.showInlineDecorations": {
          "type": "boolean",
          "default": true,
          "description": "Show inline preview decorations in editor"
        },
        "vivid.previewSize": {
          "type": "number",
          "default": 48,
          "description": "Size of inline preview thumbnails in pixels"
        },
        "vivid.autoConnect": {
          "type": "boolean",
          "default": true,
          "description": "Automatically connect to runtime when opening a Vivid project"
        }
      }
    },
    "languages": [
      {
        "id": "wgsl",
        "extensions": [".wgsl"],
        "configuration": "./language-configuration.json"
      }
    ]
  },
  "scripts": {
    "vscode:prepublish": "npm run compile",
    "compile": "tsc -p ./",
    "watch": "tsc -watch -p ./",
    "lint": "eslint src --ext ts"
  },
  "devDependencies": {
    "@types/node": "^20.10.0",
    "@types/vscode": "^1.85.0",
    "@types/ws": "^8.5.10",
    "@typescript-eslint/eslint-plugin": "^6.0.0",
    "@typescript-eslint/parser": "^6.0.0",
    "eslint": "^8.50.0",
    "typescript": "^5.3.0"
  },
  "dependencies": {
    "ws": "^8.14.2"
  }
}
```

### extension/tsconfig.json

```json
{
  "compilerOptions": {
    "module": "commonjs",
    "target": "ES2020",
    "outDir": "out",
    "lib": ["ES2020"],
    "sourceMap": true,
    "rootDir": "src",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true
  },
  "exclude": ["node_modules", ".vscode-test"]
}
```

---

## Extension Entry Point

### extension/src/extension.ts

```typescript
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
    
    const terminal = vscode.window.createTerminal('Vivid Runtime');
    terminal.show();
    terminal.sendText(`"${runtimePath}" "${workspaceFolders[0].uri.fsPath}"`);
    
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
```

---

## Runtime Client

### extension/src/runtimeClient.ts

```typescript
import WebSocket from 'ws';

export interface NodeUpdate {
    id: string;
    line: number;
    kind: 'texture' | 'value' | 'value_array' | 'geometry';
    preview?: string;  // base64 image for textures
    value?: number;    // single value
    values?: number[]; // value array / history
}

export interface RuntimeMessage {
    type: string;
    nodes?: NodeUpdate[];
    success?: boolean;
    message?: string;
}

type Callback<T> = (data: T) => void;

export class RuntimeClient {
    private ws: WebSocket | null = null;
    private port: number;
    private reconnectTimer: NodeJS.Timeout | null = null;
    private reconnectAttempts = 0;
    private maxReconnectAttempts = 10;
    
    private connectedCallbacks: Callback<void>[] = [];
    private disconnectedCallbacks: Callback<void>[] = [];
    private nodeUpdateCallbacks: Callback<NodeUpdate[]>[] = [];
    private compileStatusCallbacks: Callback<[boolean, string]>[] = [];
    private errorCallbacks: Callback<string>[] = [];
    
    constructor(port: number) {
        this.port = port;
    }
    
    connect() {
        this.cleanup();
        
        try {
            this.ws = new WebSocket(`ws://localhost:${this.port}`);
            
            this.ws.on('open', () => {
                this.reconnectAttempts = 0;
                this.connectedCallbacks.forEach(cb => cb());
            });
            
            this.ws.on('message', (data: WebSocket.Data) => {
                this.handleMessage(data.toString());
            });
            
            this.ws.on('close', () => {
                this.disconnectedCallbacks.forEach(cb => cb());
                this.scheduleReconnect();
            });
            
            this.ws.on('error', (err: Error) => {
                this.errorCallbacks.forEach(cb => cb(err.message));
            });
        } catch (e) {
            this.scheduleReconnect();
        }
    }
    
    disconnect() {
        this.cleanup();
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
    }
    
    private cleanup() {
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
    }
    
    private scheduleReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            return;
        }
        
        if (!this.reconnectTimer) {
            const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts), 30000);
            this.reconnectTimer = setTimeout(() => {
                this.reconnectTimer = null;
                this.reconnectAttempts++;
                this.connect();
            }, delay);
        }
    }
    
    private handleMessage(data: string) {
        try {
            const msg: RuntimeMessage = JSON.parse(data);
            
            switch (msg.type) {
                case 'node_update':
                    if (msg.nodes) {
                        this.nodeUpdateCallbacks.forEach(cb => cb(msg.nodes!));
                    }
                    break;
                    
                case 'compile_status':
                    this.compileStatusCallbacks.forEach(cb => 
                        cb([msg.success ?? false, msg.message ?? '']));
                    break;
                    
                case 'error':
                    this.errorCallbacks.forEach(cb => cb(msg.message ?? 'Unknown error'));
                    break;
            }
        } catch (e) {
            this.errorCallbacks.forEach(cb => cb(`Parse error: ${e}`));
        }
    }
    
    // Event subscriptions
    onConnected(callback: Callback<void>) { this.connectedCallbacks.push(callback); }
    onDisconnected(callback: Callback<void>) { this.disconnectedCallbacks.push(callback); }
    onNodeUpdate(callback: Callback<NodeUpdate[]>) { this.nodeUpdateCallbacks.push(callback); }
    onCompileStatus(callback: (success: boolean, message: string) => void) { 
        this.compileStatusCallbacks.push(([s, m]) => callback(s, m)); 
    }
    onError(callback: Callback<string>) { this.errorCallbacks.push(callback); }
    
    // Commands to runtime
    sendReload() {
        this.send({ type: 'reload' });
    }
    
    sendParamChange(node: string, param: string, value: number | string | boolean) {
        this.send({ type: 'param_change', node, param, value });
    }
    
    sendPause(paused: boolean) {
        this.send({ type: 'pause', paused });
    }
    
    private send(msg: object) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(msg));
        }
    }
}
```

---

## Inline Decorations

### extension/src/decorations.ts

```typescript
import * as vscode from 'vscode';
import * as path from 'path';
import { NodeUpdate } from './runtimeClient';

export class DecorationManager {
    private nodes: Map<string, NodeUpdate> = new Map();
    private lineToNode: Map<number, NodeUpdate> = new Map();
    private decorationType: vscode.TextEditorDecorationType;
    private enabled: boolean = true;
    private context: vscode.ExtensionContext;
    
    constructor(context: vscode.ExtensionContext) {
        this.context = context;
        
        this.decorationType = vscode.window.createTextEditorDecorationType({
            after: {
                margin: '0 0 0 2em',
            }
        });
        
        // Listen for config changes
        vscode.workspace.onDidChangeConfiguration(e => {
            if (e.affectsConfiguration('vivid.showInlineDecorations')) {
                const config = vscode.workspace.getConfiguration('vivid');
                this.enabled = config.get<boolean>('showInlineDecorations') ?? true;
                
                const editor = vscode.window.activeTextEditor;
                if (editor) {
                    this.updateDecorations(editor);
                }
            }
        });
    }
    
    setEnabled(enabled: boolean) {
        this.enabled = enabled;
        const editor = vscode.window.activeTextEditor;
        if (editor) {
            this.updateDecorations(editor);
        }
    }
    
    updateNodes(nodes: NodeUpdate[]) {
        this.nodes.clear();
        this.lineToNode.clear();
        
        for (const node of nodes) {
            this.nodes.set(node.id, node);
            this.lineToNode.set(node.line, node);
        }
    }
    
    getNodeLine(nodeId: string): number | undefined {
        return this.nodes.get(nodeId)?.line;
    }
    
    updateDecorations(editor: vscode.TextEditor) {
        if (!this.enabled) {
            editor.setDecorations(this.decorationType, []);
            return;
        }
        
        const decorations: vscode.DecorationOptions[] = [];
        const config = vscode.workspace.getConfiguration('vivid');
        const previewSize = config.get<number>('previewSize') ?? 48;
        
        for (const [line, node] of this.lineToNode) {
            const lineIndex = line - 1;  // VS Code is 0-indexed
            if (lineIndex < 0 || lineIndex >= editor.document.lineCount) continue;
            
            const lineText = editor.document.lineAt(lineIndex);
            const range = new vscode.Range(
                lineIndex, lineText.text.length,
                lineIndex, lineText.text.length
            );
            
            const decoration = this.createDecoration(node, range, previewSize);
            if (decoration) {
                decorations.push(decoration);
            }
        }
        
        editor.setDecorations(this.decorationType, decorations);
    }
    
    private createDecoration(
        node: NodeUpdate, 
        range: vscode.Range,
        previewSize: number
    ): vscode.DecorationOptions | null {
        let contentText = '';
        let hoverContent: vscode.MarkdownString;
        
        switch (node.kind) {
            case 'value':
                contentText = `∿ ${node.value?.toFixed(2) ?? '?'}`;
                hoverContent = new vscode.MarkdownString();
                hoverContent.appendMarkdown(`**${node.id}**\n\nValue: \`${node.value?.toFixed(6)}\``);
                break;
                
            case 'value_array':
                const vals = node.values?.slice(0, 4).map(v => v.toFixed(1)).join(', ') ?? '';
                const more = (node.values?.length ?? 0) > 4 ? '...' : '';
                contentText = `[${vals}${more}]`;
                hoverContent = this.createSparklineHover(node);
                break;
                
            case 'texture':
                contentText = '🖼';
                hoverContent = this.createTextureHover(node, previewSize);
                break;
                
            case 'geometry':
                contentText = '📐';
                hoverContent = new vscode.MarkdownString(`**${node.id}** (geometry)`);
                break;
                
            default:
                return null;
        }
        
        return {
            range,
            renderOptions: {
                after: {
                    contentText,
                    color: new vscode.ThemeColor('editorCodeLens.foreground'),
                    fontStyle: 'italic',
                    margin: '0 0 0 2em'
                }
            },
            hoverMessage: hoverContent
        };
    }
    
    private createTextureHover(node: NodeUpdate, size: number): vscode.MarkdownString {
        const md = new vscode.MarkdownString();
        md.isTrusted = true;
        md.supportHtml = true;
        
        md.appendMarkdown(`**${node.id}** (texture)\n\n`);
        
        if (node.preview) {
            // Embed base64 image directly
            md.appendMarkdown(`<img src="${node.preview}" width="${size * 3}" />`);
        } else {
            md.appendMarkdown('*No preview available*');
        }
        
        return md;
    }
    
    private createSparklineHover(node: NodeUpdate): vscode.MarkdownString {
        const md = new vscode.MarkdownString();
        md.isTrusted = true;
        md.supportHtml = true;
        
        md.appendMarkdown(`**${node.id}** (values)\n\n`);
        
        if (node.values && node.values.length > 0) {
            // Create inline SVG sparkline
            const width = 200;
            const height = 40;
            const values = node.values;
            const min = Math.min(...values);
            const max = Math.max(...values);
            const range = max - min || 1;
            
            const points = values.map((v, i) => {
                const x = (i / (values.length - 1)) * width;
                const y = height - ((v - min) / range) * (height - 4) - 2;
                return `${x},${y}`;
            }).join(' ');
            
            const svg = `<svg width="${width}" height="${height}" xmlns="http://www.w3.org/2000/svg">
                <polyline points="${points}" fill="none" stroke="#4EC9B0" stroke-width="1.5"/>
            </svg>`;
            
            md.appendMarkdown(svg);
            md.appendMarkdown(`\n\nCurrent: \`${values[values.length - 1]?.toFixed(4)}\``);
            md.appendMarkdown(`\nRange: \`${min.toFixed(2)}\` to \`${max.toFixed(2)}\``);
        }
        
        return md;
    }
    
    dispose() {
        this.decorationType.dispose();
    }
}
```

---

## Preview Panel

### extension/src/previewPanel.ts

```typescript
import * as vscode from 'vscode';
import { NodeUpdate } from './runtimeClient';

export class PreviewPanel {
    private panel: vscode.WebviewPanel | undefined;
    private nodes: NodeUpdate[] = [];
    private onNodeClick: (nodeId: string) => void;
    private extensionUri: vscode.Uri;
    
    constructor(extensionUri: vscode.Uri, onNodeClick: (nodeId: string) => void) {
        this.extensionUri = extensionUri;
        this.onNodeClick = onNodeClick;
    }
    
    reveal() {
        if (this.panel) {
            this.panel.reveal();
            return;
        }
        
        this.panel = vscode.window.createWebviewPanel(
            'vividPreview',
            'Vivid Previews',
            vscode.ViewColumn.Beside,
            {
                enableScripts: true,
                retainContextWhenHidden: true,
                localResourceRoots: [this.extensionUri]
            }
        );
        
        this.panel.onDidDispose(() => {
            this.panel = undefined;
        });
        
        this.panel.webview.onDidReceiveMessage(message => {
            if (message.type === 'nodeClick') {
                this.onNodeClick(message.nodeId);
            }
        });
        
        this.updateContent();
    }
    
    updateNodes(nodes: NodeUpdate[]) {
        this.nodes = nodes;
        this.updateContent();
    }
    
    private updateContent() {
        if (!this.panel) return;
        this.panel.webview.html = this.getHtml();
    }
    
    private getHtml(): string {
        const nodeCards = this.nodes.map(node => this.renderNode(node)).join('');
        
        return `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Vivid Previews</title>
    <style>
        :root {
            --bg: var(--vscode-editor-background);
            --fg: var(--vscode-editor-foreground);
            --card-bg: var(--vscode-editorWidget-background);
            --border: var(--vscode-widget-border);
            --hover: var(--vscode-list-hoverBackground);
            --accent: var(--vscode-textLink-foreground);
        }
        
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }
        
        body {
            font-family: var(--vscode-font-family);
            font-size: var(--vscode-font-size);
            background: var(--bg);
            color: var(--fg);
            padding: 12px;
        }
        
        .container {
            display: flex;
            flex-direction: column;
            gap: 12px;
        }
        
        .node-card {
            background: var(--card-bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 12px;
            cursor: pointer;
            transition: background 0.15s ease;
        }
        
        .node-card:hover {
            background: var(--hover);
        }
        
        .node-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 8px;
        }
        
        .node-id {
            font-weight: 600;
            color: var(--accent);
        }
        
        .node-line {
            font-size: 0.85em;
            opacity: 0.6;
        }
        
        .node-kind {
            font-size: 0.75em;
            text-transform: uppercase;
            opacity: 0.5;
            letter-spacing: 0.5px;
        }
        
        .node-preview {
            margin-top: 8px;
        }
        
        .node-preview img {
            max-width: 100%;
            border-radius: 4px;
            display: block;
        }
        
        .node-value {
            font-family: var(--vscode-editor-font-family);
            font-size: 1.5em;
            font-weight: 300;
        }
        
        .sparkline {
            width: 100%;
            height: 32px;
        }
        
        .sparkline polyline {
            fill: none;
            stroke: var(--accent);
            stroke-width: 1.5;
        }
        
        .empty-state {
            text-align: center;
            padding: 40px 20px;
            opacity: 0.6;
        }
    </style>
</head>
<body>
    <div class="container">
        ${nodeCards || '<div class="empty-state">No nodes yet.<br>Start the Vivid runtime to see previews.</div>'}
    </div>
    
    <script>
        const vscode = acquireVsCodeApi();
        
        document.querySelectorAll('.node-card').forEach(card => {
            card.addEventListener('click', () => {
                vscode.postMessage({
                    type: 'nodeClick',
                    nodeId: card.dataset.nodeId
                });
            });
        });
    </script>
</body>
</html>`;
    }
    
    private renderNode(node: NodeUpdate): string {
        let preview = '';
        
        switch (node.kind) {
            case 'texture':
                if (node.preview) {
                    preview = `<div class="node-preview"><img src="${node.preview}" alt="${node.id}"></div>`;
                }
                break;
                
            case 'value':
                preview = `<div class="node-preview"><span class="node-value">${node.value?.toFixed(4) ?? '—'}</span></div>`;
                break;
                
            case 'value_array':
                preview = `<div class="node-preview">${this.renderSparkline(node.values || [])}</div>`;
                break;
                
            case 'geometry':
                preview = `<div class="node-preview"><span class="node-value">📐</span></div>`;
                break;
        }
        
        return `
            <div class="node-card" data-node-id="${node.id}">
                <div class="node-header">
                    <span class="node-id">${node.id}</span>
                    <span class="node-line">:${node.line}</span>
                </div>
                <div class="node-kind">${node.kind}</div>
                ${preview}
            </div>
        `;
    }
    
    private renderSparkline(values: number[]): string {
        if (values.length === 0) return '';
        
        const width = 200;
        const height = 32;
        const min = Math.min(...values);
        const max = Math.max(...values);
        const range = max - min || 1;
        
        const points = values.map((v, i) => {
            const x = (i / Math.max(values.length - 1, 1)) * width;
            const y = height - ((v - min) / range) * (height - 4) - 2;
            return `${x},${y}`;
        }).join(' ');
        
        return `
            <svg class="sparkline" viewBox="0 0 ${width} ${height}" preserveAspectRatio="none">
                <polyline points="${points}"/>
            </svg>
        `;
    }
}
```

---

## Status Bar

### extension/src/statusBar.ts

```typescript
import * as vscode from 'vscode';

export class StatusBar implements vscode.Disposable {
    private statusBarItem: vscode.StatusBarItem;
    private connected: boolean = false;
    private compileSuccess: boolean = true;
    
    constructor() {
        this.statusBarItem = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Left,
            100
        );
        this.statusBarItem.command = 'vivid.showPreviewPanel';
        this.update();
        this.statusBarItem.show();
    }
    
    setConnected(connected: boolean) {
        this.connected = connected;
        this.update();
    }
    
    setCompileStatus(success: boolean) {
        this.compileSuccess = success;
        this.update();
    }
    
    private update() {
        if (!this.connected) {
            this.statusBarItem.text = '$(debug-disconnect) Vivid';
            this.statusBarItem.tooltip = 'Not connected to runtime';
            this.statusBarItem.backgroundColor = undefined;
        } else if (!this.compileSuccess) {
            this.statusBarItem.text = '$(error) Vivid';
            this.statusBarItem.tooltip = 'Compile error';
            this.statusBarItem.backgroundColor = new vscode.ThemeColor('statusBarItem.errorBackground');
        } else {
            this.statusBarItem.text = '$(pulse) Vivid';
            this.statusBarItem.tooltip = 'Connected to runtime';
            this.statusBarItem.backgroundColor = undefined;
        }
    }
    
    dispose() {
        this.statusBarItem.dispose();
    }
}
```

---

## Preview Server (Runtime Side)

This is the WebSocket server implementation for the runtime (C++ side), referenced in Part 2:

### runtime/src/preview_server.h

```cpp
#pragma once
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>
#include <vivid/types.h>
#include <vector>
#include <string>
#include <mutex>

namespace vivid {

struct NodePreview {
    std::string id;
    int sourceLine;
    OutputKind kind;
    std::string base64Image;   // For textures (JPEG base64)
    float value;               // For single values
    std::vector<float> values; // For value arrays
};

class PreviewServer {
public:
    explicit PreviewServer(int port);
    ~PreviewServer();
    
    void start();
    void stop();
    
    // Send updates to all connected clients
    void sendNodeUpdates(const std::vector<NodePreview>& previews);
    void sendCompileStatus(bool success, const std::string& message);
    void sendError(const std::string& error);
    
    // Set callback for incoming commands
    using CommandCallback = std::function<void(const std::string& type, const nlohmann::json& data)>;
    void setCommandCallback(CommandCallback callback) { commandCallback_ = callback; }
    
private:
    void onMessage(std::shared_ptr<ix::ConnectionState> state,
                   ix::WebSocket& ws,
                   const ix::WebSocketMessagePtr& msg);
    void broadcast(const std::string& message);
    
    ix::WebSocketServer server_;
    std::mutex mutex_;
    bool running_ = false;
    CommandCallback commandCallback_;
};

} // namespace vivid
```

### runtime/src/preview_server.cpp

```cpp
#include "preview_server.h"
#include <iostream>

namespace vivid {

PreviewServer::PreviewServer(int port)
    : server_(port, "0.0.0.0") {
    
    server_.setOnClientMessageCallback(
        [this](auto state, auto& ws, auto msg) {
            onMessage(state, ws, msg);
        }
    );
}

PreviewServer::~PreviewServer() {
    stop();
}

void PreviewServer::start() {
    auto res = server_.listen();
    if (!res.first) {
        std::cerr << "[PreviewServer] Failed to start: " << res.second << "\n";
        return;
    }
    server_.start();
    running_ = true;
    std::cout << "[PreviewServer] Listening on port " << server_.getPort() << "\n";
}

void PreviewServer::stop() {
    if (running_) {
        server_.stop();
        running_ = false;
    }
}

void PreviewServer::sendNodeUpdates(const std::vector<NodePreview>& previews) {
    nlohmann::json msg;
    msg["type"] = "node_update";
    msg["nodes"] = nlohmann::json::array();
    
    for (const auto& preview : previews) {
        nlohmann::json node;
        node["id"] = preview.id;
        node["line"] = preview.sourceLine;
        
        switch (preview.kind) {
            case OutputKind::Texture:
                node["kind"] = "texture";
                if (!preview.base64Image.empty()) {
                    node["preview"] = preview.base64Image;
                }
                break;
                
            case OutputKind::Value:
                node["kind"] = "value";
                node["value"] = preview.value;
                break;
                
            case OutputKind::ValueArray:
                node["kind"] = "value_array";
                node["values"] = preview.values;
                break;
                
            case OutputKind::Geometry:
                node["kind"] = "geometry";
                break;
        }
        
        msg["nodes"].push_back(node);
    }
    
    broadcast(msg.dump());
}

void PreviewServer::sendCompileStatus(bool success, const std::string& message) {
    nlohmann::json msg;
    msg["type"] = "compile_status";
    msg["success"] = success;
    msg["message"] = message;
    
    broadcast(msg.dump());
}

void PreviewServer::sendError(const std::string& error) {
    nlohmann::json msg;
    msg["type"] = "error";
    msg["message"] = error;
    
    broadcast(msg.dump());
}

void PreviewServer::onMessage(std::shared_ptr<ix::ConnectionState> state,
                               ix::WebSocket& ws,
                               const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
        std::cout << "[PreviewServer] Client connected\n";
    }
    else if (msg->type == ix::WebSocketMessageType::Close) {
        std::cout << "[PreviewServer] Client disconnected\n";
    }
    else if (msg->type == ix::WebSocketMessageType::Message) {
        try {
            auto json = nlohmann::json::parse(msg->str);
            std::string type = json["type"];
            
            if (commandCallback_) {
                commandCallback_(type, json);
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[PreviewServer] Parse error: " << e.what() << "\n";
        }
    }
}

void PreviewServer::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& client : server_.getClients()) {
        client->send(message);
    }
}

} // namespace vivid
```

---

## Async Shared Memory Preview Architecture

The WebSocket-based preview system works for basic use cases, but has scalability limitations:
- GPU readback (`readTexturePixels`) blocks the main render thread
- Base64 encoding inflates image data by ~33%
- JSON parsing overhead for large preview payloads
- Linear scaling: more operators = more blocking readbacks per frame

### Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        MAIN RENDER THREAD                        │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐                      │
│  │ Op1     │───▶│ Op2     │───▶│ Op3     │───▶ Display          │
│  │ process │    │ process │    │ process │                      │
│  └────┬────┘    └────┬────┘    └────┬────┘                      │
│       │              │              │                            │
│       ▼              ▼              ▼                            │
│  Queue async    Queue async    Queue async   ◀── non-blocking   │
│  readback       readback       readback                         │
└─────────────────────────────────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────────┐
│                      PREVIEW THREAD                              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐       │
│  │ Wait for GPU │───▶│ Downsample   │───▶│ Write to     │       │
│  │ fence/callback│    │ to thumbnail │    │ shared mem   │       │
│  └──────────────┘    └──────────────┘    └──────────────┘       │
│                                                 │                │
│                                                 ▼                │
│                                          ┌──────────────┐       │
│                                          │ Signal ready │       │
│                                          │ (WebSocket)  │       │
│                                          └──────────────┘       │
└─────────────────────────────────────────────────────────────────┘
                       │
        ───────────────┴───────────────
       │                               │
       ▼                               ▼
┌─────────────────┐           ┌─────────────────┐
│  SHARED MEMORY  │           │    WEBSOCKET    │
│  ┌───────────┐  │           │  (metadata only)│
│  │ Op1: RGB  │  │           │  {              │
│  │ 128x128   │  │           │    "op1": {     │
│  ├───────────┤  │           │      "ready": 1,│
│  │ Op2: RGB  │  │           │      "frame": 42│
│  │ 128x128   │  │           │    }            │
│  ├───────────┤  │           │  }              │
│  │ Op3: RGB  │  │           │                 │
│  │ 128x128   │  │           │                 │
│  └───────────┘  │           │                 │
└─────────────────┘           └─────────────────┘
        │                               │
        └───────────────┬───────────────┘
                        ▼
              ┌─────────────────┐
              │  VS CODE EXT    │
              │  (reads mmap    │
              │   on metadata   │
              │   notification) │
              └─────────────────┘
```

### Key Components

#### 1. Async GPU Readback

Instead of blocking `wgpuDevicePoll`, use `wgpuBufferMapAsync` with callbacks:

```cpp
// runtime/src/async_readback.h
#pragma once
#include <webgpu/webgpu.h>
#include <functional>
#include <queue>
#include <mutex>

namespace vivid {

struct ReadbackRequest {
    WGPUBuffer stagingBuffer;
    int width, height;
    std::string operatorId;
    int frameNumber;
    std::function<void(const uint8_t* data, size_t size)> callback;
};

class AsyncReadback {
public:
    AsyncReadback(WGPUDevice device, WGPUQueue queue);
    ~AsyncReadback();

    // Queue a texture for async readback (non-blocking)
    void queueReadback(const Texture& texture, const std::string& operatorId,
                       int frameNumber, std::function<void(const uint8_t*, size_t)> callback);

    // Poll for completed readbacks (call from preview thread)
    void pollCompletions();

    // Get number of pending readbacks
    size_t pendingCount() const;

private:
    WGPUDevice device_;
    WGPUQueue queue_;

    std::queue<ReadbackRequest> pendingRequests_;
    std::queue<ReadbackRequest> completedRequests_;
    std::mutex mutex_;

    // Ring buffer of staging buffers to avoid allocation per frame
    static constexpr int RING_BUFFER_SIZE = 8;
    WGPUBuffer stagingBuffers_[RING_BUFFER_SIZE];
    int nextBuffer_ = 0;
};

} // namespace vivid
```

#### 2. Shared Memory Segment

Platform-specific shared memory with a fixed layout:

```cpp
// runtime/src/shared_preview.h
#pragma once
#include <string>
#include <cstdint>

namespace vivid {

// Fixed thumbnail size for predictable memory layout
constexpr int THUMB_WIDTH = 128;
constexpr int THUMB_HEIGHT = 128;
constexpr int THUMB_CHANNELS = 3;  // RGB
constexpr int THUMB_SIZE = THUMB_WIDTH * THUMB_HEIGHT * THUMB_CHANNELS;
constexpr int MAX_OPERATORS = 64;

// Header at start of shared memory
struct SharedPreviewHeader {
    uint32_t magic;              // 'VIVD' for validation
    uint32_t version;            // Protocol version
    uint32_t operatorCount;      // Number of active operators
    uint32_t frameNumber;        // Current frame for sync
    uint64_t timestamp;          // Microseconds since epoch
};

// Per-operator slot in shared memory
struct SharedPreviewSlot {
    char operatorId[64];         // Operator name (null-terminated)
    int32_t sourceLine;          // Source file line number
    uint32_t frameNumber;        // Frame when this was captured
    uint32_t width, height;      // Original texture dimensions
    uint8_t kind;                // OutputKind enum
    uint8_t ready;               // 1 if data is valid
    uint8_t padding[2];

    union {
        uint8_t pixels[THUMB_SIZE];   // RGB thumbnail data
        float value;                   // For Value outputs
        float values[256];             // For ValueArray outputs
    } data;
};

// Total shared memory layout
struct SharedPreviewMemory {
    SharedPreviewHeader header;
    SharedPreviewSlot slots[MAX_OPERATORS];
};

class SharedPreview {
public:
    SharedPreview();
    ~SharedPreview();

    // Create/open shared memory (returns false on failure)
    bool create(const std::string& name);  // Runtime calls this
    bool open(const std::string& name);    // Extension calls this
    void close();

    // Access the shared memory
    SharedPreviewMemory* memory() { return memory_; }
    const SharedPreviewMemory* memory() const { return memory_; }

    // Convenience methods
    void updateSlot(int index, const std::string& operatorId, int sourceLine,
                    int width, int height, const uint8_t* rgbPixels);
    void updateSlotValue(int index, const std::string& operatorId, int sourceLine, float value);
    void setOperatorCount(int count);
    void incrementFrame();

private:
    void* handle_ = nullptr;        // Platform-specific handle
    SharedPreviewMemory* memory_ = nullptr;
    std::string name_;
    bool isCreator_ = false;
};

} // namespace vivid
```

#### 3. Platform-Specific Implementation

```cpp
// runtime/src/shared_preview_unix.cpp (macOS/Linux)
#include "shared_preview.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace vivid {

bool SharedPreview::create(const std::string& name) {
    name_ = "/" + name;  // POSIX shared memory names must start with /
    isCreator_ = true;

    // Remove any existing segment
    shm_unlink(name_.c_str());

    // Create shared memory
    int fd = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd < 0) return false;

    // Set size
    if (ftruncate(fd, sizeof(SharedPreviewMemory)) < 0) {
        ::close(fd);
        shm_unlink(name_.c_str());
        return false;
    }

    // Map into address space
    memory_ = static_cast<SharedPreviewMemory*>(
        mmap(nullptr, sizeof(SharedPreviewMemory), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
    );
    ::close(fd);  // Can close fd after mmap

    if (memory_ == MAP_FAILED) {
        memory_ = nullptr;
        shm_unlink(name_.c_str());
        return false;
    }

    // Initialize header
    std::memset(memory_, 0, sizeof(SharedPreviewMemory));
    memory_->header.magic = 0x56495644;  // 'VIVD'
    memory_->header.version = 1;

    return true;
}

bool SharedPreview::open(const std::string& name) {
    name_ = "/" + name;
    isCreator_ = false;

    int fd = shm_open(name_.c_str(), O_RDONLY, 0);
    if (fd < 0) return false;

    memory_ = static_cast<SharedPreviewMemory*>(
        mmap(nullptr, sizeof(SharedPreviewMemory), PROT_READ, MAP_SHARED, fd, 0)
    );
    ::close(fd);

    if (memory_ == MAP_FAILED) {
        memory_ = nullptr;
        return false;
    }

    // Validate magic
    if (memory_->header.magic != 0x56495644) {
        munmap(memory_, sizeof(SharedPreviewMemory));
        memory_ = nullptr;
        return false;
    }

    return true;
}

void SharedPreview::close() {
    if (memory_) {
        munmap(memory_, sizeof(SharedPreviewMemory));
        memory_ = nullptr;
    }
    if (isCreator_ && !name_.empty()) {
        shm_unlink(name_.c_str());
    }
}

} // namespace vivid
```

#### 4. Preview Thread

Separate thread that handles async readback completion and shared memory updates:

```cpp
// runtime/src/preview_thread.h
#pragma once
#include "async_readback.h"
#include "shared_preview.h"
#include "preview_server.h"
#include <thread>
#include <atomic>

namespace vivid {

class PreviewThread {
public:
    PreviewThread(WGPUDevice device, WGPUQueue queue, PreviewServer& server);
    ~PreviewThread();

    // Start/stop the preview thread
    void start(const std::string& sharedMemName);
    void stop();

    // Queue a preview capture (called from main thread, non-blocking)
    void queuePreview(const Texture& texture, const std::string& operatorId,
                      int sourceLine, int slotIndex);
    void queueValuePreview(const std::string& operatorId, int sourceLine,
                           int slotIndex, float value);

    // Signal frame complete (increments frame counter, sends WebSocket notification)
    void frameComplete();

private:
    void threadMain();

    AsyncReadback readback_;
    SharedPreview sharedMem_;
    PreviewServer& server_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> frameNumber_{0};
};

} // namespace vivid
```

#### 5. VS Code Extension Changes

The extension needs a native module to read shared memory:

```typescript
// extension/src/sharedPreview.ts
import * as path from 'path';

// Native module for shared memory access (compiled with node-gyp)
interface SharedPreviewNative {
    open(name: string): boolean;
    close(): void;
    getHeader(): { magic: number; version: number; operatorCount: number; frameNumber: number };
    getSlot(index: number): {
        operatorId: string;
        sourceLine: number;
        frameNumber: number;
        width: number;
        height: number;
        kind: number;
        ready: boolean;
        pixels?: Buffer;  // For textures
        value?: number;   // For values
    } | null;
}

let native: SharedPreviewNative | null = null;

export function initSharedPreview(): boolean {
    try {
        // Load native module (built with node-gyp)
        native = require('../build/Release/shared_preview_native.node');
        return native.open('vivid_preview');
    } catch (e) {
        console.error('Failed to load shared preview native module:', e);
        return false;
    }
}

export function closeSharedPreview(): void {
    if (native) {
        native.close();
        native = null;
    }
}

export function readPreviewSlot(index: number): NodeUpdate | null {
    if (!native) return null;

    const slot = native.getSlot(index);
    if (!slot || !slot.ready) return null;

    const update: NodeUpdate = {
        id: slot.operatorId,
        line: slot.sourceLine,
        kind: ['texture', 'value', 'value_array', 'geometry'][slot.kind] as any,
    };

    if (slot.kind === 0 && slot.pixels) {
        // Convert RGB pixels to base64 JPEG (or use raw for canvas)
        update.preview = `data:image/raw;base64,${slot.pixels.toString('base64')}`;
        update.width = slot.width;
        update.height = slot.height;
    } else if (slot.kind === 1) {
        update.value = slot.value;
    }

    return update;
}
```

### WebSocket Protocol Changes

With shared memory, WebSocket only carries metadata:

```typescript
// New lightweight message format
interface PreviewMetadata {
    type: 'preview_ready';
    frame: number;
    operators: {
        id: string;
        slot: number;      // Index in shared memory
        updated: boolean;  // True if changed since last frame
    }[];
}
```

### Migration Path

1. **Phase 1**: Implement async readback (non-blocking GPU reads)
2. **Phase 2**: Add preview thread (decouple from render loop)
3. **Phase 3**: Implement shared memory (zero-copy to extension)
4. **Phase 4**: Update extension with native module
5. **Phase 5**: Deprecate base64 WebSocket previews

### Performance Comparison

| Metric | Current (WebSocket) | Shared Memory |
|--------|---------------------|---------------|
| GPU readback | Blocking | Async (non-blocking) |
| Data transfer | ~33% overhead (base64) | Zero-copy |
| Latency | ~10-50ms | ~1-5ms |
| CPU overhead | JSON encode/decode | Direct memory access |
| Max operators | ~10-20 at 60fps | 64+ at 60fps |

---

## Build and Run Instructions

### Building the Extension

```bash
cd extension
npm install
npm run compile
```

### Testing in VS Code

1. Open the `extension` folder in VS Code
2. Press F5 to launch Extension Development Host
3. In the new VS Code window, open a Vivid project folder
4. The extension will auto-connect if the runtime is running

### Installing the Extension

```bash
cd extension
npx vsce package
code --install-extension vivid-0.1.0.vsix
```

---

## WebSocket Protocol Summary

### Runtime → Extension

| Message Type | Fields | Description |
|--------------|--------|-------------|
| `node_update` | `nodes[]` | Live preview data for all nodes |
| `compile_status` | `success`, `message` | Compilation result |
| `error` | `message` | Runtime error |

### Extension → Runtime

| Message Type | Fields | Description |
|--------------|--------|-------------|
| `reload` | — | Force recompile and reload |
| `pause` | `paused` | Pause/resume execution |
| `param_change` | `node`, `param`, `value` | Live parameter update |

---

## Complete Project Checklist

After implementing all four parts, you should have:

- [ ] Root `CMakeLists.txt` with FetchContent for all dependencies
- [ ] `runtime/` with WebGPU renderer, hot-loader, preview server
- [ ] `operators/` with built-in texture and value operators
- [ ] `shaders/` with WGSL shaders for all operators
- [ ] `extension/` with VS Code extension for inline previews
- [ ] `examples/hello/` with a working example project

### Quick Test

```bash
# Build everything
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run example
./build/bin/vivid-runtime examples/hello

# In another terminal, open VS Code
code examples/hello
```

You should see:
1. A window with animated noise visuals
2. VS Code with inline preview indicators next to each `NODE()` call
3. Hover over indicators to see live texture previews
4. Edit `chain.cpp`, save, and watch it hot-reload
