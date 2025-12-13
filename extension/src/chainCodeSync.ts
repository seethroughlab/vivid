import * as vscode from 'vscode';

interface ParamLocation {
    line: number;
    startColumn: number;
    endColumn: number;
    currentValue: string;
}

interface OperatorParamMap {
    [operatorName: string]: {
        [paramName: string]: ParamLocation;
    };
}

export class ChainCodeSync {
    private paramMap: OperatorParamMap = {};
    private chainDocument: vscode.TextDocument | undefined;
    private pendingUpdates: Map<string, { value: number[], timeout: NodeJS.Timeout }> = new Map();
    private debounceMs = 500;
    private isUpdating = false;

    async findChainFile(): Promise<vscode.TextDocument | undefined> {
        const files = await vscode.workspace.findFiles('**/chain.cpp', null, 1);
        if (files.length === 0) {
            return undefined;
        }
        this.chainDocument = await vscode.workspace.openTextDocument(files[0]);
        return this.chainDocument;
    }

    async parseChainFile(): Promise<void> {
        if (!this.chainDocument) {
            await this.findChainFile();
        }
        if (!this.chainDocument) {
            return;
        }

        this.paramMap = {};
        const text = this.chainDocument.getText();
        const lines = text.split('\n');

        // Find operator declarations and their parameters
        // Pattern: chain.add<Type>("name") followed by .paramName(value)
        let currentOperator: string | undefined;

        for (let lineNum = 0; lineNum < lines.length; lineNum++) {
            const line = lines[lineNum];

            // Match chain.add<Type>("name") or chain.add<Type, Config>("name")
            const addMatch = line.match(/chain\.add<[^>]+>\s*\(\s*["']([^"']+)["']\s*\)/);
            if (addMatch) {
                currentOperator = addMatch[1];
                if (!this.paramMap[currentOperator]) {
                    this.paramMap[currentOperator] = {};
                }
            }

            // Also match auto& name = chain.add<Type>("name")
            const autoAddMatch = line.match(/auto\s*&?\s*\w+\s*=\s*chain\.add<[^>]+>\s*\(\s*["']([^"']+)["']\s*\)/);
            if (autoAddMatch) {
                currentOperator = autoAddMatch[1];
                if (!this.paramMap[currentOperator]) {
                    this.paramMap[currentOperator] = {};
                }
            }

            // If we're in an operator context, look for parameter setters
            if (currentOperator) {
                // Match .paramName(value) - handles float, int, bool
                const paramMatches = line.matchAll(/\.(\w+)\s*\(\s*(-?[\d.]+f?|true|false)\s*\)/g);
                for (const match of paramMatches) {
                    const paramName = match[1];
                    const value = match[2];

                    // Skip common non-param methods
                    if (['input', 'output', 'size', 'add'].includes(paramName)) {
                        continue;
                    }

                    const startCol = match.index! + match[0].indexOf('(') + 1;
                    const endCol = startCol + value.length;

                    this.paramMap[currentOperator][paramName] = {
                        line: lineNum,
                        startColumn: startCol,
                        endColumn: endCol,
                        currentValue: value
                    };
                }

                // Match .paramName(x, y) for Vec2
                const vec2Matches = line.matchAll(/\.(\w+)\s*\(\s*(-?[\d.]+f?)\s*,\s*(-?[\d.]+f?)\s*\)/g);
                for (const match of vec2Matches) {
                    const paramName = match[1];
                    if (['input', 'output', 'size', 'resolution'].includes(paramName)) {
                        continue;
                    }

                    const fullMatch = match[0];
                    const startCol = match.index! + fullMatch.indexOf('(') + 1;
                    const endCol = match.index! + fullMatch.lastIndexOf(')');

                    this.paramMap[currentOperator][paramName] = {
                        line: lineNum,
                        startColumn: startCol,
                        endColumn: endCol,
                        currentValue: `${match[2]}, ${match[3]}`
                    };
                }

                // Match .paramName(x, y, z) for Vec3
                const vec3Matches = line.matchAll(/\.(\w+)\s*\(\s*(-?[\d.]+f?)\s*,\s*(-?[\d.]+f?)\s*,\s*(-?[\d.]+f?)\s*\)/g);
                for (const match of vec3Matches) {
                    const paramName = match[1];
                    if (['input', 'output'].includes(paramName)) {
                        continue;
                    }

                    const fullMatch = match[0];
                    const startCol = match.index! + fullMatch.indexOf('(') + 1;
                    const endCol = match.index! + fullMatch.lastIndexOf(')');

                    this.paramMap[currentOperator][paramName] = {
                        line: lineNum,
                        startColumn: startCol,
                        endColumn: endCol,
                        currentValue: `${match[2]}, ${match[3]}, ${match[4]}`
                    };
                }

                // Match .paramName(x, y, z, w) for Vec4/Color
                const vec4Matches = line.matchAll(/\.(\w+)\s*\(\s*(-?[\d.]+f?)\s*,\s*(-?[\d.]+f?)\s*,\s*(-?[\d.]+f?)\s*,\s*(-?[\d.]+f?)\s*\)/g);
                for (const match of vec4Matches) {
                    const paramName = match[1];
                    if (['input', 'output'].includes(paramName)) {
                        continue;
                    }

                    const fullMatch = match[0];
                    const startCol = match.index! + fullMatch.indexOf('(') + 1;
                    const endCol = match.index! + fullMatch.lastIndexOf(')');

                    this.paramMap[currentOperator][paramName] = {
                        line: lineNum,
                        startColumn: startCol,
                        endColumn: endCol,
                        currentValue: `${match[2]}, ${match[3]}, ${match[4]}, ${match[5]}`
                    };
                }

                // Check if this line ends the operator chain (ends with semicolon)
                if (line.trim().endsWith(';') && !line.includes('chain.add')) {
                    // Only reset if we're not starting a new add on the same line
                    if (!addMatch && !autoAddMatch) {
                        // Look ahead to see if next non-empty line continues the chain
                        let nextLineNum = lineNum + 1;
                        while (nextLineNum < lines.length && lines[nextLineNum].trim() === '') {
                            nextLineNum++;
                        }
                        if (nextLineNum < lines.length) {
                            const nextLine = lines[nextLineNum].trim();
                            if (!nextLine.startsWith('.')) {
                                currentOperator = undefined;
                            }
                        }
                    }
                }
            }
        }
    }

