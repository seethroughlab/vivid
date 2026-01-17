import { defineConfig } from "vite";
import monacoEditorPlugin from "vite-plugin-monaco-editor";

export default defineConfig({
  clearScreen: false,
  plugins: [
    (monacoEditorPlugin as any).default({
      languageWorkers: ["editorWorkerService", "css", "html", "json", "typescript"],
      customWorkers: [],
    }),
  ],
  server: {
    port: 5173,
    strictPort: true,
    watch: {
      ignored: ["**/src-tauri/**"],
    },
  },
});
