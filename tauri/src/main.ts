import { Terminal } from "@xterm/xterm";
import { FitAddon } from "@xterm/addon-fit";
import { WebLinksAddon } from "@xterm/addon-web-links";
import "@xterm/xterm/css/xterm.css";
import * as monaco from "monaco-editor";
import { open, save } from "@tauri-apps/plugin-dialog";
import { vividConnection, type VividMessage, type ConnectionStatus } from "./vivid-connection";

// Tauri API - will be available at runtime
declare global {
  interface Window {
    __TAURI__: {
      core: {
        invoke: <T>(cmd: string, args?: Record<string, unknown>) => Promise<T>;
      };
      event: {
        listen: <T>(
          event: string,
          handler: (event: { payload: T }) => void
        ) => Promise<() => void>;
      };
      window: {
        getCurrentWindow: () => {
          startDragging: () => Promise<void>;
        };
      };
    };
  }
}

const { invoke } = window.__TAURI__.core;
const { listen } = window.__TAURI__.event;
const { getCurrentWindow } = window.__TAURI__.window;

// Global terminal state
let terminal: Terminal | null = null;
let fitAddon: FitAddon | null = null;
let sessionId: number | null = null;

// Global editor state
let editor: monaco.editor.IStandaloneCodeEditor | null = null;
let currentFilePath: string | null = null;
let isModified: boolean = false;

// Initialize the application
async function init() {
  console.log("Vivid IDE initializing...");

  // Set up window dragging on title bar
  setupWindowDragging();

  // Set up panel toggles
  setupPanelToggles();

  // Set up parameter sliders
  setupSliders();

  // Initialize terminal with xterm.js
  await initTerminal();

  // Initialize Monaco editor
  initEditor();

  // Initialize Vivid connection
  initVividConnection();

  // Set up input forwarding to wgpu/egui
  setupInputForwarding();

  // Update resolution display
  updateResolution();
  window.addEventListener("resize", updateResolution);
}

function setupWindowDragging() {
  const titlebar = document.querySelector(".titlebar");
  if (titlebar) {
    titlebar.addEventListener("mousedown", async (e) => {
      // Only drag on left mouse button and not on interactive elements
      if ((e as MouseEvent).button === 0) {
        const target = e.target as HTMLElement;
        if (!target.closest("button") && !target.closest("input")) {
          try {
            await getCurrentWindow().startDragging();
          } catch (err) {
            console.error("Failed to start dragging:", err);
          }
        }
      }
    });
  }
}

function setupPanelToggles() {
  const inspectorToggle = document.getElementById("toggle-inspector");
  const terminalToggle = document.getElementById("toggle-terminal");
  const editorToggle = document.getElementById("toggle-editor");
  const inspectorPanel = document.getElementById("inspector-panel");
  const terminalPanel = document.getElementById("terminal-panel");
  const editorPanel = document.getElementById("editor-panel");

  inspectorToggle?.addEventListener("click", () => {
    inspectorPanel?.classList.toggle("collapsed");
    if (inspectorToggle) {
      inspectorToggle.textContent = inspectorPanel?.classList.contains("collapsed") ? "+" : "−";
    }
  });

  terminalToggle?.addEventListener("click", () => {
    terminalPanel?.classList.toggle("collapsed");
    if (terminalToggle) {
      terminalToggle.textContent = terminalPanel?.classList.contains("collapsed") ? "+" : "−";
    }
    // Resize terminal when panel is toggled
    setTimeout(() => fitAddon?.fit(), 100);
  });

  editorToggle?.addEventListener("click", () => {
    editorPanel?.classList.toggle("collapsed");
    if (editorToggle) {
      editorToggle.textContent = editorPanel?.classList.contains("collapsed") ? "+" : "−";
    }
    // Resize editor when panel is toggled
    setTimeout(() => editor?.layout(), 100);
  });
}

function setupSliders() {
  const sliders = ["scale", "speed", "octaves"];

  sliders.forEach((param) => {
    const slider = document.getElementById(`param-${param}`) as HTMLInputElement;
    const valueDisplay = document.getElementById(`value-${param}`);

    if (slider && valueDisplay) {
      slider.addEventListener("input", () => {
        const value = parseFloat(slider.value);
        valueDisplay.textContent = param === "octaves" ? value.toString() : value.toFixed(1);

        // TODO: Send to Rust backend via Tauri command
        console.log(`Parameter ${param} changed to ${value}`);
      });
    }
  });
}

