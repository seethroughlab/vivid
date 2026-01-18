import { Terminal } from "@xterm/xterm";
import { FitAddon } from "@xterm/addon-fit";
import { WebLinksAddon } from "@xterm/addon-web-links";
import "@xterm/xterm/css/xterm.css";
import * as monaco from "monaco-editor";
import { open, save } from "@tauri-apps/plugin-dialog";
import * as vivid from "./vivid-api";

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

// Global vivid state
let operators: vivid.OperatorInfo[] = [];
let selectedOperator: string | null = null;

// --- Persistent Layout ---

interface LayoutState {
  terminalCollapsed: boolean;
  inspectorCollapsed: boolean;
  editorCollapsed: boolean;
}

const LAYOUT_STORAGE_KEY = "vivid-ide-layout";

function saveLayout() {
  const terminalPanel = document.getElementById("terminal-panel");
  const inspectorPanel = document.getElementById("inspector-panel");
  const editorPanel = document.getElementById("editor-panel");

  const layout: LayoutState = {
    terminalCollapsed: terminalPanel?.classList.contains("collapsed") ?? false,
    inspectorCollapsed: inspectorPanel?.classList.contains("collapsed") ?? false,
    editorCollapsed: editorPanel?.classList.contains("collapsed") ?? false,
  };

  localStorage.setItem(LAYOUT_STORAGE_KEY, JSON.stringify(layout));
}

function restoreLayout() {
  try {
    const stored = localStorage.getItem(LAYOUT_STORAGE_KEY);
    if (!stored) return;

    const layout: LayoutState = JSON.parse(stored);

    const terminalPanel = document.getElementById("terminal-panel");
    const inspectorPanel = document.getElementById("inspector-panel");
    const editorPanel = document.getElementById("editor-panel");
    const terminalToggle = document.getElementById("toggle-terminal");
    const inspectorToggle = document.getElementById("toggle-inspector");
    const editorToggle = document.getElementById("toggle-editor");

    if (layout.terminalCollapsed) {
      terminalPanel?.classList.add("collapsed");
      if (terminalToggle) terminalToggle.textContent = "+";
    }

    if (layout.inspectorCollapsed) {
      inspectorPanel?.classList.add("collapsed");
      if (inspectorToggle) inspectorToggle.textContent = "+";
    }

    if (layout.editorCollapsed) {
      editorPanel?.classList.add("collapsed");
      if (editorToggle) editorToggle.textContent = "+";
    }

    console.log("[Vivid] Layout restored:", layout);
  } catch (e) {
    console.error("[Vivid] Failed to restore layout:", e);
  }
}

// Initialize the application
async function init() {
  console.log("Vivid IDE initializing...");

  // Restore saved layout before anything else
  restoreLayout();

  // Set up window dragging on title bar
  setupWindowDragging();

  // Set up file menu
  setupFileMenu();

  // Set up panel toggles
  setupPanelToggles();

  // Initialize terminal with xterm.js
  await initTerminal();

  // Initialize Monaco editor
  initEditor();

  // Initialize Vivid state polling
  await initVividState();

  // Set up error banner click handlers
  setupErrorBanner();

  // Set up input forwarding to wgpu/egui
  setupInputForwarding();

  // Set up keyboard shortcuts
  setupKeyboardShortcuts();

  // Set up menu event listeners
  setupMenuListeners();

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
        if (!target.closest("button") && !target.closest("input") && !target.closest(".menu-item")) {
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

// --- File Menu ---

function setupFileMenu() {
  const fileMenu = document.getElementById("file-menu");
  const fileDropdown = document.getElementById("file-dropdown");

  if (!fileMenu || !fileDropdown) return;

  // Toggle menu on click
  fileMenu.querySelector(".menu-label")?.addEventListener("click", (e) => {
    e.stopPropagation();
    fileMenu.classList.toggle("open");
  });

  // Close menu when clicking outside
  document.addEventListener("click", (e) => {
    if (!fileMenu.contains(e.target as Node)) {
      fileMenu.classList.remove("open");
    }
  });

  // Close menu on escape
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") {
      fileMenu.classList.remove("open");
    }
  });

  // Menu option handlers
  document.getElementById("menu-new-project")?.addEventListener("click", async () => {
    fileMenu.classList.remove("open");
    await newProject();
  });

  document.getElementById("menu-open-project")?.addEventListener("click", async () => {
    fileMenu.classList.remove("open");
    await openProject();
  });

  document.getElementById("menu-open-file")?.addEventListener("click", async () => {
    fileMenu.classList.remove("open");
    await openFile();
  });

  document.getElementById("menu-save-file")?.addEventListener("click", async () => {
    fileMenu.classList.remove("open");
    await saveFile();
  });
}

