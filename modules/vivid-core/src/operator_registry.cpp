// Operator Registry Implementation
// Provides JSON output of all registered operators

#include <vivid/operator_registry.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>
#include <set>

using json = nlohmann::json;

namespace vivid {

OperatorRegistry& OperatorRegistry::instance() {
    // Use heap allocation to avoid static destruction order issues.
    // The registry is accessed during static initialization (operator registration)
    // and must outlive all static OperatorRegistrar objects. A leaky singleton
    // ensures it's never destroyed, preventing use-after-free crashes on exit.
    static OperatorRegistry* registry = new OperatorRegistry();
    return *registry;
}

void OperatorRegistry::registerOperator(const OperatorMeta& meta) {
    m_operators.push_back(meta);
}

std::vector<const OperatorMeta*> OperatorRegistry::operatorsByCategory(const std::string& category) const {
    std::vector<const OperatorMeta*> result;
    for (const auto& op : m_operators) {
        if (op.category == category) {
            result.push_back(&op);
        }
    }
    return result;
}

std::vector<std::string> OperatorRegistry::categories() const {
    std::set<std::string> cats;
    for (const auto& op : m_operators) {
        cats.insert(op.category);
    }
    return std::vector<std::string>(cats.begin(), cats.end());
}

const OperatorMeta* OperatorRegistry::find(const std::string& name) const {
    // First check exact name match
    for (const auto& op : m_operators) {
        if (op.name == name) {
            return &op;
        }
    }
    // Then check aliases
    for (const auto& op : m_operators) {
        for (const auto& alias : op.aliases) {
            if (alias == name) {
                return &op;
            }
        }
    }
    return nullptr;
}

OperatorMeta* OperatorRegistry::findMutable(const std::string& name) {
    for (auto& op : m_operators) {
        if (op.name == name) {
            return &op;
        }
    }
    return nullptr;
}

void OperatorRegistry::setUsage(const std::string& name, const std::string& usage) {
    if (auto* meta = findMutable(name)) {
        meta->usage = usage;
    }
}

void OperatorRegistry::setAliases(const std::string& name, std::vector<std::string> aliases) {
    if (auto* meta = findMutable(name)) {
        meta->aliases = std::move(aliases);
    }
}

void OperatorRegistry::setInputs(const std::string& name, std::vector<InputMeta> inputs) {
    if (auto* meta = findMutable(name)) {
        meta->inputs = std::move(inputs);
    }
}

// Helper to convert ParamType to string
static const char* paramTypeName(ParamType type) {
    switch (type) {
        case ParamType::Float:    return "Float";
        case ParamType::Int:      return "Int";
        case ParamType::Bool:     return "Bool";
        case ParamType::Vec2:     return "Vec2";
        case ParamType::Vec3:     return "Vec3";
        case ParamType::Vec4:     return "Vec4";
        case ParamType::Color:    return "Color";
        case ParamType::String:   return "String";
        case ParamType::FilePath: return "FilePath";
        default:                  return "Unknown";
    }
}

// Helper to convert a single OperatorMeta to JSON
static json operatorMetaToJson(const OperatorMeta& meta) {
    json op;
    op["name"] = meta.name;
    op["category"] = meta.category;
    op["description"] = meta.description;
    op["module"] = meta.module.empty() ? json(nullptr) : json(meta.module);
    op["requiresInput"] = meta.requiresInput;
    op["outputType"] = outputKindName(meta.outputKind);

    // Extended metadata for LLM/MCP
    if (!meta.usage.empty()) {
        op["usage"] = meta.usage;
    }
    if (!meta.aliases.empty()) {
        op["aliases"] = meta.aliases;
    }
    if (!meta.inputs.empty()) {
        op["inputs"] = json::array();
        for (const auto& input : meta.inputs) {
            op["inputs"].push_back({
                {"method", input.method},
                {"description", input.description},
                {"required", input.required}
            });
        }
    }
    if (!meta.examples.empty()) {
        op["examples"] = json::array();
        for (const auto& ex : meta.examples) {
            json example;
            // Resolve module-relative paths to full paths
            std::string path = ex.path;
            if (!meta.module.empty() &&
                path.find("modules/") != 0 &&
                path.find("projects/") != 0 &&
                path.find("../") != 0) {
                // Module-relative path: prepend modules/<module>/
                path = "modules/" + meta.module + "/" + path;
            }
            example["path"] = path;
            if (!ex.description.empty()) {
                example["description"] = ex.description;
            }
            op["examples"].push_back(example);
        }
    }

    // Get params by instantiating a temp operator
    op["params"] = json::array();

    if (meta.factory) {
        try {
            auto tempOp = meta.factory();
            auto params = tempOp->params();

            for (const auto& p : params) {
                json param;
                param["name"] = p.name;
                param["type"] = paramTypeName(p.type);

                // Output default value(s) based on type
                if (p.type == ParamType::String || p.type == ParamType::FilePath) {
                    param["default"] = p.stringDefault;
                    if (!p.fileFilter.empty()) {
                        param["fileFilter"] = p.fileFilter;
                    }
                    if (!p.fileCategory.empty()) {
                        param["fileCategory"] = p.fileCategory;
                    }
                } else if (p.type == ParamType::Vec2) {
                    param["default"] = {p.defaultVal[0], p.defaultVal[1]};
                } else if (p.type == ParamType::Vec3) {
                    param["default"] = {p.defaultVal[0], p.defaultVal[1], p.defaultVal[2]};
                } else if (p.type == ParamType::Vec4 || p.type == ParamType::Color) {
                    param["default"] = {p.defaultVal[0], p.defaultVal[1], p.defaultVal[2], p.defaultVal[3]};
                } else if (p.type == ParamType::Bool) {
                    param["default"] = (p.defaultVal[0] != 0.0f);
                } else if (p.type == ParamType::Int) {
                    param["default"] = static_cast<int>(p.defaultVal[0]);
                } else {
                    param["default"] = p.defaultVal[0];
                }

                // Output min/max for numeric types
                if (p.type != ParamType::String && p.type != ParamType::FilePath) {
                    if (p.type == ParamType::Int) {
                        param["min"] = static_cast<int>(p.minVal);
                        param["max"] = static_cast<int>(p.maxVal);
                    } else {
                        param["min"] = p.minVal;
                        param["max"] = p.maxVal;
                    }
                }

                op["params"].push_back(param);
            }
        } catch (...) {
            // Factory failed, no params
        }
    }

    return op;
}

json OperatorRegistry::toJson() const {
    json root;
    root["version"] = "1.0.0";
    root["operators"] = json::array();

    for (const auto& meta : m_operators) {
        root["operators"].push_back(operatorMetaToJson(meta));
    }

    return root;
}

json OperatorRegistry::toJsonGrouped() const {
    json result = json::object();

    for (const auto& meta : m_operators) {
        if (result.find(meta.category) == result.end()) {
            result[meta.category] = json::array();
        }
        result[meta.category].push_back(operatorMetaToJson(meta));
    }

    return result;
}

void OperatorRegistry::outputJson() const {
    std::cout << toJson().dump(2) << std::endl;
}

} // namespace vivid
