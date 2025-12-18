// Operator Registry Implementation
// Provides JSON output of all registered operators

#include <vivid/operator_registry.h>
#include <iostream>
#include <algorithm>
#include <set>

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

// Helper to escape JSON strings
static std::string escapeJson(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c;
        }
    }
    return result;
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
    std::cout << "{\n";
    std::cout << "  \"version\": \"1.0.0\",\n";
    std::cout << "  \"operators\": [\n";

    bool first = true;
    for (const auto& meta : m_operators) {
        if (!first) std::cout << ",\n";
        first = false;

        std::cout << "    {\n";
        std::cout << "      \"name\": \"" << escapeJson(meta.name) << "\",\n";
        std::cout << "      \"category\": \"" << escapeJson(meta.category) << "\",\n";
        std::cout << "      \"description\": \"" << escapeJson(meta.description) << "\",\n";

        if (!meta.addon.empty()) {
            std::cout << "      \"addon\": \"" << escapeJson(meta.addon) << "\",\n";
        } else {
            std::cout << "      \"addon\": null,\n";
        }

        std::cout << "      \"requiresInput\": " << (meta.requiresInput ? "true" : "false") << ",\n";
        std::cout << "      \"outputType\": \"" << outputKindName(meta.outputKind) << "\",\n";

        // Get params by instantiating a temp operator
        std::cout << "      \"params\": [";

        if (meta.factory) {
            try {
                auto tempOp = meta.factory();
                auto params = tempOp->params();

                bool firstParam = true;
                for (const auto& p : params) {
                    if (!firstParam) std::cout << ",";
                    firstParam = false;

                    std::cout << "\n        {\n";
                    std::cout << "          \"name\": \"" << escapeJson(p.name) << "\",\n";
                    std::cout << "          \"type\": \"" << paramTypeName(p.type) << "\",\n";

                    // Output default value(s)
                    if (p.type == ParamType::String || p.type == ParamType::FilePath) {
                        std::cout << "          \"default\": \"" << escapeJson(p.stringDefault) << "\",\n";
                        if (!p.fileFilter.empty()) {
                            std::cout << "          \"fileFilter\": \"" << escapeJson(p.fileFilter) << "\",\n";
                        }
                        if (!p.fileCategory.empty()) {
                            std::cout << "          \"fileCategory\": \"" << escapeJson(p.fileCategory) << "\",\n";
                        }
                    } else if (p.type == ParamType::Vec2) {
                        std::cout << "          \"default\": [" << p.defaultVal[0] << ", " << p.defaultVal[1] << "],\n";
                    } else if (p.type == ParamType::Vec3) {
                        std::cout << "          \"default\": [" << p.defaultVal[0] << ", " << p.defaultVal[1] << ", " << p.defaultVal[2] << "],\n";
                    } else if (p.type == ParamType::Vec4 || p.type == ParamType::Color) {
                        std::cout << "          \"default\": [" << p.defaultVal[0] << ", " << p.defaultVal[1] << ", " << p.defaultVal[2] << ", " << p.defaultVal[3] << "],\n";
                    } else if (p.type == ParamType::Bool) {
                        std::cout << "          \"default\": " << (p.defaultVal[0] != 0.0f ? "true" : "false") << ",\n";
                    } else if (p.type == ParamType::Int) {
                        std::cout << "          \"default\": " << static_cast<int>(p.defaultVal[0]) << ",\n";
                    } else {
                        std::cout << "          \"default\": " << p.defaultVal[0] << ",\n";
                    }

                    // Output min/max for numeric types
                    if (p.type != ParamType::String && p.type != ParamType::FilePath) {
                        if (p.type == ParamType::Int) {
                            std::cout << "          \"min\": " << static_cast<int>(p.minVal) << ",\n";
                            std::cout << "          \"max\": " << static_cast<int>(p.maxVal) << "\n";
                        } else {
                            std::cout << "          \"min\": " << p.minVal << ",\n";
                            std::cout << "          \"max\": " << p.maxVal << "\n";
                        }
                    } else {
                        // Remove trailing comma for string/filepath types
                        // Seek back and remove the trailing newline and comma
                    }

                    std::cout << "        }";
                }

                if (!params.empty()) {
                    std::cout << "\n      ";
                }
            } catch (...) {
                // Factory failed, no params
            }
        }

        std::cout << "]\n";
        std::cout << "    }";
    }

    if (!m_operators.empty()) {
        std::cout << "\n";
    }
    std::cout << "  ]\n";
    std::cout << "}\n";
}

} // namespace vivid
