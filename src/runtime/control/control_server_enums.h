#pragma once

// Enum → string converters shared across control-server response builders.
// Pure formatting helpers; no runtime state. Extracted from
// control_server_internal.h (Audit 04-R2-F3).

#include "operator_api/types.h"
#include "operator_api/type_id.h"  // vivid_is_custom_port_type
#include "runtime/packages/package_manager.h"  // PackageUpdateClass

namespace vivid {

inline const char* lane_behavior_str(VividLaneBehavior lb) {
    switch (lb) {
        case VIVID_LANE_POINTWISE:  return "pointwise";
        case VIVID_LANE_STRUCTURAL: return "structural";
        case VIVID_LANE_REDUCTION:  return "reduction";
        case VIVID_LANE_KERNEL:     return "kernel";
        default: return "unknown";
    }
}

inline const char* lane_behavior_help_str(VividLaneBehavior lb) {
    switch (lb) {
        case VIVID_LANE_POINTWISE:
            return "Processes each lane independently and preserves per-lane structure. "
                   "Use this in poly chains when you want one stateful copy per note or lane.";
        case VIVID_LANE_STRUCTURAL:
            return "Creates, reorders, or reshapes lane structure. "
                   "Use this to generate or transform polyphonic note/gate lane arrays.";
        case VIVID_LANE_REDUCTION:
            return "Consumes multiple lanes and collapses them into fewer outputs. "
                   "Use this when summing or mixing voices back to a smaller channel count.";
        case VIVID_LANE_KERNEL:
            return "Processes neighborhoods of lanes together. "
                   "Use this for cross-lane operations that depend on nearby lane values.";
        default:
            return "Lane behavior is unknown.";
    }
}

// --- Value model (lane-value clean-break, v6) --------------------------------

inline const char* multiplicity_behavior_str(VividMultiplicityBehavior mb) {
    switch (mb) {
        case VIVID_MULTIPLICITY_SCALAR_ONLY: return "scalar_only";
        case VIVID_MULTIPLICITY_MAP:         return "map";
        case VIVID_MULTIPLICITY_REDUCE:      return "reduce";
        case VIVID_MULTIPLICITY_GENERATE:    return "generate";
        case VIVID_MULTIPLICITY_COLLECT:     return "collect";
        case VIVID_MULTIPLICITY_PRESERVE:    return "preserve";
        case VIVID_MULTIPLICITY_KERNEL:      return "kernel";
        default: return "unknown";
    }
}

inline const char* value_type_str(VividValueType vt) {
    switch (vt) {
        case VIVID_VALUE_FLOAT:   return "float";
        case VIVID_VALUE_AUDIO:   return "audio";
        case VIVID_VALUE_TEXTURE: return "texture";
        case VIVID_VALUE_STRING:  return "string";
        case VIVID_VALUE_CUSTOM:  return "custom";
        default: return "unknown";
    }
}

inline const char* multiplicity_str(VividMultiplicity m) {
    return m == VIVID_MULTIPLICITY_MANY ? "many" : "scalar";
}

// Value-model envelope of a port for MCP probing. Multiplicity is declared on the
// port (lane port types retired, 7d.5e); the payload value_type honors an explicit
// override else derives from the payload port type.
inline VividValueType value_type_for_port(const VividPortDescriptor& pd) {
    if (pd.value_type != VIVID_VALUE_FLOAT) return pd.value_type;  // explicit override
    if (vivid_is_custom_port_type(pd.type)) return VIVID_VALUE_CUSTOM;
    switch (pd.type) {
        case VIVID_PORT_AUDIO_BUFFER: return VIVID_VALUE_AUDIO;
        case VIVID_PORT_STRING:       return VIVID_VALUE_STRING;
        case VIVID_PORT_TEXTURE:      return VIVID_VALUE_TEXTURE;
        default:                      return VIVID_VALUE_FLOAT;  // SCALAR
    }
}

inline VividMultiplicity multiplicity_for_port(const VividPortDescriptor& pd) {
    return pd.multiplicity;
}

inline const char* kind_str(VividOperatorKind k) {
    switch (k) {
        case VIVID_OP_CONTROL: return "control";
        case VIVID_OP_AUDIO:   return "audio";
        case VIVID_OP_GPU:     return "gpu";
        default: return "unknown";
    }
}

inline const char* param_type_str(VividParamType t) {
    switch (t) {
        case VIVID_PARAM_FLOAT: return "float";
        case VIVID_PARAM_INT:   return "int";
        case VIVID_PARAM_BOOL:  return "bool";
        case VIVID_PARAM_FILE:  return "file";
        case VIVID_PARAM_TEXT:  return "text";
        default: return "unknown";
    }
}

inline const char* param_visibility_op_str(VividParamVisibilityOp op) {
    switch (op) {
        case VIVID_PARAM_VIS_ALWAYS: return "always";
        case VIVID_PARAM_VIS_EQ:     return "eq";
        case VIVID_PARAM_VIS_NE:     return "ne";
        default: return "unknown";
    }
}

inline const char* display_hint_str(VividDisplayHint hint) {
    switch (hint) {
        case VIVID_DISPLAY_DEFAULT:  return "default";
        case VIVID_DISPLAY_KNOB:     return "knob";
        case VIVID_DISPLAY_XY_PAD:   return "xy_pad";
        case VIVID_DISPLAY_COLOR:    return "color";
        case VIVID_DISPLAY_ADSR:     return "adsr";
        case VIVID_DISPLAY_LFO:      return "lfo";
        case VIVID_DISPLAY_STEP_SEQ: return "step_seq";
        case VIVID_DISPLAY_HIDDEN:   return "hidden";
        case VIVID_DISPLAY_EDITOR:   return "editor";
        case VIVID_DISPLAY_TRANSIENT:return "transient";
        default: return "unknown";
    }
}

inline const char* port_type_str(VividPortType t) {
    switch (t) {
        case VIVID_PORT_SCALAR:         return "float";
        case VIVID_PORT_AUDIO_BUFFER:         return "audio";
        case VIVID_PORT_STRING:        return "string";
        case VIVID_PORT_TEXTURE:       return "texture";
        default:
            if (vivid_is_custom_port_type(t)) return "custom";
            return "unknown";
    }
}

inline const char* transport_str(VividPortTransport t) {
    switch (t) {
        case VIVID_PORT_TRANSPORT_SIGNAL:        return "scalar";
        case VIVID_PORT_TRANSPORT_AUDIO_BUFFER:  return "audio_buffer";
        case VIVID_PORT_TRANSPORT_STRING:        return "string";
        case VIVID_PORT_TRANSPORT_TEXTURE:       return "texture";
        case VIVID_PORT_TRANSPORT_CUSTOM_VALUE:  return "custom_value";
        case VIVID_PORT_TRANSPORT_CUSTOM_REF:    return "custom_ref";
        default: return "unknown";
    }
}

inline const char* update_class_str(PackageUpdateClass c) {
    switch (c) {
        case PackageUpdateClass::UpToDate: return "up_to_date";
        case PackageUpdateClass::CompatibleUpdate: return "compatible_update";
        case PackageUpdateClass::IncompatibleUpdate: return "incompatible_update";
        case PackageUpdateClass::RemoteOlderOrEqual: return "remote_older_or_equal";
        case PackageUpdateClass::InvalidVersionData: return "invalid_version_data";
        default: return "unknown";
    }
}

} // namespace vivid
