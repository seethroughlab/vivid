import * as vscode from 'vscode';

export interface OperatorData {
    name: string;           // Chain name (e.g., "noise")
    displayName: string;    // Operator type (e.g., "Noise")
    outputType: string;     // e.g., "Texture", "Geometry", "Value"
    sourceLine: number;     // Line in chain.cpp
    inputs: string[];       // Connected input names
}

export interface ParamData {
    operator: string;
    name: string;
    type: string;
    value: number[];
    min: number;
    max: number;
}

class OperatorTreeItem extends vscode.TreeItem {
    constructor(
        public readonly operator: OperatorData,
        public readonly collapsibleState: vscode.TreeItemCollapsibleState
    ) {
        super(operator.name, collapsibleState);

        this.description = operator.displayName;
        this.tooltip = `${operator.displayName} (${operator.outputType})\nLine: ${operator.sourceLine}`;

        // Icon based on output type
        this.iconPath = this.getIcon(operator.outputType);

        // Click to go to line
        if (operator.sourceLine > 0) {
            this.command = {
                command: 'vivid.goToOperator',
                title: 'Go to Operator',
                arguments: [operator.sourceLine]
            };
        }

        this.contextValue = 'operator';
    }

    private getIcon(outputType: string): vscode.ThemeIcon {
        switch (outputType) {
            case 'Texture':
                return new vscode.ThemeIcon('file-media');
            case 'Geometry':
                return new vscode.ThemeIcon('symbol-structure');
            case 'Value':
                return new vscode.ThemeIcon('symbol-number');
            case 'ValueArray':
                return new vscode.ThemeIcon('graph');
            case 'Camera':
                return new vscode.ThemeIcon('device-camera');
            case 'Light':
                return new vscode.ThemeIcon('lightbulb');
            case 'Audio':
                return new vscode.ThemeIcon('unmute');
            case 'AudioValue':
                return new vscode.ThemeIcon('pulse');
            default:
                return new vscode.ThemeIcon('symbol-misc');
        }
    }
}

class ParamTreeItem extends vscode.TreeItem {
    constructor(
        public readonly param: ParamData,
        public readonly parentOperator: string
    ) {
        super(param.name, vscode.TreeItemCollapsibleState.None);

        // Format value display based on type
        const valueStr = this.formatValue(param);
        this.description = valueStr;
        this.tooltip = `${param.type}: ${valueStr}\nRange: [${param.min}, ${param.max}]\nClick to edit`;

        this.iconPath = new vscode.ThemeIcon('symbol-property');
        this.contextValue = 'param';

        // Click to edit
        this.command = {
            command: 'vivid.editParam',
            title: 'Edit Parameter',
            arguments: [param]
        };
    }

    private formatValue(param: ParamData): string {
        const v = param.value;
        switch (param.type) {
            case 'Float':
            case 'Int':
                return v[0].toFixed(2);
            case 'Bool':
                return v[0] ? 'true' : 'false';
            case 'Vec2':
                return `(${v[0].toFixed(2)}, ${v[1].toFixed(2)})`;
            case 'Vec3':
                return `(${v[0].toFixed(2)}, ${v[1].toFixed(2)}, ${v[2].toFixed(2)})`;
            case 'Vec4':
            case 'Color':
                return `(${v[0].toFixed(2)}, ${v[1].toFixed(2)}, ${v[2].toFixed(2)}, ${v[3].toFixed(2)})`;
            default:
                return v[0].toFixed(2);
        }
    }
}

type TreeItem = OperatorTreeItem | ParamTreeItem;

export class OperatorTreeProvider implements vscode.TreeDataProvider<TreeItem> {
    private _onDidChangeTreeData = new vscode.EventEmitter<TreeItem | undefined | null | void>();
    readonly onDidChangeTreeData = this._onDidChangeTreeData.event;

    private operators: OperatorData[] = [];
    private params: Map<string, ParamData[]> = new Map();

    updateOperators(operators: OperatorData[]) {
        this.operators = operators;
        this._onDidChangeTreeData.fire();
    }

    updateParams(params: ParamData[]) {
        this.params.clear();
        for (const p of params) {
            if (!this.params.has(p.operator)) {
                this.params.set(p.operator, []);
            }
            this.params.get(p.operator)!.push(p);
        }
        this._onDidChangeTreeData.fire();
    }

    clear() {
        this.operators = [];
        this.params.clear();
        this._onDidChangeTreeData.fire();
    }

    getTreeItem(element: TreeItem): vscode.TreeItem {
        return element;
    }

    getChildren(element?: TreeItem): Thenable<TreeItem[]> {
        if (!element) {
            // Root level: show operators
            return Promise.resolve(
                this.operators.map(op => {
                    const hasParams = this.params.has(op.name) && this.params.get(op.name)!.length > 0;
                    return new OperatorTreeItem(
                        op,
                        hasParams ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None
                    );
                })
            );
        }

        if (element instanceof OperatorTreeItem) {
            // Show params for this operator
            const opParams = this.params.get(element.operator.name) || [];
            return Promise.resolve(
                opParams.map(p => new ParamTreeItem(p, element.operator.name))
            );
        }

        return Promise.resolve([]);
    }
}
