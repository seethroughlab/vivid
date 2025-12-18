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
    static OperatorRegistry registry;
    return registry;
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
    for (const auto& op : m_operators) {
        if (op.name == name) {
            return &op;
        }
    }
    return nullptr;
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

void OperatorRegistry::outputJson() const {
    json root;
    root["version"] = "1.0.0";
    root["operators"] = json::array();

    for (const auto& meta : m_operators) {
        json op;
        op["name"] = meta.name;
        op["category"] = meta.category;
        op["description"] = meta.description;
        op["addon"] = meta.addon.empty() ? json(nullptr) : json(meta.addon);
        op["requiresInput"] = meta.requiresInput;
        op["outputType"] = outputKindName(meta.outputKind);

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

        root["operators"].push_back(op);
    }

    std::cout << root.dump(2) << std::endl;
}

} // namespace vivid
