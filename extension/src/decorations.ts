import * as vscode from 'vscode';
import { NodeUpdate } from './runtimeClient';

export class DecorationManager {
    private nodes: Map<string, NodeUpdate> = new Map();
    private lineToNode: Map<number, NodeUpdate> = new Map();
    private decorationType: vscode.TextEditorDecorationType;
    private enabled: boolean = true;

    constructor(context: vscode.ExtensionContext) {
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
                contentText = `~ ${node.value?.toFixed(2) ?? '?'}`;
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
                contentText = '[img]';
                hoverContent = this.createTextureHover(node, previewSize);
                break;

            case 'geometry':
                contentText = '[geo]';
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
