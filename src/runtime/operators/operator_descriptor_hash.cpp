#include "runtime/operators/operator_descriptor_hash.h"

#include "common/hash_util.h"
#include "operator_api/types.h"

#include <sstream>
#include <string>

namespace vivid {

namespace {

// Render a possibly-null C string; null becomes empty (distinct from "").
// We fold null and "" together because an operator author would treat them
// the same semantically.
std::string str(const char* s) { return s ? s : ""; }

void append_param(std::ostringstream& os, size_t i, const VividParamDescriptor& p) {
    os << "param[" << i << "]: "
       << "name=" << str(p.name)
       << " type=" << p.type
       << " default=" << p.default_value
       << " min="     << p.min_value
       << " max="     << p.max_value
       << " display_hint=" << p.display_hint
       << " tag="     << str(p.semantic_tag)
       << " shape="   << str(p.semantic_shape)
       << " unit="    << str(p.semantic_unit)
       << " intent="  << str(p.semantic_intent)
       << " asset_kind=" << str(p.asset_kind)
       << " widget_id="  << str(p.widget_id)
       << " default_string=" << str(p.default_string)
       << "\n";
    os << "param[" << i << "].choices: " << p.choice_count;
    for (uint32_t c = 0; c < p.choice_count; ++c) {
        os << "," << str(p.choice_labels ? p.choice_labels[c] : nullptr);
    }
    os << "\n";
}

void append_port(std::ostringstream& os, size_t i, const VividPortDescriptor& p) {
    os << "port[" << i << "]: "
       << "name="      << str(p.name)
       << " type="     << p.type
       << " direction=" << p.direction
       << " transport=" << p.transport
       << " channels=" << static_cast<int>(p.channels)
       << " default="  << p.default_value
       << " payload_size=" << p.payload_size
       << " type_name="    << str(p.type_name)
       << " stable_type_id=" << str(p.stable_type_id)
       << " tag="      << str(p.semantic_tag)
       << " shape="    << str(p.semantic_shape)
       << " intent="   << str(p.semantic_intent)
       << "\n";
}

}  // namespace

std::string operator_descriptor_hash(const VividOperatorDescriptor* desc) {
    if (!desc) return {};

    std::ostringstream os;
    os << "name=" << str(desc->name) << "\n";
    os << "flags=time:" << desc->time_dependent
       << " audio:"     << desc->has_process_audio
       << " gpu:"       << desc->has_process_gpu
       << " frame:"     << desc->has_process_frame
       << " multiplicity_behavior:" << static_cast<int>(desc->multiplicity_behavior)
       << " strategy_independent:" << desc->strategy_independent
       << "\n";

    os << "params=" << desc->param_count << "\n";
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        append_param(os, i, desc->params[i]);
    }
    os << "ports=" << desc->port_count << "\n";
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        append_port(os, i, desc->ports[i]);
    }

    return "sha256:" + sha256_hex(os.str());
}

}  // namespace vivid