// Forward mouse/scroll events to wgpu/egui for node graph interaction
function setupInputForwarding() {
  // Forward all mouse events to egui since it renders behind the entire webview.
  // The webview panels will still receive events first due to DOM structure.
  // We forward from document.body to catch events in transparent areas.

  // Mouse move - forward position for hover effects
  document.addEventListener("mousemove", (e) => {
    // Don't forward if over a panel (they handle their own events)
    const target = e.target as HTMLElement;
    if (target.closest(".panel") || target.closest(".titlebar") || target.closest(".statusbar")) {
      return;
    }
    invoke("input_mouse_move", { x: e.clientX, y: e.clientY }).catch(() => {});
  });

  // Mouse buttons - forward for click/drag interactions
  document.addEventListener("mousedown", (e) => {
    const target = e.target as HTMLElement;
    if (target.closest(".panel") || target.closest(".titlebar") || target.closest(".statusbar")) {
      return;
    }
    invoke("input_mouse_button", { button: e.button, pressed: true }).catch(() => {});
  });

  document.addEventListener("mouseup", (e) => {
    // Always forward mouseup to handle drag release
    invoke("input_mouse_button", { button: e.button, pressed: false }).catch(() => {});
  });

  // Scroll/wheel - forward for zooming and panning
  document.addEventListener("wheel", (e) => {
    const target = e.target as HTMLElement;
    if (target.closest(".panel") || target.closest(".titlebar") || target.closest(".statusbar")) {
      return;
    }
    // Prevent default scroll behavior when over node graph area
    e.preventDefault();
    invoke("input_scroll", { dx: e.deltaX, dy: e.deltaY }).catch(() => {});
  }, { passive: false });

  console.log("Input forwarding to egui enabled");
}

async function initTerminal() {
  const terminalContainer = document.getElementById("terminal");
  if (!terminalContainer) return;

  terminal = new Terminal({
    fontFamily: '"SF Mono", "Monaco", "Consolas", monospace',
    fontSize: 13,
    lineHeight: 1.4,
    cursorBlink: true,
    cursorStyle: "bar",
    theme: {
      background: "transparent",
      foreground: "#e4e4e7",
      cursor: "#6366f1",
      cursorAccent: "#18181b",
      selectionBackground: "#6366f133",
      black: "#18181b",
      red: "#f87171",
      green: "#4ade80",
      yellow: "#facc15",
      blue: "#60a5fa",
      magenta: "#c084fc",
      cyan: "#22d3ee",
      white: "#e4e4e7",
      brightBlack: "#52525b",
      brightRed: "#fca5a5",
      brightGreen: "#86efac",
      brightYellow: "#fde047",
      brightBlue: "#93c5fd",
      brightMagenta: "#d8b4fe",
      brightCyan: "#67e8f9",
      brightWhite: "#fafafa",
    },
    allowTransparency: true,
    scrollback: 10000,
  });

  fitAddon = new FitAddon();
  const webLinksAddon = new WebLinksAddon();

  terminal.loadAddon(fitAddon);
  terminal.loadAddon(webLinksAddon);

  terminal.open(terminalContainer);
  fitAddon.fit();

  // Get terminal dimensions
  const { rows, cols } = terminal;

  try {
    // Spawn a shell session
    sessionId = await invoke<number>("spawn_shell", { rows, cols });
    console.log(`Shell session started with ID: ${sessionId}`);

    // Listen for PTY output
    await listen<[number, string]>("pty-output", (event) => {
      const [sid, data] = event.payload;
      if (sid === sessionId && terminal) {
        terminal.write(data);
      }
    });

    // Listen for PTY exit
    await listen<number>("pty-exit", (event) => {
      if (event.payload === sessionId && terminal) {
        terminal.writeln("\r\n\x1b[38;5;245m[Shell session ended]\x1b[0m");
        sessionId = null;
      }
    });

    // Send terminal input to PTY
    terminal.onData(async (data) => {
      if (sessionId !== null) {
        try {
          await invoke("write_pty", { sessionId, data });
        } catch (e) {
          console.error("Failed to write to PTY:", e);
        }
      }
    });

    // Handle resize
    const handleResize = async () => {
      if (fitAddon && terminal) {
        fitAddon.fit();
        if (sessionId !== null) {
          try {
            await invoke("resize_pty", {
              sessionId,
              rows: terminal.rows,
              cols: terminal.cols,
            });
          } catch (e) {
            console.error("Failed to resize PTY:", e);
          }
        }
      }
    };

    window.addEventListener("resize", handleResize);

    // Also resize when terminal panel is toggled
    const terminalPanel = document.getElementById("terminal-panel");
    if (terminalPanel) {
      const observer = new MutationObserver(() => {
        setTimeout(handleResize, 100);
      });
      observer.observe(terminalPanel, { attributes: true, attributeFilter: ["class"] });
    }

  } catch (e) {
    console.error("Failed to spawn shell:", e);
    // Fall back to showing an error message
    terminal.writeln("\x1b[38;5;196m╭─────────────────────────────────────────╮\x1b[0m");
    terminal.writeln("\x1b[38;5;196m│\x1b[0m   \x1b[1;38;5;196mFailed to start shell\x1b[0m                \x1b[38;5;196m│\x1b[0m");
    terminal.writeln("\x1b[38;5;196m╰─────────────────────────────────────────╯\x1b[0m");
    terminal.writeln("");
    terminal.writeln(`\x1b[38;5;245mError: ${e}\x1b[0m`);
  }
}