async function newProject() {
  try {
    // Ask for project name first
    const projectName = await promptProjectName();
    if (!projectName) return;

    // Ask where to save the project
    const parentDir = await open({
      directory: true,
      multiple: false,
      title: "Choose location for new project",
    });

    if (!parentDir || typeof parentDir !== "string") return;

    const projectPath = `${parentDir}/${projectName}`;
    console.log("[Vivid] Creating new project at:", projectPath);

    // Create the project using Tauri command (calls `vivid new` CLI)
    // This creates the full project structure with AGENTS.md, BRIEF.md, etc.
    await invoke("create_project", {
      path: projectPath,
      name: projectName,
      template: "blank"  // TODO: Add template picker UI
    });

    // Load the new project
    await vivid.loadProject(projectPath);
    currentProjectPath = projectPath;
    updateProjectTitle();

    // Wait for project to initialize, then refresh state
    await new Promise(resolve => setTimeout(resolve, 500));
    await refreshVividState();

    console.log("[Vivid] New project created successfully");
  } catch (e) {
    console.error("[Vivid] Failed to create new project:", e);
    // Show error to user
    alert(`Failed to create project: ${e}`);
  }
}

// Simple prompt for project name using a dialog
async function promptProjectName(): Promise<string | null> {
  // For now, use a simple browser prompt
  // TODO: Could be replaced with a custom modal
  const name = window.prompt("Enter project name:", "my-project");
  if (!name || name.trim() === "") return null;

  // Sanitize the name (remove special characters, replace spaces with hyphens)
  const sanitized = name.trim()
    .toLowerCase()
    .replace(/[^a-z0-9\s-]/g, "")
    .replace(/\s+/g, "-");

  return sanitized || null;
}

// --- Open Project ---

// Current project path (for title display)
let currentProjectPath: string | null = null;

async function openProject() {
  try {
    // Open folder dialog
    const selected = await open({
      directory: true,
      multiple: false,
      title: "Open Vivid Project",
    });

    if (selected && typeof selected === "string") {
      console.log("[Vivid] Opening project:", selected);

      // Load the project
      await vivid.loadProject(selected);
      currentProjectPath = selected;

      // Update the title bar
      updateProjectTitle();

      // Clear the current file in editor (will be reloaded)
      currentFilePath = null;

      // Wait a moment for the project to initialize, then refresh state
      await new Promise(resolve => setTimeout(resolve, 500));
      await refreshVividState();

      console.log("[Vivid] Project opened successfully");
    }
  } catch (e) {
    console.error("[Vivid] Failed to open project:", e);
  }
}

function updateProjectTitle() {
  const titleEl = document.getElementById("title");
  if (titleEl) {
    if (currentProjectPath) {
      // Extract just the project folder name from the path
      const projectName = currentProjectPath.split("/").pop() || currentProjectPath;
      titleEl.innerHTML = `Vivid — <span class="project-name">${projectName}</span>`;
    } else {
      titleEl.textContent = "Vivid";
    }
  }
}

function setupPanelToggles() {
  const inspectorToggle = document.getElementById("toggle-inspector");
  const terminalToggle = document.getElementById("toggle-terminal");
  const editorToggle = document.getElementById("toggle-editor");
  const inspectorPanel = document.getElementById("inspector-panel");
  const terminalPanel = document.getElementById("terminal-panel");
  const editorPanel = document.getElementById("editor-panel");

  inspectorToggle?.addEventListener("click", () => toggleInspector());
  terminalToggle?.addEventListener("click", () => toggleTerminal());
  editorToggle?.addEventListener("click", () => toggleEditor());
}

