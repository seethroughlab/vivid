# Operator API Audit Report (2026-04-30)

## Executive Summary

The Vivid Operator API is a highly successful implementation of "Developer Experience (DX) first" design. It achieves its goal of making operator authoring extremely declarative and easy, but it does so by hiding significant complexity within large macros that could become a maintenance burden.

---

## 1. Strengths (High DX)

*   **Declarative Parameter Interface:** The `vivid::Param<T>` template combined with the fluent API (`display_hint`, `param_group`, etc.) allows developers to define parameters and their UI metadata in a highly readable, chained way.
*   **Unified Capability Model:** Using simple trait-like interfaces (`FrameProcessable`, `AudioProcessable`, `GpuProcessable`) clearly separates concerns and allows the runtime to dispatch work efficiently based on what the operator actually does.
*   **Automated Boilerplate:** The `VIVID_REGISTER` macro performs massive amounts of heavy lifting: managing the `extern "C"` boundary, automating parameter collection/syncing, and detecting capabilities at compile-time using `std::is_base_of_v`.

## 2. Weaknesses (Technical Risk)

*   **The "Black Box" Macro:** The `VIVID_REGISTER` macro is extremely complex. While it reduces boilerplate for the user, any failure within this macro (e.g., during registration or parameter syncing) will be incredibly difficult for an operator developer to debug, as the logic is expanded at compile-time and exists outside the standard C++ call stack.
*   **Fragile State Management:** The macro manages complex, static state (like `s_params` and `s_label_storage`) within a helper function to ensure stability across dylib reloads. This is clever but sensitive; any error in how these vectors are resized or how string pointers are managed could lead to memory corruption or crashes during hot-reloading.
*   **Fragile CPU/GPU Contract:** While the API handles parameter syncing, it cannot enforce the strict structural contract required between C++ `Uniforms` structs and WGSL shaders. This remains a primary point of failure for developers (e.g., alignment mismities in `NoiseUniforms`).

## 3. Recommendations

*   **Refactor Registration:** Move as much logic as possible out of the `VIVID_REGISTER` macro and into transparent, debuggable C++ functions or a registration factory class. The goal should be to reduce the macro to a simple "entry point" rather than an entire engine component.
*   **Shader-to-C++ Tooling:** To mitigate the fragility of the WGSL/C++ struct contract, consider implementing a lightweight build-time tool that parses `.wgsl` shaders and generates the corresponding C++ `struct` definitions to ensure perfect alignment and type safety.
*   **Simplify Error Handling:** Further abstract the WebGPU error scope management (currently manually handled in `lazy_init`) into the higher-level GPU utility functions to further reduce developer friction.

---

## Final Audit Verdict

| Metric | Score |
| :--- | :--- |
| **Developer Experience (DX)** | 9/10 |
| **Stability** | 7/10 |
| **Maintainability** | 6/10 |
