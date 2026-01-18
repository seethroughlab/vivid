// Vivid RuntimeAPI WebSocket Connection
// Connects to running Vivid instance on port 9876
// NOTE: This file is kept for reference but not used by the Tauri IDE
// (the IDE uses C API via Tauri commands instead)

export type ConnectionStatus = "disconnected" | "connecting" | "connected";

// Message types from Vivid runtime
export interface CompileStatus {
  type: "compile_status";
  success: boolean;
  message: string;
}

export interface OperatorInfo {
  name: string;
  displayName: string;
  outputType: string;
  sourceLine?: number;
  inputs: string[];
}

export interface OperatorList {
  type: "operator_list";
  operators: OperatorInfo[];
}

export interface ParamValue {
  operator: string;
  name: string;
  type: "Float" | "Int" | "Bool" | "Vec2" | "Vec3" | "Vec4" | "Color" | "String" | "FilePath";
  value: number[];
  min?: number;
  max?: number;
  stringValue?: string;
  fileFilter?: string;
  fileCategory?: string;
}

export interface ParamValues {
  type: "param_values";
  params: ParamValue[];
}

export interface ChainStructure {
  type: "chain_structure";
  operators: OperatorInfo[];
}

export interface SoloState {
  type: "solo_state";
  active: boolean;
  operator: string;
}

export interface WindowState {
  type: "window_state";
  fullscreen: boolean;
  borderless: boolean;
  alwaysOnTop: boolean;
  cursorVisible: boolean;
  currentMonitor: number;
  monitors: Array<{
    index: number;
    name: string;
    width: number;
    height: number;
  }>;
}

export interface PendingChange {
  operator: string;
  param: string;
  paramType: string;
  oldValue: number[];
  newValue: number[];
  sourceLine?: number;
  timestamp: number;
}

export interface PendingChanges {
  type: "pending_changes";
  hasChanges: boolean;
  changes: PendingChange[];
}

export interface FrameInfo {
  type: "frame_info";
  frame: number;
  time: number;
  fps: number;
}

export interface PerformanceStats {
  type: "performance_stats";
  fps: number;
  frameTimeMs: number;
  fpsHistory: number[];
  frameTimeHistory: number[];
  textureMemoryBytes: number;
  operatorCount: number;
  operatorTimings: Array<{ name: string; timeMs: number }>;
}

export interface CaptureResult {
  type: "capture_result";
  success: boolean;
  outputPath: string;
  error?: string;
}

export interface SetParamResult {
  type: "set_param_result";
  operator: string;
  param: string;
  success: boolean;
}

export type VividMessage =
  | CompileStatus
  | OperatorList
  | ParamValues
  | ChainStructure
  | SoloState
  | WindowState
  | PendingChanges
  | FrameInfo
  | PerformanceStats
  | CaptureResult
  | SetParamResult;

export type MessageHandler = (message: VividMessage) => void;
export type StatusHandler = (status: ConnectionStatus) => void;

class VividConnection {
  private ws: WebSocket | null = null;
  private url = "ws://localhost:9876";
  private reconnectInterval = 2000;
  private reconnectTimer: number | null = null;
  private messageHandlers: Set<MessageHandler> = new Set();
  private statusHandlers: Set<StatusHandler> = new Set();
  private _status: ConnectionStatus = "disconnected";

  get status(): ConnectionStatus {
    return this._status;
  }

  private setStatus(status: ConnectionStatus) {
    this._status = status;
    this.statusHandlers.forEach((handler) => handler(status));
  }

  connect() {
    if (this.ws?.readyState === WebSocket.OPEN) {
      return;
    }

    this.setStatus("connecting");

    try {
      this.ws = new WebSocket(this.url);

      this.ws.onopen = () => {
        console.log("Connected to Vivid EditorBridge");
        this.setStatus("connected");
        // Request initial state
        this.requestOperators();
        this.requestChainStructure();
        this.requestCompileStatus();
      };

      this.ws.onmessage = (event) => {
        try {
          const message = JSON.parse(event.data) as VividMessage;
          this.messageHandlers.forEach((handler) => handler(message));
        } catch (e) {
          console.error("Failed to parse Vivid message:", e);
        }
      };

      this.ws.onclose = () => {
        console.log("Disconnected from Vivid EditorBridge");
        this.setStatus("disconnected");
        this.scheduleReconnect();
      };

      this.ws.onerror = (error) => {
        console.error("WebSocket error:", error);
        this.ws?.close();
      };
    } catch (e) {
      console.error("Failed to connect to Vivid:", e);
      this.setStatus("disconnected");
      this.scheduleReconnect();
    }
  }

