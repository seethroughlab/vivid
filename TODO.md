[x] What are the advantages of using a VS Code extension for visualization of the chain instead of having the runtime launch its own visualization window?

**Decision:** Hybrid approach - VS Code extension for lightweight features (compile errors, reload commands, status bar) + runtime visualization window for **view-only inspection** (node graph, previews, performance metrics). All parameter changes happen in code to keep Vivid LLM-first and text-based.

[x] Currently, on MacOS, the runtime is compiled into an app package, which makes it confusing to run it from the command line. I believe interacting with it CLI is going to be the most common way, so I think it would make more sense to hav eit be a regular executable, not in an app bundle. What are the advantages and disadvantages of doing this?

**Decision:** Changed to regular executable. Removed `MACOSX_BUNDLE` from CMakeLists.txt. The vivid runtime is now at `build/runtime/vivid` instead of `build/runtime/vivid.app/Contents/MacOS/vivid`. Assets are copied alongside the executable.