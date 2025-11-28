import WebSocket from 'ws';

export interface NodeUpdate {
    id: string;
    line: number;
    kind: 'texture' | 'value' | 'value_array' | 'geometry';
    preview?: string;  // base64 image for textures
    value?: number;    // single value
    values?: number[]; // value array / history
    width?: number;
    height?: number;
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