    scheduleParamUpdate(operator: string, param: string, value: number[], paramType: string): void {
        const key = `${operator}.${param}`;

        // Cancel any pending update for this param
        const pending = this.pendingUpdates.get(key);
        if (pending) {
            clearTimeout(pending.timeout);
        }

        // Schedule new update
        const timeout = setTimeout(() => {
            this.applyParamUpdate(operator, param, value, paramType);
            this.pendingUpdates.delete(key);
        }, this.debounceMs);

        this.pendingUpdates.set(key, { value, timeout });
    }

    private async applyParamUpdate(operator: string, param: string, value: number[], paramType: string): Promise<void> {
        if (this.isUpdating) {
            return;
        }

        // Re-parse to get fresh locations
        await this.parseChainFile();

        const opParams = this.paramMap[operator];
        if (!opParams || !opParams[param]) {
            // Parameter not found in source - might be dynamically set
            return;
        }

        const location = opParams[param];
        if (!this.chainDocument) {
            return;
        }

        // Format the new value based on type
        let newValueStr: string;
        switch (paramType) {
            case 'Float':
                newValueStr = this.formatFloat(value[0]);
                break;
            case 'Int':
                newValueStr = Math.round(value[0]).toString();
                break;
            case 'Bool':
                newValueStr = value[0] ? 'true' : 'false';
                break;
            case 'Vec2':
                newValueStr = `${this.formatFloat(value[0])}, ${this.formatFloat(value[1])}`;
                break;
            case 'Vec3':
                newValueStr = `${this.formatFloat(value[0])}, ${this.formatFloat(value[1])}, ${this.formatFloat(value[2])}`;
                break;
            case 'Vec4':
            case 'Color':
                newValueStr = `${this.formatFloat(value[0])}, ${this.formatFloat(value[1])}, ${this.formatFloat(value[2])}, ${this.formatFloat(value[3])}`;
                break;
            default:
                newValueStr = this.formatFloat(value[0]);
        }

        // Create the edit
        const range = new vscode.Range(
            location.line,
            location.startColumn,
            location.line,
            location.endColumn
        );

        this.isUpdating = true;
        try {
            const edit = new vscode.WorkspaceEdit();
            edit.replace(this.chainDocument.uri, range, newValueStr);
            await vscode.workspace.applyEdit(edit);

            // Update our cached location
            location.endColumn = location.startColumn + newValueStr.length;
            location.currentValue = newValueStr;
        } finally {
            this.isUpdating = false;
        }
    }

    private formatFloat(value: number): string {
        // Format float with 'f' suffix for C++
        const str = value.toFixed(2);
        // Remove trailing zeros but keep at least one decimal
        const trimmed = str.replace(/\.?0+$/, '');
        return (trimmed.includes('.') ? trimmed : str) + 'f';
    }

    getParamLocation(operator: string, param: string): ParamLocation | undefined {
        return this.paramMap[operator]?.[param];
    }

    clear(): void {
        // Cancel all pending updates
        for (const pending of this.pendingUpdates.values()) {
            clearTimeout(pending.timeout);
        }
        this.pendingUpdates.clear();
        this.paramMap = {};
        this.chainDocument = undefined;
    }
}