function updateResolution() {
  const resDisplay = document.getElementById("resolution");
  if (resDisplay) {
    resDisplay.textContent = `${window.innerWidth} × ${window.innerHeight}`;
  }
}

// Define WGSL language for Monaco
function registerWGSLLanguage() {
  monaco.languages.register({ id: "wgsl" });

  monaco.languages.setMonarchTokensProvider("wgsl", {
    keywords: [
      "fn", "let", "var", "const", "return", "if", "else", "for", "while", "loop",
      "break", "continue", "switch", "case", "default", "struct", "type", "alias",
      "true", "false", "discard", "enable", "override", "diagnostic"
    ],
    typeKeywords: [
      "bool", "i32", "u32", "f32", "f16",
      "vec2", "vec3", "vec4", "mat2x2", "mat3x3", "mat4x4",
      "sampler", "texture_2d", "texture_3d", "texture_cube",
      "array", "ptr", "atomic"
    ],
    builtins: [
      "abs", "acos", "asin", "atan", "atan2", "ceil", "clamp", "cos", "cross",
      "degrees", "distance", "dot", "exp", "exp2", "floor", "fract", "length",
      "log", "log2", "max", "min", "mix", "normalize", "pow", "radians", "reflect",
      "round", "sign", "sin", "smoothstep", "sqrt", "step", "tan", "trunc",
      "textureSample", "textureSampleLevel", "textureLoad", "textureStore"
    ],
    operators: [
      "=", ">", "<", "!", "~", "?", ":", "==", "<=", ">=", "!=",
      "&&", "||", "++", "--", "+", "-", "*", "/", "&", "|", "^", "%",
      "<<", ">>", "+=", "-=", "*=", "/=", "&=", "|=", "^=", "%=", "<<=", ">>="
    ],
    symbols: /[=><!~?:&|+\-*\/\^%]+/,
    tokenizer: {
      root: [
        [/@[a-zA-Z_]\w*/, "annotation"],
        [/[a-zA-Z_]\w*/, {
          cases: {
            "@keywords": "keyword",
            "@typeKeywords": "type",
            "@builtins": "predefined",
            "@default": "identifier"
          }
        }],
        { include: "@whitespace" },
        [/[{}()\[\]]/, "@brackets"],
        [/[<>](?!@symbols)/, "@brackets"],
        [/@symbols/, {
          cases: {
            "@operators": "operator",
            "@default": ""
          }
        }],
        [/\d*\.\d+([eE][\-+]?\d+)?[fh]?/, "number.float"],
        [/0[xX][0-9a-fA-F]+[iu]?/, "number.hex"],
        [/\d+[iu]?/, "number"],
        [/[;,.]/, "delimiter"],
        [/"([^"\\]|\\.)*$/, "string.invalid"],
        [/"/, { token: "string.quote", bracket: "@open", next: "@string" }],
      ],
      whitespace: [
        [/[ \t\r\n]+/, "white"],
        [/\/\*/, "comment", "@comment"],
        [/\/\/.*$/, "comment"],
      ],
      comment: [
        [/[^\/*]+/, "comment"],
        [/\/\*/, "comment", "@push"],
        ["\\*/", "comment", "@pop"],
        [/[\/*]/, "comment"]
      ],
      string: [
        [/[^\\"]+/, "string"],
        [/\\./, "string.escape"],
        [/"/, { token: "string.quote", bracket: "@close", next: "@pop" }]
      ],
    }
  });
}

// Initialize Monaco editor
function initEditor() {
  const editorContainer = document.getElementById("editor");
  if (!editorContainer) return;

  // Register WGSL language
  registerWGSLLanguage();

  // Define dark theme
  monaco.editor.defineTheme("vivid-dark", {
    base: "vs-dark",
    inherit: true,
    rules: [
      { token: "comment", foreground: "6A9955" },
      { token: "keyword", foreground: "C586C0" },
      { token: "type", foreground: "4EC9B0" },
      { token: "predefined", foreground: "DCDCAA" },
      { token: "annotation", foreground: "D7BA7D" },
      { token: "number", foreground: "B5CEA8" },
      { token: "string", foreground: "CE9178" },
    ],
    colors: {
      "editor.background": "#00000000",
      "editor.lineHighlightBackground": "#ffffff10",
      "editorLineNumber.foreground": "#6e6e6e",
      "editorCursor.foreground": "#6366f1",
      "editor.selectionBackground": "#6366f133",
    }
  });

  editor = monaco.editor.create(editorContainer, {
    value: "// Open a file to edit\n// Supported: .cpp, .h, .hpp, .wgsl\n",
    language: "cpp",
    theme: "vivid-dark",
    fontFamily: '"SF Mono", "Monaco", "Consolas", monospace',
    fontSize: 13,
    lineHeight: 20,
    minimap: { enabled: false },
    scrollBeyondLastLine: false,
    automaticLayout: true,
    padding: { top: 8, bottom: 8 },
    renderLineHighlight: "line",
    cursorBlinking: "smooth",
    cursorSmoothCaretAnimation: "on",
  });

  // Track modifications
  editor.onDidChangeModelContent(() => {
    if (!isModified) {
      isModified = true;
      updateEditorStatus();
    }
  });

  // Setup file buttons
  const openBtn = document.getElementById("open-file");
  const saveBtn = document.getElementById("save-file");

  openBtn?.addEventListener("click", openFile);
  saveBtn?.addEventListener("click", saveFile);

  // Keyboard shortcut: Cmd/Ctrl+S to save
  editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, saveFile);

  // Handle resize
  window.addEventListener("resize", () => editor?.layout());
}

// Get language from file extension
function getLanguageForFile(path: string): string {
  const ext = path.split(".").pop()?.toLowerCase();
  switch (ext) {
    case "cpp":
    case "c":
    case "cc":
    case "cxx":
    case "h":
    case "hpp":
    case "hxx":
      return "cpp";
    case "wgsl":
      return "wgsl";
    case "glsl":
    case "vert":
    case "frag":
      return "glsl";
    case "js":
    case "mjs":
      return "javascript";
    case "ts":
    case "mts":
      return "typescript";
    case "json":
      return "json";
    default:
      return "plaintext";
  }
}

// Update filename display
function updateFilenameDisplay() {
  const filenameEl = document.getElementById("editor-filename");
  if (filenameEl) {
    if (currentFilePath) {
      const filename = currentFilePath.split("/").pop() || currentFilePath;
      filenameEl.textContent = isModified ? `● ${filename}` : filename;
      filenameEl.classList.add("has-file");
    } else {
      filenameEl.textContent = "No file open";
      filenameEl.classList.remove("has-file");
    }
  }
}

// Update editor status in statusbar
function updateEditorStatus() {
  const statusEl = document.getElementById("editor-status");
  if (statusEl) {
    if (currentFilePath && isModified) {
      statusEl.textContent = "Modified";
      statusEl.classList.add("modified");
    } else {
      statusEl.textContent = "";
      statusEl.classList.remove("modified");
    }
  }
  updateFilenameDisplay();
}

// Open file
async function openFile() {
  try {
    const selected = await open({
      multiple: false,
      filters: [
        { name: "C++ Files", extensions: ["cpp", "h", "hpp", "c", "cc"] },
        { name: "WGSL Shaders", extensions: ["wgsl"] },
        { name: "All Files", extensions: ["*"] }
      ]
    });

    if (selected && typeof selected === "string") {
      const content = await invoke<string>("read_file", { path: selected });
      currentFilePath = selected;
      isModified = false;

      const language = getLanguageForFile(selected);
      const model = monaco.editor.createModel(content, language);
      editor?.setModel(model);

      updateEditorStatus();
      console.log(`Opened: ${selected}`);
    }
  } catch (e) {
    console.error("Failed to open file:", e);
  }
}

// Save file
async function saveFile() {
  if (!editor) return;

  try {
    let path = currentFilePath;

    // If no file open, show save dialog
    if (!path) {
      const selected = await save({
        filters: [
          { name: "C++ Files", extensions: ["cpp", "h", "hpp"] },
          { name: "WGSL Shaders", extensions: ["wgsl"] },
          { name: "All Files", extensions: ["*"] }
        ]
      });

      if (!selected) return;
      path = selected;
    }

    const content = editor.getValue();
    await invoke("write_file", { path, content });
    currentFilePath = path;
    isModified = false;
    updateEditorStatus();
    console.log(`Saved: ${path}`);
  } catch (e) {
    console.error("Failed to save file:", e);
  }
}

// --- Vivid Connection ---

function initVividConnection() {
  // Update status indicator on connection status change
  vividConnection.onStatusChange(updateVividStatus);

  // Handle messages from Vivid
  vividConnection.onMessage(handleVividMessage);

  // Start connection
  vividConnection.connect();
}

function updateVividStatus(status: ConnectionStatus) {
  const statusEl = document.getElementById("status");
  if (!statusEl) return;

  switch (status) {
    case "connected":
      statusEl.textContent = "Vivid Connected";
      statusEl.className = "status connected";
      break;
    case "connecting":
      statusEl.textContent = "Connecting...";
      statusEl.className = "status connecting";
      break;
    case "disconnected":
      statusEl.textContent = "Vivid Disconnected";
      statusEl.className = "status disconnected";
      break;
  }
}

function handleVividMessage(message: VividMessage) {
  switch (message.type) {
    case "compile_status":
      handleCompileStatus(message);
      break;
    case "frame_info":
      updateFpsDisplay(message.fps);
      break;
    case "operator_list":
      console.log("Received operator list:", message.operators.length, "operators");
      // TODO: Update node graph (Phase 4)
      break;
    case "param_values":
      console.log("Received param values:", message.params.length, "params");
      // TODO: Update inspector panel (Phase 5)
      break;
    case "chain_structure":
      console.log("Received chain structure:", message.operators.length, "operators");
      // TODO: Update node graph (Phase 4)
      break;
    case "pending_changes":
      if (message.hasChanges) {
        console.log("Pending changes:", message.changes.length);
      }
      break;
    case "performance_stats":
      updateFpsDisplay(message.fps);
      break;
    default:
      // Log other messages for debugging
      console.log("Vivid message:", message);
  }
}

function handleCompileStatus(status: { success: boolean; message: string }) {
  if (status.success) {
    console.log("Vivid chain compiled successfully");
  } else {
    console.error("Vivid compile error:", status.message);
    // Could show error in editor or terminal
  }
}

function updateFpsDisplay(fps: number) {
  const fpsEl = document.getElementById("fps");
  if (fpsEl) {
    fpsEl.textContent = `${Math.round(fps)} FPS`;
  }
}

// Start the app
init().catch(console.error);
