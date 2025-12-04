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