function toggleTerminal() {
  const terminalPanel = document.getElementById("terminal-panel");
  const terminalToggle = document.getElementById("toggle-terminal");
  terminalPanel?.classList.toggle("collapsed");
  if (terminalToggle) {
    terminalToggle.textContent = terminalPanel?.classList.contains("collapsed") ? "+" : "−";
  }
  // Resize terminal when panel is toggled
  setTimeout(() => fitAddon?.fit(), 100);
  saveLayout();
}

function toggleInspector() {
  const inspectorPanel = document.getElementById("inspector-panel");
  const inspectorToggle = document.getElementById("toggle-inspector");
  inspectorPanel?.classList.toggle("collapsed");
  if (inspectorToggle) {
    inspectorToggle.textContent = inspectorPanel?.classList.contains("collapsed") ? "+" : "−";
  }
  saveLayout();
}

function toggleEditor() {
  const editorPanel = document.getElementById("editor-panel");
  const editorToggle = document.getElementById("toggle-editor");
  editorPanel?.classList.toggle("collapsed");
  if (editorToggle) {
    editorToggle.textContent = editorPanel?.classList.contains("collapsed") ? "+" : "−";
  }
  // Resize editor when panel is toggled
  setTimeout(() => editor?.layout(), 100);
  saveLayout();
}

