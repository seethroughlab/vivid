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
                preview = `<div class="node-preview"><span class="node-value">${node.value?.toFixed(4) ?? '-'}</span></div>`;
                break;

            case 'value_array':
                preview = `<div class="node-preview">${this.renderSparkline(node.values || [])}</div>`;
                break;

            case 'geometry':
                preview = `<div class="node-preview"><span class="node-value">[geo]</span></div>`;
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