  disconnect() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.setStatus("disconnected");
  }

  private scheduleReconnect() {
    if (this.reconnectTimer) {
      return;
    }
    this.reconnectTimer = window.setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, this.reconnectInterval);
  }

  private send(message: object) {
    if (this.ws?.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(message));
    }
  }

  // Subscribe to messages
  onMessage(handler: MessageHandler): () => void {
    this.messageHandlers.add(handler);
    return () => this.messageHandlers.delete(handler);
  }

  // Subscribe to status changes
  onStatusChange(handler: StatusHandler): () => void {
    this.statusHandlers.add(handler);
    return () => this.statusHandlers.delete(handler);
  }

  // --- Commands to Vivid ---

  // Request chain operators and parameters
  requestOperators() {
    this.send({ type: "request_operators" });
  }

  // Request chain structure
  requestChainStructure() {
    this.send({ type: "request_chain_structure" });
  }

  // Request compile status
  requestCompileStatus() {
    this.send({ type: "request_compile_status" });
  }

  // Request pending changes
  requestPendingChanges() {
    this.send({ type: "request_pending_changes" });
  }

  // Request frame info
  requestFrameInfo() {
    this.send({ type: "request_frame_info" });
  }

  // Request window state
  requestWindowState() {
    this.send({ type: "request_window_state" });
  }

  // Set parameter value (triggers pending change tracking)
  setParam(operator: string, param: string, value: number[]) {
    this.send({
      type: "param_change",
      operator,
      param,
      value,
    });
  }

  // Set parameter immediately (no pending change tracking)
  setParamImmediate(operator: string, param: string, value: number[]) {
    this.send({
      type: "set_param_immediate",
      operator,
      param,
      value,
    });
  }

  // Solo an operator (preview its output)
  soloNode(operator: string) {
    this.send({
      type: "solo_node",
      operator,
    });
  }

  // Exit solo mode
  soloExit() {
    this.send({ type: "solo_exit" });
  }

  // Select a node (highlight in graph)
  selectNode(operator: string) {
    this.send({
      type: "select_node",
      operator,
    });
  }

  // Focus a node (3x larger preview)
  focusNode(operator: string) {
    this.send({
      type: "focused_node",
      operator,
    });
  }

  // Trigger hot-reload
  reload() {
    this.send({ type: "reload" });
  }

  // Commit pending changes (clear queue after applying to code)
  commitChanges() {
    this.send({ type: "commit_changes" });
  }

  // Discard pending changes (revert to original values)
  discardChanges() {
    this.send({ type: "discard_changes" });
  }

  // Advance simulation by N frames
  advanceFrames(count: number) {
    this.send({
      type: "advance_frames",
      count,
    });
  }

  // Reset time/frame counter
  resetTime() {
    this.send({ type: "reset_time" });
  }

  // Capture current frame to PNG
  captureFrame(outputPath: string) {
    this.send({
      type: "capture_frame",
      outputPath,
    });
  }

  // Window control
  setFullscreen(enabled: boolean) {
    this.send({
      type: "window_control",
      setting: "fullscreen",
      value: enabled ? 1 : 0,
    });
  }

  setBorderless(enabled: boolean) {
    this.send({
      type: "window_control",
      setting: "borderless",
      value: enabled ? 1 : 0,
    });
  }

  setAlwaysOnTop(enabled: boolean) {
    this.send({
      type: "window_control",
      setting: "alwaysOnTop",
      value: enabled ? 1 : 0,
    });
  }

  setCursorVisible(visible: boolean) {
    this.send({
      type: "window_control",
      setting: "cursorVisible",
      value: visible ? 1 : 0,
    });
  }

  setMonitor(index: number) {
    this.send({
      type: "window_control",
      setting: "monitor",
      value: index,
    });
  }
}

// Singleton instance
export const vividConnection = new VividConnection();
