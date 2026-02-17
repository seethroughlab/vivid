// Snapshot — Chain-wide parameter capture and crossfade interpolation

#include <vivid/snapshot.h>
#include <vivid/chain.h>
#include <vivid/operator.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

namespace vivid {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Capture all params from a chain into a Snapshot
static Snapshot captureChainParams(const std::string& name, Chain& chain) {
    Snapshot snap;
    snap.name = name;

    for (const auto& opName : chain.operatorNames()) {
        Operator* op = chain.getByName(opName);
        if (!op) continue;

        auto decls = op->params();
        if (decls.empty()) continue;

        auto& opMap = snap.values[opName];
        for (const auto& decl : decls) {
            std::array<float, 4> val = {0, 0, 0, 0};
            if (op->getParam(decl.name, val.data())) {
                opMap[decl.name] = val;
            }
        }
    }

    return snap;
}

/// Check if a param type should be interpolated (lerp) or snapped at midpoint
static bool isInterpolatable(ParamType type) {
    switch (type) {
        case ParamType::Float:
        case ParamType::Int:
        case ParamType::Vec2:
        case ParamType::Vec3:
        case ParamType::Vec4:
        case ParamType::Color:
        case ParamType::ADSR:
            return true;
        case ParamType::String:
        case ParamType::FilePath:
        case ParamType::Enum:
        case ParamType::DeviceList:
        case ParamType::Bool:
        default:
            return false;
    }
}

/// Determine component count for lerping
static int componentCount(ParamType type) {
    switch (type) {
        case ParamType::Vec2:  return 2;
        case ParamType::Vec3:  return 3;
        case ParamType::Vec4:
        case ParamType::Color:
        case ParamType::ADSR:  return 4;
        default:               return 1;
    }
}

// ---------------------------------------------------------------------------
// SnapshotStore
// ---------------------------------------------------------------------------

int SnapshotStore::capture(const std::string& name, Chain& chain) {
    m_snapshots.push_back(captureChainParams(name, chain));
    return static_cast<int>(m_snapshots.size()) - 1;
}

void SnapshotStore::recall(int index, Chain& chain, float duration, EasingCurve easing) {
    if (index < 0 || index >= static_cast<int>(m_snapshots.size())) return;

    if (duration <= 0.0f) {
        // Hard cut — apply immediately
        applySnapshot(m_snapshots[index], chain);
        m_activeIndex = index;
        m_crossfading = false;
        return;
    }

    // Start crossfade from current state
    m_startSnapshot = captureChainParams("_crossfade_start", chain);
    m_crossfadeFrom = m_activeIndex;
    m_crossfadeTo = index;
    m_crossfadeT = 0.0f;
    m_crossfadeDuration = duration;
    m_easingCurve = easing;
    m_crossfading = true;
}

void SnapshotStore::update(float dt, Chain& chain) {
    if (!m_crossfading) return;

    m_crossfadeT += dt / m_crossfadeDuration;

    if (m_crossfadeT >= 1.0f) {
        // Crossfade complete — apply final values exactly
        m_crossfadeT = 1.0f;
        applySnapshot(m_snapshots[m_crossfadeTo], chain);
        m_activeIndex = m_crossfadeTo;
        m_crossfading = false;
        return;
    }

    // Interpolate between start snapshot and target (apply easing to linear t)
    float easedT = m_easingCurve.apply(m_crossfadeT);
    applyInterpolated(m_startSnapshot, m_snapshots[m_crossfadeTo], easedT, chain);
}

float SnapshotStore::crossfadeProgress() const {
    if (!m_crossfading) return 0.0f;
    return m_crossfadeT;
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

void SnapshotStore::applySnapshot(const Snapshot& snap, Chain& chain) {
    for (const auto& [opName, params] : snap.values) {
        Operator* op = chain.getByName(opName);
        if (!op) continue;

        for (const auto& [paramName, val] : params) {
            op->setParam(paramName, val.data());
        }
    }
}

void SnapshotStore::applyInterpolated(const Snapshot& from, const Snapshot& to,
                                       float t, Chain& chain) {
    // Build a set of all operators referenced by either snapshot
    std::map<std::string, Operator*> ops;
    for (const auto& [opName, _] : from.values) {
        Operator* op = chain.getByName(opName);
        if (op) ops[opName] = op;
    }
    for (const auto& [opName, _] : to.values) {
        Operator* op = chain.getByName(opName);
        if (op) ops[opName] = op;
    }

    for (auto& [opName, op] : ops) {
        auto decls = op->params();

        for (const auto& decl : decls) {
            // Look up values in both snapshots
            std::array<float, 4> fromVal = {0, 0, 0, 0};
            std::array<float, 4> toVal = {0, 0, 0, 0};

            bool hasFrom = false, hasTo = false;

            auto fromIt = from.values.find(opName);
            if (fromIt != from.values.end()) {
                auto paramIt = fromIt->second.find(decl.name);
                if (paramIt != fromIt->second.end()) {
                    fromVal = paramIt->second;
                    hasFrom = true;
                }
            }

            auto toIt = to.values.find(opName);
            if (toIt != to.values.end()) {
                auto paramIt = toIt->second.find(decl.name);
                if (paramIt != toIt->second.end()) {
                    toVal = paramIt->second;
                    hasTo = true;
                }
            }

            if (!hasFrom && !hasTo) continue;

            // If only one side has the param, read current value for the other
            if (!hasFrom) {
                op->getParam(decl.name, fromVal.data());
            }
            if (!hasTo) {
                op->getParam(decl.name, toVal.data());
            }

            if (isInterpolatable(decl.type)) {
                // Lerp component-wise
                int n = componentCount(decl.type);
                float result[4] = {0, 0, 0, 0};
                for (int i = 0; i < n; i++) {
                    result[i] = fromVal[i] + (toVal[i] - fromVal[i]) * t;
                }
                // Int params: round to nearest
                if (decl.type == ParamType::Int) {
                    result[0] = std::round(result[0]);
                }
                op->setParam(decl.name, result);
            } else {
                // Non-interpolatable: snap at midpoint
                if (t < 0.5f) {
                    op->setParam(decl.name, fromVal.data());
                } else {
                    op->setParam(decl.name, toVal.data());
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Management
// ---------------------------------------------------------------------------

const Snapshot* SnapshotStore::get(int index) const {
    if (index < 0 || index >= static_cast<int>(m_snapshots.size())) return nullptr;
    return &m_snapshots[index];
}

void SnapshotStore::remove(int index) {
    if (index < 0 || index >= static_cast<int>(m_snapshots.size())) return;
    m_snapshots.erase(m_snapshots.begin() + index);

    // Adjust active index
    if (m_activeIndex == index) {
        m_activeIndex = -1;
    } else if (m_activeIndex > index) {
        m_activeIndex--;
    }
}

void SnapshotStore::rename(int index, const std::string& name) {
    if (index < 0 || index >= static_cast<int>(m_snapshots.size())) return;
    m_snapshots[index].name = name;
}

void SnapshotStore::move(int from, int to) {
    if (from < 0 || from >= static_cast<int>(m_snapshots.size())) return;
    if (to < 0 || to >= static_cast<int>(m_snapshots.size())) return;
    if (from == to) return;

    Snapshot snap = std::move(m_snapshots[from]);
    m_snapshots.erase(m_snapshots.begin() + from);
    m_snapshots.insert(m_snapshots.begin() + to, std::move(snap));

    // Adjust active index
    if (m_activeIndex == from) {
        m_activeIndex = to;
    } else if (from < to) {
        if (m_activeIndex > from && m_activeIndex <= to) m_activeIndex--;
    } else {
        if (m_activeIndex >= to && m_activeIndex < from) m_activeIndex++;
    }
}

// ---------------------------------------------------------------------------
// Persistence (JSON)
// ---------------------------------------------------------------------------

bool SnapshotStore::save(const std::string& path) const {
    try {
        json j;
        j["snapshots"] = json::array();

        for (const auto& snap : m_snapshots) {
            json jSnap;
            jSnap["name"] = snap.name;
            jSnap["values"] = json::object();

            for (const auto& [opName, params] : snap.values) {
                json jOp = json::object();
                for (const auto& [paramName, val] : params) {
                    jOp[paramName] = {val[0], val[1], val[2], val[3]};
                }
                jSnap["values"][opName] = jOp;
            }

            j["snapshots"].push_back(jSnap);
        }

        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Snapshot] Failed to save: " << e.what() << std::endl;
        return false;
    }
}

bool SnapshotStore::load(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        json j = json::parse(file);

        m_snapshots.clear();
        m_activeIndex = -1;
        m_crossfading = false;

        if (!j.contains("snapshots") || !j["snapshots"].is_array()) return false;

        for (const auto& jSnap : j["snapshots"]) {
            Snapshot snap;
            snap.name = jSnap.value("name", "Untitled");

            if (jSnap.contains("values") && jSnap["values"].is_object()) {
                for (auto& [opName, jOp] : jSnap["values"].items()) {
                    if (!jOp.is_object()) continue;
                    auto& opMap = snap.values[opName];
                    for (auto& [paramName, jVal] : jOp.items()) {
                        if (!jVal.is_array() || jVal.size() < 4) continue;
                        std::array<float, 4> val = {
                            jVal[0].get<float>(),
                            jVal[1].get<float>(),
                            jVal[2].get<float>(),
                            jVal[3].get<float>()
                        };
                        opMap[paramName] = val;
                    }
                }
            }

            m_snapshots.push_back(std::move(snap));
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Snapshot] Failed to load: " << e.what() << std::endl;
        return false;
    }
}

} // namespace vivid
