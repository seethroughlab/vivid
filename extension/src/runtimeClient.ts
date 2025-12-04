import WebSocket from 'ws';
import * as sharedPreview from './sharedPreview';

export interface NodeUpdate {
    id: string;
    line: number;
    kind: 'texture' | 'value' | 'value_array' | 'geometry';
    preview?: string;  // base64 image for textures (or data URL from shared memory)
    value?: number;    // single value
    values?: number[]; // value array / history
    width?: number;
    height?: number;
}

export interface PreviewSlotInfo {
    id: string;
    slot: number;
    line: number;
    kind: 'texture' | 'value' | 'value_array' | 'geometry';
    updated: boolean;
}

export interface RuntimeMessage {
    type: string;
    nodes?: NodeUpdate[];
    slots?: PreviewSlotInfo[];
    frame?: number;
    sharedMem?: string;
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
    private sharedMemName: string | null = null;
    private sharedMemConnected = false;

    private connectedCallbacks: Callback<void>[] = [];
    private disconnectedCallbacks: Callback<void>[] = [];
    private nodeUpdateCallbacks: Callback<NodeUpdate[]>[] = [];
    private compileStatusCallbacks: Callback<[boolean, string]>[] = [];
    private errorCallbacks: Callback<string>[] = [];

    constructor(port: number) {
        this.port = port;
    }

    // Initialize shared memory support
    initSharedMemory(extensionPath: string): boolean {
        return sharedPreview.initSharedPreview(extensionPath);
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
                    // Legacy: full image data in WebSocket
                    if (msg.nodes) {
                        this.nodeUpdateCallbacks.forEach(cb => cb(msg.nodes!));
                    }
                    break;

                case 'preview_ready':
                    // New: metadata only, read from shared memory
                    this.handlePreviewReady(msg);
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

    private handlePreviewReady(msg: RuntimeMessage) {
        // Connect to shared memory if not already
        if (msg.sharedMem && msg.sharedMem !== this.sharedMemName) {
            this.sharedMemName = msg.sharedMem;
            this.sharedMemConnected = sharedPreview.openSharedMemory(msg.sharedMem);
        }

        if (!this.sharedMemConnected || !msg.slots) {
            return;
        }

        // Read from shared memory and build NodeUpdate array
        const updates: NodeUpdate[] = [];

        for (const slotInfo of msg.slots) {
            if (!slotInfo.updated) continue;

            const slot = sharedPreview.getSlot(slotInfo.slot);
            if (!slot || !slot.ready) continue;

            const update: NodeUpdate = {
                id: slot.operatorId,
                line: slot.sourceLine,
                kind: slotInfo.kind,
                width: slot.width,
                height: slot.height
            };

            if (slot.kind === sharedPreview.PREVIEW_KIND.TEXTURE && slot.pixels) {
                // Convert raw pixels to displayable format
                update.preview = sharedPreview.pixelsToDataUrl(slot.pixels);
            } else if (slot.kind === sharedPreview.PREVIEW_KIND.VALUE && slot.value !== undefined) {
                update.value = slot.value;
            } else if (slot.kind === sharedPreview.PREVIEW_KIND.VALUE_ARRAY && slot.values) {
                update.values = slot.values;
            }

            updates.push(update);
        }

        if (updates.length > 0) {
            this.nodeUpdateCallbacks.forEach(cb => cb(updates));
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
