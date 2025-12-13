import * as vscode from 'vscode';
import { OperatorData, ParamData } from './operatorTreeView';

export class NodeInspectorPanel implements vscode.WebviewViewProvider {
    public static readonly viewType = 'vividInspector';

    private _view?: vscode.WebviewView;
    private _selectedOperator?: OperatorData;
    private _params: ParamData[] = [];
    private _soloedOperator?: string;

    private _onParamChange: ((operator: string, param: string, value: number[]) => void) | undefined;
    private _onSoloRequest: ((operator: string) => void) | undefined;
    private _onExitSoloRequest: (() => void) | undefined;
    private _onBrowseFile: ((operator: string, param: string, filter: string, category: string) => void) | undefined;

    constructor(private readonly _extensionUri: vscode.Uri) {}

    public resolveWebviewView(
        webviewView: vscode.WebviewView,
        _context: vscode.WebviewViewResolveContext,
        _token: vscode.CancellationToken
    ) {
        this._view = webviewView;

        webviewView.webview.options = {
            enableScripts: true,
            localResourceRoots: [this._extensionUri]
        };

        webviewView.webview.html = this._getHtmlContent();

        // Handle messages from the webview
        webviewView.webview.onDidReceiveMessage(message => {
            switch (message.type) {
                case 'paramChange':
                    if (this._onParamChange) {
                        this._onParamChange(message.operator, message.param, message.value);
                    }
                    break;
                case 'solo':
                    if (this._onSoloRequest && message.operator) {
                        this._onSoloRequest(message.operator);
                    }
                    break;
                case 'exitSolo':
                    if (this._onExitSoloRequest) {
                        this._onExitSoloRequest();
                    }
                    break;
                case 'selectOperator':
                    vscode.commands.executeCommand('vivid.goToOperator', message.line);
                    break;
                case 'browseFile':
                    if (this._onBrowseFile) {
                        this._onBrowseFile(message.operator, message.param, message.filter, message.category);
                    }
                    break;
            }
        });
    }

    public setSelectedOperator(operator: OperatorData | undefined, params: ParamData[]) {
        this._selectedOperator = operator;
        this._params = params;
        this._updateWebview();
    }

    public updateParams(params: ParamData[]) {
        if (this._selectedOperator) {
            this._params = params.filter(p => p.operator === this._selectedOperator?.name);
            this._updateWebview();
        }
    }

    public updateSoloState(operator?: string) {
        this._soloedOperator = operator;
        this._updateWebview();
    }

    public onParamChange(callback: (operator: string, param: string, value: number[]) => void) {
        this._onParamChange = callback;
    }

    public onSoloRequest(callback: (operator: string) => void) {
        this._onSoloRequest = callback;
    }

    public onExitSoloRequest(callback: () => void) {
        this._onExitSoloRequest = callback;
    }

    public onBrowseFile(callback: (operator: string, param: string, filter: string, category: string) => void) {
        this._onBrowseFile = callback;
    }

    private _updateWebview() {
        if (!this._view) return;

        this._view.webview.postMessage({
            type: 'update',
            operator: this._selectedOperator,
            params: this._params,
            soloedOperator: this._soloedOperator
        });
    }

