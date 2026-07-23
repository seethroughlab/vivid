#pragma once
// Geometry helpers for operators that produce / modify / consume MESHES as first-class port values.
//
// A mesh flows between nodes as a VividMesh custom-ref (gpu_types.h) on a VIVID_CUSTOM_REF_PORT.
// The host routes these through the value channel (VividGpuContext.custom_inputs / custom_outputs;
// see VisualGraph::run_chain). A PRODUCER owns the VividMesh + its wgpu buffers as persistent op
// members and publishes a pointer each frame; a CONSUMER reads the pointer within the same
// topo-ordered pass (never keep it past process_gpu). These inline accessors keep ops off the raw
// void** arrays and off any assumption about custom-ordinal layout.
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_types.h"

namespace vivid::geom {

// Read the mesh on custom-ref INPUT ordinal `ord` (0 = the node's first custom-ref input port, in
// collect_ports order). Returns nullptr when unwired — the op should render/pass nothing then.
inline const VividMesh* input_mesh(const VividGpuContext* c, uint32_t ord = 0) {
    if (!c || !c->custom_inputs || ord >= c->custom_input_count) return nullptr;
    return static_cast<const VividMesh*>(c->custom_inputs[ord]);
}

// Publish `m` on custom-ref OUTPUT ordinal `ord`. `m` (and the wgpu buffers it references) must be
// owned by the operator and stay valid for the rest of this frame — downstream reads it in the same
// run_chain pass. Typically `m` is a member of the operator struct that it updates in place.
inline void publish_mesh(const VividGpuContext* c, uint32_t ord, VividMesh* m) {
    if (!c || !c->custom_outputs || ord >= c->custom_output_count) return;
    c->custom_outputs[ord] = m;   // custom_outputs is void** const on a const ctx: the pointee is writable
}

}  // namespace vivid::geom
