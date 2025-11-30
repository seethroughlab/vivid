# vivid-imgui addon (PLACEHOLDER)
#
# STATUS: Not yet functional due to WebGPU API version mismatch.
# See JEFF_NOTES.txt for resolution options.
#
# ImGUI's imgui_impl_wgpu.cpp expects an older WebGPU API than
# wgpu-native v24.0.0.2 provides. Resolution options:
# 1. Patch imgui_impl_wgpu.cpp for new API
# 2. Downgrade wgpu-native
# 3. Wait for ImGUI to update their backend
#
# The WebGPU access API has been added to Context in preparation.

cmake_minimum_required(VERSION 3.20)

# Placeholder - do not use this addon yet
function(vivid_use_imgui TARGET)
    message(WARNING "vivid-imgui: Not yet implemented - see JEFF_NOTES.txt for status")
endfunction()
