import * as vscode from 'vscode';
import { NodeUpdate } from './runtimeClient';
import { OperatorData, ParamData } from './operatorTreeView';

export class DecorationManager {
    // Legacy node update support
    private nodes: Map<string, NodeUpdate> = new Map();
    private lineToNode: Map<number, NodeUpdate> = new Map();

    // New operator/param support
    private operators: Map<string, OperatorData> = new Map();
    private params: Map<string, ParamData[]> = new Map();  // operator name -> params
    private lineToOperator: Map<number, OperatorData> = new Map();

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

    // Legacy node update support
    updateNodes(nodes: NodeUpdate[]) {
        this.nodes.clear();
        this.lineToNode.clear();

        for (const node of nodes) {
            this.nodes.set(node.id, node);
            this.lineToNode.set(node.line, node);
        }
    }

    // New: update from operator list
    updateOperators(operators: OperatorData[]) {
        this.operators.clear();
        this.lineToOperator.clear();

        for (const op of operators) {
            this.operators.set(op.name, op);
            if (op.sourceLine > 0) {
                this.lineToOperator.set(op.sourceLine, op);
            }
        }

        const editor = vscode.window.activeTextEditor;
        if (editor && this.isVividFile(editor.document)) {
            this.updateDecorations(editor);
        }
    }

    // New: update from param values
    updateParams(params: ParamData[]) {
        this.params.clear();

        for (const p of params) {
            if (!this.params.has(p.operator)) {
                this.params.set(p.operator, []);
            }
            this.params.get(p.operator)!.push(p);
        }

        const editor = vscode.window.activeTextEditor;
        if (editor && this.isVividFile(editor.document)) {
            this.updateDecorations(editor);
        }
    }

    private isVividFile(document: vscode.TextDocument): boolean {
        return document.fileName.endsWith('chain.cpp') ||
               document.getText().includes('vivid/vivid.h');
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

        // Legacy node decorations
        for (const [line, node] of this.lineToNode) {
            const lineIndex = line - 1;
            if (lineIndex < 0 || lineIndex >= editor.document.lineCount) continue;

            const lineText = editor.document.lineAt(lineIndex);
            const range = new vscode.Range(
                lineIndex, lineText.text.length,
                lineIndex, lineText.text.length
            );

            const decoration = this.createNodeDecoration(node, range, previewSize);
            if (decoration) {
                decorations.push(decoration);
            }
        }

        // New operator decorations
        for (const [line, op] of this.lineToOperator) {
            const lineIndex = line - 1;
            if (lineIndex < 0 || lineIndex >= editor.document.lineCount) continue;

            // Skip if we already have a legacy node decoration for this line
            if (this.lineToNode.has(line)) continue;

            const lineText = editor.document.lineAt(lineIndex);
            const range = new vscode.Range(
                lineIndex, lineText.text.length,
                lineIndex, lineText.text.length
            );

            const opParams = this.params.get(op.name) || [];
            const decoration = this.createOperatorDecoration(op, opParams, range);
            if (decoration) {
                decorations.push(decoration);
            }
        }

        editor.setDecorations(this.decorationType, decorations);
    }

    private createNodeDecoration(
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

    private createOperatorDecoration(
        op: OperatorData,
        params: ParamData[],
        range: vscode.Range
    ): vscode.DecorationOptions | null {
        // Build inline text based on output type
        let contentText = '';
        switch (op.outputType) {
            case 'Texture':
                contentText = '[tex]';
                break;
            case 'Geometry':
                contentText = '[geo]';
                break;
            case 'Value':
                // Show the value if we have a param with the same name or "value"
                const valueParam = params.find(p => p.name === 'value' || p.name === op.name);
                if (valueParam) {
                    contentText = `~ ${valueParam.value[0].toFixed(2)}`;
                } else {
                    contentText = '[val]';
                }
                break;
            case 'ValueArray':
                contentText = '[arr]';
                break;
            case 'Camera':
                contentText = '[cam]';
                break;
            case 'Light':
                contentText = '[lit]';
                break;
            case 'Audio':
                contentText = '[aud]';
                break;
            case 'AudioValue':
                contentText = '[lvl]';
                break;
            default:
                contentText = `[${op.outputType.toLowerCase().slice(0, 3)}]`;
        }

        // Build hover with param values
        const hoverContent = this.createOperatorHover(op, params);

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

    private createOperatorHover(op: OperatorData, params: ParamData[]): vscode.MarkdownString {
        const md = new vscode.MarkdownString();
        md.isTrusted = true;

        md.appendMarkdown(`**${op.name}** \`${op.displayName}\`\n\n`);
        md.appendMarkdown(`Type: ${op.outputType}\n\n`);

        if (op.inputs.length > 0) {
            md.appendMarkdown(`Inputs: ${op.inputs.map(i => `\`${i}\``).join(', ')}\n\n`);
        }

        if (params.length > 0) {
            md.appendMarkdown(`**Parameters:**\n\n`);
            for (const p of params) {
                const valueStr = this.formatParamValue(p);
                md.appendMarkdown(`- \`${p.name}\`: ${valueStr}\n`);
            }
        }

        return md;
    }

    private formatParamValue(param: ParamData): string {
        const v = param.value;
        switch (param.type) {
            case 'Float':
                return v[0].toFixed(3);
            case 'Int':
                return Math.round(v[0]).toString();
            case 'Bool':
                return v[0] ? 'true' : 'false';
            case 'Vec2':
                return `(${v[0].toFixed(2)}, ${v[1].toFixed(2)})`;
            case 'Vec3':
                return `(${v[0].toFixed(2)}, ${v[1].toFixed(2)}, ${v[2].toFixed(2)})`;
            case 'Vec4':
                return `(${v[0].toFixed(2)}, ${v[1].toFixed(2)}, ${v[2].toFixed(2)}, ${v[3].toFixed(2)})`;
            case 'Color':
                // Show as color swatch + values
                const r = Math.round(v[0] * 255);
                const g = Math.round(v[1] * 255);
                const b = Math.round(v[2] * 255);
                return `rgba(${r}, ${g}, ${b}, ${v[3].toFixed(2)})`;
            default:
                return v[0].toFixed(3);
        }
    }

    private createTextureHover(node: NodeUpdate, size: number): vscode.MarkdownString {
        const md = new vscode.MarkdownString();
        md.isTrusted = true;
        md.supportHtml = true;

        md.appendMarkdown(`**${node.id}** (texture)\n\n`);

        if (node.preview) {
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