// Forward mouse/scroll events to vivid-core for node graph interaction
function setupInputForwarding() {
  console.log("[Vivid] Setting up input forwarding");
  let lastLogTime = 0;

  // Mouse move - forward position for hover effects
  document.addEventListener("mousemove", (e) => {
    // Don't forward if over a panel (they handle their own events)
    const target = e.target as HTMLElement;
    if (target.closest(".panel") || target.closest(".titlebar") || target.closest(".statusbar")) {
      return;
    }
    // Log occasionally to avoid spam
    const now = Date.now();
    if (now - lastLogTime > 1000) {
      console.log("[Vivid] Forwarding mouse move:", e.clientX, e.clientY);
      lastLogTime = now;
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
    console.log("[Vivid] Forwarding scroll:", e.deltaX, e.deltaY);
    invoke("input_scroll", { dx: e.deltaX, dy: e.deltaY }).catch((err) => {
      console.error("[Vivid] input_scroll failed:", err);
    });
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

    // Trigger hot-reload if this is a chain source file
    if (path.endsWith(".cpp") || path.endsWith(".h") || path.endsWith(".hpp")) {
      try {
        await vivid.reloadProject();
        console.log("Hot-reload triggered");
        // Refresh state after reload to pick up any changes
        setTimeout(() => refreshVividState(), 500);
      } catch (reloadErr) {
        console.error("Hot-reload failed:", reloadErr);
      }
    }
  } catch (e) {
    console.error("Failed to save file:", e);
  }
}

// --- Vivid State Management ---

async function initVividState() {
  console.log("[Vivid] initVividState() starting...");

  // Wait a bit for vivid-core to initialize (it waits 30 frames after window ready)
  // At 60fps, that's ~500ms, so we wait 1 second to be safe
  console.log("[Vivid] Waiting 1.5s for vivid-core to initialize...");
  await new Promise(resolve => setTimeout(resolve, 1500));

  // Initial load of project info
  await refreshVividState();

  // Poll for updates periodically (compile status, operators, etc.)
  window.setInterval(async () => {
    await refreshVividState();
  }, 2000);

  // Faster polling for selection sync (100ms for responsive Inspector updates)
  window.setInterval(async () => {
    await syncSelectedOperator();
  }, 100);

  // Update status indicator
  updateVividStatus("connected");
  console.log("[Vivid] initVividState() complete");
}

async function refreshVividState() {
  console.log("[Vivid] refreshVividState() called");
  try {
    // Get project info and load chain.cpp if available
    console.log("[Vivid] Calling get_project_info...");
    const projectInfo = await vivid.getProjectInfo();
    console.log("[Vivid] Project info:", projectInfo);

    if (projectInfo.loaded && projectInfo.chain_path) {
      console.log("[Vivid] Project loaded, chain path:", projectInfo.chain_path);

      // Update the project title
      if (projectInfo.project_path) {
        currentProjectPath = projectInfo.project_path;
        updateProjectTitle();
      }

      // Auto-load chain.cpp in editor if no file is open
      if (!currentFilePath) {
        await loadFileInEditor(projectInfo.chain_path);
      }
    } else {
      console.log("[Vivid] No project loaded yet");
    }

    // Get operators and update inspector
    console.log("[Vivid] Calling get_operators...");
    operators = await vivid.getOperators();
    console.log("[Vivid] Got operators:", operators);
    updateOperatorList();

    // Sync selection from vivid-core visualizer
    await syncSelectedOperator();

    // Check compile status
    await checkCompileStatus();
  } catch (e) {
    console.error("[Vivid] Failed to refresh vivid state:", e);
  }
}

// Sync the selected operator from vivid-core's visualizer to the webview Inspector
async function syncSelectedOperator() {
  try {
    const coreSelection = await vivid.getSelectedOperator();

    // If the selection changed in vivid-core, update our Inspector
    if (coreSelection !== selectedOperator) {
      if (coreSelection) {
        await selectOperator(coreSelection);
      } else if (selectedOperator) {
        // Core has no selection, clear ours
        selectedOperator = null;
        updateOperatorList();
        const container = document.getElementById("param-controls");
        if (container) {
          container.innerHTML = '<div class="no-params">Select an operator</div>';
        }
      }
    }
  } catch (e) {
    // Silently ignore polling errors
  }
}

async function checkCompileStatus() {
  try {
    const status = await vivid.getCompileStatus();
    handleCompileStatus(status);
  } catch (e) {
    // Ignore errors during polling
  }
}

// Track current error state
let currentError: { line: number; column: number; message: string } | null = null;

function handleCompileStatus(status: vivid.CompileStatusInfo) {
  const statusEl = document.getElementById("compile-status");
  const errorBanner = document.getElementById("error-banner");
  const errorMessage = document.getElementById("error-message");
  const errorLocation = document.getElementById("error-location");
  const editorPanel = document.getElementById("editor-panel");

  if (status.success) {
    // Compilation succeeded - clear errors
    if (statusEl) {
      statusEl.textContent = "✓ Compiled";
      statusEl.className = "compile-status success";
      // Fade out the success message after 3 seconds
      setTimeout(() => {
        if (statusEl.textContent === "✓ Compiled") {
          statusEl.textContent = "";
          statusEl.className = "compile-status";
        }
      }, 3000);
    }

    // Hide error banner
    errorBanner?.classList.add("hidden");
    editorPanel?.classList.remove("has-error");

    // Clear editor markers
    clearEditorErrors();
    currentError = null;
  } else {
    // Compilation failed - show error
    currentError = {
      line: status.error_line || 1,
      column: status.error_column || 1,
      message: status.message || "Compilation failed"
    };

    if (statusEl) {
      statusEl.textContent = "Compile Error";
      statusEl.className = "compile-status error";
    }

    // Show error banner with details
    if (errorBanner && errorMessage) {
      // Parse error message to extract just the core message
      const msg = currentError.message;
      // Try to extract the actual error (often after "error: ")
      const errorMatch = msg.match(/error:\s*(.+?)(?:\n|$)/i);
      const displayMessage = errorMatch ? errorMatch[1] : msg;

      errorMessage.textContent = displayMessage;

      if (errorLocation) {
        if (status.error_line) {
          errorLocation.textContent = `Line ${status.error_line}${status.error_column ? `:${status.error_column}` : ""}`;
          errorLocation.style.display = "inline";
        } else {
          errorLocation.style.display = "none";
        }
      }

      errorBanner.classList.remove("hidden");
    }

    // Add error styling to editor panel
    editorPanel?.classList.add("has-error");

    // Highlight error in editor
    if (editor && currentError.line) {
      highlightErrorInEditor(currentError.line, currentError.column, currentError.message);
    }
  }
}

function setupErrorBanner() {
  const errorBanner = document.getElementById("error-banner");
  const errorDismiss = document.getElementById("error-dismiss");

  // Click on banner to jump to error
  errorBanner?.addEventListener("click", (e) => {
    // Don't jump if clicking dismiss button
    if ((e.target as HTMLElement).id === "error-dismiss") return;

    if (currentError && editor) {
      jumpToError(currentError.line, currentError.column);
    }
  });

  // Dismiss button
  errorDismiss?.addEventListener("click", (e) => {
    e.stopPropagation();
    const errorBanner = document.getElementById("error-banner");
    const editorPanel = document.getElementById("editor-panel");
    errorBanner?.classList.add("hidden");
    editorPanel?.classList.remove("has-error");
  });
}

function jumpToError(line: number, column: number) {
  if (!editor) return;

  // Reveal the line and set cursor
  editor.revealLineInCenter(line);
  editor.setPosition({ lineNumber: line, column: column });
  editor.focus();

  // Add a brief highlight animation
  const model = editor.getModel();
  if (model) {
    const decorations = editor.createDecorationsCollection([
      {
        range: new monaco.Range(line, 1, line, model.getLineMaxColumn(line)),
        options: {
          className: "error-line-flash",
          isWholeLine: true,
        }
      }
    ]);

    // Remove the flash decoration after animation
    setTimeout(() => {
      decorations.clear();
    }, 1000);
  }
}

function highlightErrorInEditor(line: number, column: number, message: string) {
  if (!editor) return;

  // Add error decoration
  const model = editor.getModel();
  if (model) {
    monaco.editor.setModelMarkers(model, "vivid", [
      {
        severity: monaco.MarkerSeverity.Error,
        message: message,
        startLineNumber: line,
        startColumn: column,
        endLineNumber: line,
        endColumn: column + 1,
      },
    ]);
  }
}

function clearEditorErrors() {
  if (!editor) return;
  const model = editor.getModel();
  if (model) {
    monaco.editor.setModelMarkers(model, "vivid", []);
  }
}

function updateVividStatus(status: "connected" | "disconnected") {
  const statusEl = document.getElementById("status");
  if (!statusEl) return;

  if (status === "connected") {
    statusEl.textContent = "Vivid Active";
    statusEl.className = "status connected";
  } else {
    statusEl.textContent = "Vivid Inactive";
    statusEl.className = "status disconnected";
  }
}

// --- Operator List / Inspector ---

function updateOperatorList() {
  console.log("[Vivid] updateOperatorList called with", operators.length, "operators");
  const listEl = document.getElementById("operator-list");
  if (!listEl) {
    console.error("[Vivid] operator-list element not found!");
    return;
  }

  console.log("[Vivid] Found operator-list element:", listEl);
  listEl.innerHTML = "";

  for (const op of operators) {
    console.log("[Vivid] Adding operator:", op.name, op.type_name);
    const item = document.createElement("div");
    item.className = "operator-item" + (op.name === selectedOperator ? " selected" : "");
    item.innerHTML = `
      <span class="op-name">${op.name}</span>
      <span class="op-type">${op.type_name}</span>
    `;
    item.addEventListener("click", () => selectOperator(op.name));
    listEl.appendChild(item);
  }
  console.log("[Vivid] Operator list updated, children:", listEl.children.length);
}

async function selectOperator(name: string) {
  selectedOperator = name;
  updateOperatorList();

  // Tell vivid-core to select this operator in the visualizer
  try {
    await vivid.selectOperator(name);
  } catch (e) {
    console.error("Failed to select operator in vivid-core:", e);
  }

  // Load params for this operator
  try {
    const params = await vivid.getOperatorParams(name);
    updateParamControls(name, params);
  } catch (e) {
    console.error("Failed to get operator params:", e);
  }
}

function updateParamControls(opName: string, params: vivid.ParamInfo[]) {
  const container = document.getElementById("param-controls");
  if (!container) return;

  container.innerHTML = "";

  if (params.length === 0) {
    container.innerHTML = '<div class="no-params">No parameters</div>';
    return;
  }

  for (const param of params) {
    const control = createParamControl(opName, param);
    container.appendChild(control);
  }
}

function createParamControl(opName: string, param: vivid.ParamInfo): HTMLElement {
  const wrapper = document.createElement("div");
  wrapper.className = "param-control";

  const label = document.createElement("label");
  label.textContent = param.name;
  wrapper.appendChild(label);

  // Create appropriate control based on param type
  switch (param.param_type) {
    case "Float":
    case "Int": {
      const slider = document.createElement("input");
      slider.type = "range";
      slider.min = String(param.min_val);
      slider.max = String(param.max_val);
      slider.step = param.param_type === "Int" ? "1" : "0.01";
      slider.value = String(param.value[0]);

      const valueDisplay = document.createElement("span");
      valueDisplay.className = "param-value";
      valueDisplay.textContent = param.param_type === "Int"
        ? String(Math.round(param.value[0]))
        : param.value[0].toFixed(2);

      slider.addEventListener("input", async () => {
        const value = parseFloat(slider.value);
        valueDisplay.textContent = param.param_type === "Int"
          ? String(Math.round(value))
          : value.toFixed(2);
        await vivid.setParamFloat(opName, param.name, value);
      });

      wrapper.appendChild(slider);
      wrapper.appendChild(valueDisplay);
      break;
    }

    case "Bool": {
      const checkbox = document.createElement("input");
      checkbox.type = "checkbox";
      checkbox.checked = param.value[0] > 0.5;

      checkbox.addEventListener("change", async () => {
        await vivid.setParamFloat(opName, param.name, checkbox.checked ? 1.0 : 0.0);
      });

      wrapper.appendChild(checkbox);
      break;
    }

    case "Color": {
      const colorInput = document.createElement("input");
      colorInput.type = "color";
      // Convert float [0-1] to hex
      const r = Math.round(param.value[0] * 255);
      const g = Math.round(param.value[1] * 255);
      const b = Math.round(param.value[2] * 255);
      colorInput.value = `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`;

      colorInput.addEventListener("input", async () => {
        const hex = colorInput.value;
        const r = parseInt(hex.slice(1, 3), 16) / 255;
        const g = parseInt(hex.slice(3, 5), 16) / 255;
        const b = parseInt(hex.slice(5, 7), 16) / 255;
        await vivid.setParamColor(opName, param.name, r, g, b, param.value[3]);
      });

      wrapper.appendChild(colorInput);
      break;
    }

    case "Enum": {
      const select = document.createElement("select");
      param.enum_labels.forEach((labelText, i) => {
        const option = document.createElement("option");
        option.value = String(i);
        option.textContent = labelText;
        if (Math.round(param.value[0]) === i) {
          option.selected = true;
        }
        select.appendChild(option);
      });

      select.addEventListener("change", async () => {
        await vivid.setParamFloat(opName, param.name, parseInt(select.value));
      });

      wrapper.appendChild(select);
      break;
    }

    default: {
      // For Vec2, Vec3, Vec4, show multiple sliders
      const components = param.param_type === "Vec2" ? 2 :
                         param.param_type === "Vec3" ? 3 :
                         param.param_type === "Vec4" ? 4 : 1;

      for (let i = 0; i < components; i++) {
        const row = document.createElement("div");
        row.className = "vec-component";

        const compLabel = document.createElement("span");
        compLabel.textContent = ["x", "y", "z", "w"][i];
        row.appendChild(compLabel);

        const slider = document.createElement("input");
        slider.type = "range";
        slider.min = String(param.min_val);
        slider.max = String(param.max_val);
        slider.step = "0.01";
        slider.value = String(param.value[i]);

        const valueDisplay = document.createElement("span");
        valueDisplay.className = "param-value";
        valueDisplay.textContent = param.value[i].toFixed(2);

        const componentIndex = i;
        slider.addEventListener("input", async () => {
          const newValue: [number, number, number, number] = [...param.value] as [number, number, number, number];
          newValue[componentIndex] = parseFloat(slider.value);
          valueDisplay.textContent = newValue[componentIndex].toFixed(2);
          await vivid.setParam(opName, param.name, newValue);
        });

        row.appendChild(slider);
        row.appendChild(valueDisplay);
        wrapper.appendChild(row);
      }
      break;
    }
  }

  return wrapper;
}

// --- Menu Event Listeners ---

async function setupMenuListeners() {
  console.log("[Vivid] Setting up menu listeners");

  await listen<string>("menu-action", async (event) => {
    const action = event.payload;
    console.log("[Vivid] Menu action:", action);

    switch (action) {
      case "new_project":
        await newProject();
        break;
      case "open_project":
        await openProject();
        break;
      case "open_file":
        await openFile();
        break;
      case "save":
        await saveFile();
        break;
      case "reload":
        try {
          await vivid.reloadProject();
          await refreshVividState();
          console.log("Project reloaded via menu");
        } catch (err) {
          console.error("Failed to reload:", err);
        }
        break;
      case "toggle_terminal":
        toggleTerminal();
        break;
      case "toggle_inspector":
        toggleInspector();
        break;
      case "toggle_editor":
        toggleEditor();
        break;
    }
  });
}

// --- Keyboard Shortcuts ---

function setupKeyboardShortcuts() {
  console.log("[Vivid] Setting up keyboard shortcuts");
  document.addEventListener("keydown", async (e) => {
    // Tab - toggle visualizer
    if (e.key === "Tab" && !e.ctrlKey && !e.metaKey && !e.altKey) {
      // Only if not focused on input elements
      const target = e.target as HTMLElement;
      console.log("[Vivid] Tab pressed, target:", target.tagName, target.className);
      if (!target.closest("input, textarea, .xterm, .monaco-editor")) {
        e.preventDefault();
        console.log("[Vivid] Calling toggleVisualizer...");
        try {
          await vivid.toggleVisualizer();
          console.log("[Vivid] toggleVisualizer succeeded");
        } catch (err) {
          console.error("[Vivid] toggleVisualizer failed:", err);
        }
      } else {
        console.log("[Vivid] Tab ignored - inside editor/terminal");
      }
    }

    // Cmd+N / Ctrl+N - new project
    if ((e.metaKey || e.ctrlKey) && e.key === "n") {
      e.preventDefault();
      await newProject();
    }

    // Cmd+O / Ctrl+O - open project
    if ((e.metaKey || e.ctrlKey) && e.key === "o") {
      e.preventDefault();
      await openProject();
    }

    // Cmd+E / Ctrl+E - toggle editor overlay
    if ((e.metaKey || e.ctrlKey) && e.key === "e") {
      const target = e.target as HTMLElement;
      // Don't intercept if we're in an input or the terminal
      if (!target.closest("input, textarea, .xterm")) {
        e.preventDefault();
        toggleEditor();
      }
    }

    // Cmd+1 / Ctrl+1 - toggle terminal panel
    if ((e.metaKey || e.ctrlKey) && e.key === "1") {
      e.preventDefault();
      toggleTerminal();
    }

    // Cmd+2 / Ctrl+2 - toggle parameters/inspector panel
    if ((e.metaKey || e.ctrlKey) && e.key === "2") {
      e.preventDefault();
      toggleInspector();
    }

    // Cmd+3 / Ctrl+3 - toggle editor panel
    if ((e.metaKey || e.ctrlKey) && e.key === "3") {
      e.preventDefault();
      toggleEditor();
    }

    // Cmd+R / Ctrl+R - reload project (when not in editor)
    if ((e.metaKey || e.ctrlKey) && e.key === "r") {
      const target = e.target as HTMLElement;
      if (!target.closest(".monaco-editor")) {
        e.preventDefault();
        try {
          await vivid.reloadProject();
          await refreshVividState();
          console.log("Project reloaded");
        } catch (err) {
          console.error("Failed to reload:", err);
        }
      }
    }
  });
}

// --- File Loading ---

async function loadFileInEditor(path: string) {
  try {
    const content = await invoke<string>("read_file", { path });
    currentFilePath = path;
    isModified = false;

    const language = getLanguageForFile(path);
    const model = monaco.editor.createModel(content, language);
    editor?.setModel(model);

    clearEditorErrors();
    updateEditorStatus();
    console.log(`Loaded: ${path}`);
  } catch (e) {
    console.error("Failed to load file:", e);
  }
}

// Start the app
init().catch(console.error);
