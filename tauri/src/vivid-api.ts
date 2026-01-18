// Vivid API - Direct Tauri command interface (replaces WebSocket)

const invoke = window.__TAURI__.core.invoke;

// Types matching Rust structs
export interface ProjectInfo {
  loaded: boolean;
  project_path: string | null;
  chain_path: string | null;
}

export interface CompileStatusInfo {
  success: boolean;
  message: string | null;
  error_line: number | null;
  error_column: number | null;
}

export interface OperatorInfo {
  name: string;
  type_name: string;
  output_kind: string;
  bypassed: boolean;
  input_count: number;
  inputs: string[];
}

export interface ParamInfo {
  name: string;
  param_type: string;
  min_val: number;
  max_val: number;
  value: [number, number, number, number];
  default_val: [number, number, number, number];
  enum_labels: string[];
}

// API functions
export async function getProjectInfo(): Promise<ProjectInfo> {
  return invoke<ProjectInfo>("get_project_info");
}

export async function getCompileStatus(): Promise<CompileStatusInfo> {
  return invoke<CompileStatusInfo>("get_compile_status");
}

export async function getOperators(): Promise<OperatorInfo[]> {
  return invoke<OperatorInfo[]>("get_operators");
}

export async function getOperatorParams(opName: string): Promise<ParamInfo[]> {
  return invoke<ParamInfo[]>("get_operator_params", { opName });
}

export async function setParam(
  opName: string,
  paramName: string,
  value: [number, number, number, number]
): Promise<boolean> {
  return invoke<boolean>("set_param", { opName, paramName, value });
}

export async function loadProject(path: string): Promise<void> {
  return invoke("load_project", { path });
}

export async function reloadProject(): Promise<void> {
  return invoke("reload_project");
}

export async function toggleVisualizer(): Promise<void> {
  return invoke("toggle_visualizer");
}

export async function getSelectedOperator(): Promise<string | null> {
  return invoke<string | null>("get_selected_operator");
}

export async function selectOperator(name: string): Promise<void> {
  return invoke("select_operator", { name });
}

// Convenience functions
export async function setParamFloat(
  opName: string,
  paramName: string,
  value: number
): Promise<boolean> {
  return setParam(opName, paramName, [value, 0, 0, 0]);
}

export async function setParamVec2(
  opName: string,
  paramName: string,
  x: number,
  y: number
): Promise<boolean> {
  return setParam(opName, paramName, [x, y, 0, 0]);
}

export async function setParamVec3(
  opName: string,
  paramName: string,
  x: number,
  y: number,
  z: number
): Promise<boolean> {
  return setParam(opName, paramName, [x, y, z, 0]);
}

export async function setParamColor(
  opName: string,
  paramName: string,
  r: number,
  g: number,
  b: number,
  a: number = 1.0
): Promise<boolean> {
  return setParam(opName, paramName, [r, g, b, a]);
}
