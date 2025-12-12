import * as vscode from 'vscode';
import { PerformanceStats } from './runtimeClient';

export class PerformancePanelProvider implements vscode.WebviewViewProvider {
    public static readonly viewType = 'vividPerformance';

    private _view?: vscode.WebviewView;
    private _stats?: PerformanceStats;

    constructor(private readonly _extensionUri: vscode.Uri) {}

    public resolveWebviewView(
        webviewView: vscode.WebviewView,
        context: vscode.WebviewViewResolveContext,
        _token: vscode.CancellationToken
    ) {
        this._view = webviewView;

        webviewView.webview.options = {
            enableScripts: true,
            localResourceRoots: [this._extensionUri]
        };

        webviewView.webview.html = this._getHtmlForWebview(webviewView.webview);

        // Send initial stats if available
        if (this._stats) {
            this.updateStats(this._stats);
        }
    }

    public updateStats(stats: PerformanceStats) {
        this._stats = stats;
        if (this._view) {
            this._view.webview.postMessage({ type: 'update', stats });
        }
    }

    private _getHtmlForWebview(webview: vscode.Webview): string {
        return `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Performance</title>
    <style>
        body {
            font-family: var(--vscode-font-family);
            font-size: var(--vscode-font-size);
            color: var(--vscode-foreground);
            background-color: var(--vscode-sideBar-background);
            padding: 8px;
            margin: 0;
        }
        .stat-row {
            display: flex;
            justify-content: space-between;
            margin-bottom: 4px;
        }
        .stat-label {
            color: var(--vscode-descriptionForeground);
        }
        .stat-value {
            font-family: var(--vscode-editor-font-family);
            font-weight: bold;
        }
        .stat-value.good { color: var(--vscode-charts-green); }
        .stat-value.warn { color: var(--vscode-charts-yellow); }
        .stat-value.bad { color: var(--vscode-charts-red); }
        .graph-container {
            margin: 8px 0;
            height: 40px;
            background-color: var(--vscode-editor-background);
            border-radius: 4px;
            overflow: hidden;
            position: relative;
        }
        .graph-label {
            font-size: 10px;
            color: var(--vscode-descriptionForeground);
            margin-bottom: 2px;
        }
        canvas {
            width: 100%;
            height: 100%;
        }
        .section {
            margin-bottom: 12px;
        }
        .section-title {
            font-weight: bold;
            margin-bottom: 6px;
            font-size: 11px;
            text-transform: uppercase;
            color: var(--vscode-sideBarSectionHeader-foreground);
        }
        .no-data {
            color: var(--vscode-descriptionForeground);
            font-style: italic;
            text-align: center;
            padding: 20px;
        }
    </style>
</head>
<body>
    <div id="content">
        <div class="no-data">Waiting for runtime connection...</div>
    </div>

    <script>
        const vscode = acquireVsCodeApi();

        let fpsHistory = [];
        let frameTimeHistory = [];

        window.addEventListener('message', event => {
            const message = event.data;
            if (message.type === 'update') {
                updateDisplay(message.stats);
            }
        });

        function updateDisplay(stats) {
            fpsHistory = stats.fpsHistory || [];
            frameTimeHistory = stats.frameTimeHistory || [];

            const content = document.getElementById('content');
            content.innerHTML = \`
                <div class="section">
                    <div class="section-title">Frame Rate</div>
                    <div class="stat-row">
                        <span class="stat-label">FPS</span>
                        <span class="stat-value \${getFpsClass(stats.fps)}">\${stats.fps.toFixed(0)}</span>
                    </div>
                    <div class="stat-row">
                        <span class="stat-label">Frame Time</span>
                        <span class="stat-value">\${stats.frameTimeMs.toFixed(1)} ms</span>
                    </div>
                    <div class="graph-label">FPS History (last 60 samples)</div>
                    <div class="graph-container">
                        <canvas id="fpsGraph"></canvas>
                    </div>
                </div>

                <div class="section">
                    <div class="section-title">Memory</div>
                    <div class="stat-row">
                        <span class="stat-label">Texture Memory</span>
                        <span class="stat-value">\${formatBytes(stats.textureMemoryBytes)}</span>
                    </div>
                    <div class="stat-row">
                        <span class="stat-label">Operators</span>
                        <span class="stat-value">\${stats.operatorCount}</span>
                    </div>
                </div>

                <div class="section">
                    <div class="graph-label">Frame Time History</div>
                    <div class="graph-container">
                        <canvas id="frameTimeGraph"></canvas>
                    </div>
                </div>
            \`;

            // Draw graphs
            requestAnimationFrame(() => {
                drawGraph('fpsGraph', fpsHistory, '#4EC9B0', 0, 120);
                drawGraph('frameTimeGraph', frameTimeHistory, '#DCDCAA', 0, 50);
            });
        }

        function getFpsClass(fps) {
            if (fps >= 55) return 'good';
            if (fps >= 30) return 'warn';
            return 'bad';
        }

        function formatBytes(bytes) {
            if (bytes < 1024) return bytes + ' B';
            if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
            return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
        }

        function drawGraph(canvasId, data, color, minVal, maxVal) {
            const canvas = document.getElementById(canvasId);
            if (!canvas || !data || data.length === 0) return;

            const ctx = canvas.getContext('2d');
            const dpr = window.devicePixelRatio || 1;

            // Set canvas size for high DPI
            const rect = canvas.getBoundingClientRect();
            canvas.width = rect.width * dpr;
            canvas.height = rect.height * dpr;
            ctx.scale(dpr, dpr);

            const width = rect.width;
            const height = rect.height;
            const padding = 2;

            // Clear
            ctx.clearRect(0, 0, width, height);

            // Calculate range
            const dataMin = Math.min(minVal, ...data);
            const dataMax = Math.max(maxVal, ...data);
            const range = dataMax - dataMin || 1;

            // Draw line
            ctx.beginPath();
            ctx.strokeStyle = color;
            ctx.lineWidth = 1.5;

            for (let i = 0; i < data.length; i++) {
                const x = (i / (data.length - 1)) * (width - padding * 2) + padding;
                const y = height - padding - ((data[i] - dataMin) / range) * (height - padding * 2);

                if (i === 0) {
                    ctx.moveTo(x, y);
                } else {
                    ctx.lineTo(x, y);
                }
            }
            ctx.stroke();

            // Draw current value indicator
            if (data.length > 0) {
                const lastVal = data[data.length - 1];
                const lastY = height - padding - ((lastVal - dataMin) / range) * (height - padding * 2);
                ctx.beginPath();
                ctx.fillStyle = color;
                ctx.arc(width - padding, lastY, 3, 0, Math.PI * 2);
                ctx.fill();
            }
        }
    </script>
</body>
</html>`;
    }
}