    private _getHtmlContent(): string {
        return `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Node Inspector</title>
    <style>
        :root {
            --vscode-font-family: var(--vscode-editor-font-family, system-ui);
        }
        body {
            font-family: var(--vscode-font-family);
            padding: 8px;
            color: var(--vscode-foreground);
            background: var(--vscode-sideBar-background);
        }
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 12px;
            padding-bottom: 8px;
            border-bottom: 1px solid var(--vscode-panel-border);
        }
        .header-title {
            font-weight: bold;
            font-size: 14px;
        }
        .header-subtitle {
            font-size: 11px;
            opacity: 0.7;
            margin-top: 2px;
        }
        .solo-btn {
            padding: 4px 8px;
            background: var(--vscode-button-secondaryBackground);
            color: var(--vscode-button-secondaryForeground);
            border: none;
            border-radius: 3px;
            cursor: pointer;
            font-size: 11px;
        }
        .solo-btn:hover {
            background: var(--vscode-button-secondaryHoverBackground);
        }
        .solo-btn.active {
            background: var(--vscode-statusBarItem-warningBackground);
            color: var(--vscode-statusBarItem-warningForeground);
        }
        .section {
            margin-bottom: 16px;
        }
        .section-title {
            font-size: 11px;
            text-transform: uppercase;
            opacity: 0.6;
            margin-bottom: 8px;
            letter-spacing: 0.5px;
        }
        .param-row {
            display: flex;
            flex-direction: column;
            margin-bottom: 12px;
        }
        .param-name {
            font-size: 12px;
            margin-bottom: 4px;
            display: flex;
            justify-content: space-between;
        }
        .param-value {
            font-size: 11px;
            opacity: 0.7;
            font-family: monospace;
        }
        .param-control {
            display: flex;
            gap: 4px;
            align-items: center;
        }
        input[type="range"] {
            flex: 1;
            height: 4px;
            -webkit-appearance: none;
            background: var(--vscode-input-background);
            border-radius: 2px;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 12px;
            height: 12px;
            background: var(--vscode-button-background);
            border-radius: 50%;
            cursor: pointer;
        }
        input[type="number"] {
            width: 60px;
            padding: 2px 4px;
            background: var(--vscode-input-background);
            color: var(--vscode-input-foreground);
            border: 1px solid var(--vscode-input-border);
            border-radius: 2px;
            font-size: 11px;
        }
        input[type="checkbox"] {
            width: 16px;
            height: 16px;
            cursor: pointer;
        }
        input[type="color"] {
            width: 32px;
            height: 24px;
            padding: 0;
            border: 1px solid var(--vscode-input-border);
            border-radius: 2px;
            cursor: pointer;
        }
        .vec-inputs {
            display: flex;
            gap: 4px;
        }
        .vec-inputs input {
            flex: 1;
            min-width: 40px;
        }
        .input-item {
            padding: 4px 8px;
            background: var(--vscode-badge-background);
            color: var(--vscode-badge-foreground);
            border-radius: 3px;
            font-size: 11px;
            cursor: pointer;
            display: inline-block;
            margin: 2px;
        }
        .input-item:hover {
            opacity: 0.8;
        }
        .empty-state {
            text-align: center;
            padding: 20px;
            opacity: 0.6;
            font-size: 12px;
        }
        .file-input {
            display: flex;
            gap: 8px;
            align-items: center;
        }
        .file-path {
            flex: 1;
            font-size: 11px;
            font-family: monospace;
            opacity: 0.8;
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }
        .browse-btn {
            padding: 3px 8px;
            background: var(--vscode-button-secondaryBackground);
            color: var(--vscode-button-secondaryForeground);
            border: none;
            border-radius: 3px;
            cursor: pointer;
            font-size: 11px;
        }
        .browse-btn:hover {
            background: var(--vscode-button-secondaryHoverBackground);
        }
    </style>
</head>
<body>
    <div id="content">
        <div class="empty-state">Click an operator to inspect</div>
    </div>

    <script>
        const vscode = acquireVsCodeApi();
        let currentOperator = null;
        let currentParams = [];
        let soloedOperator = null;

        window.addEventListener('message', event => {
            const message = event.data;
            if (message.type === 'update') {
                currentOperator = message.operator;
                currentParams = message.params || [];
                soloedOperator = message.soloedOperator;
                render();
            }
        });

        function render() {
            const content = document.getElementById('content');

            if (!currentOperator) {
                content.innerHTML = '<div class="empty-state">Click an operator to inspect</div>';
                return;
            }

            const isSoloed = soloedOperator === currentOperator.name;

            let html = \`
                <div class="header">
                    <div>
                        <div class="header-title">\${escapeHtml(currentOperator.name)}</div>
                        <div class="header-subtitle">\${escapeHtml(currentOperator.displayName)} (\${escapeHtml(currentOperator.outputType)})</div>
                    </div>
                    <button class="solo-btn \${isSoloed ? 'active' : ''}" onclick="toggleSolo()">
                        \${isSoloed ? 'Exit Solo' : 'Solo'}
                    </button>
                </div>
            \`;

            // Parameters section
            if (currentParams.length > 0) {
                html += '<div class="section"><div class="section-title">Parameters</div>';
                for (const param of currentParams) {
                    html += renderParam(param);
                }
                html += '</div>';
            }

            // Inputs section
            if (currentOperator.inputs && currentOperator.inputs.length > 0) {
                html += '<div class="section"><div class="section-title">Inputs</div>';
                for (const input of currentOperator.inputs) {
                    html += \`<span class="input-item" onclick="selectOperator('\${escapeHtml(input)}')">\${escapeHtml(input)}</span>\`;
                }
                html += '</div>';
            }

            content.innerHTML = html;
        }

        function renderParam(param) {
            const name = escapeHtml(param.name);
            const operator = escapeHtml(param.operator);

            let control = '';
            switch (param.type) {
                case 'Float':
                case 'Int':
                    const step = param.type === 'Int' ? 1 : 0.01;
                    control = \`
                        <div class="param-control">
                            <input type="range"
                                   min="\${param.min}" max="\${param.max}" step="\${step}"
                                   value="\${param.value[0]}"
                                   oninput="onSliderChange('\${operator}', '\${name}', this.value, '\${param.type}')">
                            <input type="number"
                                   min="\${param.min}" max="\${param.max}" step="\${step}"
                                   value="\${param.value[0].toFixed(param.type === 'Int' ? 0 : 2)}"
                                   onchange="onNumberChange('\${operator}', '\${name}', this.value, '\${param.type}')">
                        </div>
                    \`;
                    break;

                case 'Bool':
                    control = \`
                        <input type="checkbox" \${param.value[0] ? 'checked' : ''}
                               onchange="onBoolChange('\${operator}', '\${name}', this.checked)">
                    \`;
                    break;

                case 'Color':
                    const hex = rgbToHex(param.value[0], param.value[1], param.value[2]);
                    control = \`
                        <div class="param-control">
                            <input type="color" value="\${hex}"
                                   onchange="onColorChange('\${operator}', '\${name}', this.value, \${param.value[3]})">
                            <span class="param-value">\${hex}</span>
                        </div>
                    \`;
                    break;

                case 'Vec2':
                    control = \`
                        <div class="vec-inputs">
                            <input type="number" step="0.01" value="\${param.value[0].toFixed(2)}"
                                   onchange="onVecChange('\${operator}', '\${name}', 0, this.value, 2)">
                            <input type="number" step="0.01" value="\${param.value[1].toFixed(2)}"
                                   onchange="onVecChange('\${operator}', '\${name}', 1, this.value, 2)">
                        </div>
                    \`;
                    break;

                case 'Vec3':
                    control = \`
                        <div class="vec-inputs">
                            <input type="number" step="0.01" value="\${param.value[0].toFixed(2)}"
                                   onchange="onVecChange('\${operator}', '\${name}', 0, this.value, 3)">
                            <input type="number" step="0.01" value="\${param.value[1].toFixed(2)}"
                                   onchange="onVecChange('\${operator}', '\${name}', 1, this.value, 3)">
                            <input type="number" step="0.01" value="\${param.value[2].toFixed(2)}"
                                   onchange="onVecChange('\${operator}', '\${name}', 2, this.value, 3)">
                        </div>
                    \`;
                    break;

                case 'Vec4':
                    control = \`
                        <div class="vec-inputs">
                            <input type="number" step="0.01" value="\${param.value[0].toFixed(2)}"
                                   onchange="onVecChange('\${operator}', '\${name}', 0, this.value, 4)">
                            <input type="number" step="0.01" value="\${param.value[1].toFixed(2)}"
                                   onchange="onVecChange('\${operator}', '\${name}', 1, this.value, 4)">
                            <input type="number" step="0.01" value="\${param.value[2].toFixed(2)}"
                                   onchange="onVecChange('\${operator}', '\${name}', 2, this.value, 4)">
                            <input type="number" step="0.01" value="\${param.value[3].toFixed(2)}"
                                   onchange="onVecChange('\${operator}', '\${name}', 3, this.value, 4)">
                        </div>
                    \`;
                    break;

                case 'FilePath':
                case 'String':
                    const pathValue = param.stringValue || '';
                    const fileName = pathValue ? pathValue.split('/').pop() : '(none)';
                    const isFilePath = param.type === 'FilePath';
                    control = \`
                        <div class="file-input">
                            <span class="file-path" title="\${escapeHtml(pathValue)}">\${escapeHtml(fileName)}</span>
                            \${isFilePath ? \`<button class="browse-btn" onclick="browseFile('\${operator}', '\${name}', '\${escapeHtml(param.fileFilter || '*.*')}', '\${escapeHtml(param.fileCategory || '')}')">Browse</button>\` : ''}
                        </div>
                    \`;
                    break;

                default:
                    control = \`<span class="param-value">\${formatValue(param)}</span>\`;
            }

            return \`
                <div class="param-row">
                    <div class="param-name">
                        <span>\${name}</span>
                        <span class="param-value">\${param.type}</span>
                    </div>
                    \${control}
                </div>
            \`;
        }

        function escapeHtml(str) {
            const div = document.createElement('div');
            div.textContent = str;
            return div.innerHTML;
        }

        function formatValue(param) {
            const v = param.value;
            switch (param.type) {
                case 'Float':
                case 'Int':
                    return v[0].toFixed(2);
                case 'Bool':
                    return v[0] ? 'true' : 'false';
                case 'Vec2':
                    return \`(\${v[0].toFixed(2)}, \${v[1].toFixed(2)})\`;
                case 'Vec3':
                    return \`(\${v[0].toFixed(2)}, \${v[1].toFixed(2)}, \${v[2].toFixed(2)})\`;
                case 'Vec4':
                case 'Color':
                    return \`(\${v[0].toFixed(2)}, \${v[1].toFixed(2)}, \${v[2].toFixed(2)}, \${v[3].toFixed(2)})\`;
                default:
                    return v[0].toFixed(2);
            }
        }

        function rgbToHex(r, g, b) {
            const toHex = c => Math.round(c * 255).toString(16).padStart(2, '0');
            return '#' + toHex(r) + toHex(g) + toHex(b);
        }

        function hexToRgb(hex) {
            const result = /^#?([a-f\\d]{2})([a-f\\d]{2})([a-f\\d]{2})$/i.exec(hex);
            return result ? {
                r: parseInt(result[1], 16) / 255,
                g: parseInt(result[2], 16) / 255,
                b: parseInt(result[3], 16) / 255
            } : null;
        }

        // Parameter change handlers
        function onSliderChange(operator, param, value, type) {
            const numValue = type === 'Int' ? Math.round(parseFloat(value)) : parseFloat(value);
            sendParamChange(operator, param, [numValue, 0, 0, 0]);
            // Update the number input
            const row = event.target.closest('.param-control');
            if (row) {
                const numInput = row.querySelector('input[type="number"]');
                if (numInput) numInput.value = numValue.toFixed(type === 'Int' ? 0 : 2);
            }
        }

        function onNumberChange(operator, param, value, type) {
            const numValue = type === 'Int' ? Math.round(parseFloat(value)) : parseFloat(value);
            sendParamChange(operator, param, [numValue, 0, 0, 0]);
            // Update the slider
            const row = event.target.closest('.param-control');
            if (row) {
                const slider = row.querySelector('input[type="range"]');
                if (slider) slider.value = numValue;
            }
        }

        function onBoolChange(operator, param, checked) {
            sendParamChange(operator, param, [checked ? 1 : 0, 0, 0, 0]);
        }

        function onColorChange(operator, param, hex, alpha) {
            const rgb = hexToRgb(hex);
            if (rgb) {
                sendParamChange(operator, param, [rgb.r, rgb.g, rgb.b, alpha]);
            }
        }

        function onVecChange(operator, param, index, value, components) {
            // Get current values from the param
            const currentParam = currentParams.find(p => p.operator === operator && p.name === param);
            if (!currentParam) return;

            const newValue = [...currentParam.value];
            newValue[index] = parseFloat(value);
            sendParamChange(operator, param, newValue);
        }

        function sendParamChange(operator, param, value) {
            vscode.postMessage({
                type: 'paramChange',
                operator: operator,
                param: param,
                value: value
            });
        }

        function toggleSolo() {
            if (soloedOperator === currentOperator.name) {
                vscode.postMessage({ type: 'exitSolo' });
            } else {
                vscode.postMessage({ type: 'solo', operator: currentOperator.name });
            }
        }

        function selectOperator(name) {
            // Find the operator's source line from the inputs
            // For now, just trigger the command - extension will handle finding the line
            vscode.postMessage({ type: 'selectOperator', name: name });
        }

        function browseFile(operator, param, filter, category) {
            vscode.postMessage({
                type: 'browseFile',
                operator: operator,
                param: param,
                filter: filter,
                category: category
            });
        }
    </script>
</body>
</html>`;
    }
}
