#pragma once

/**
 * @file snapshot.h
 * @brief Chain-wide parameter snapshots with crossfade interpolation
 *
 * A Snapshot captures all parameter values across every operator in a chain.
 * SnapshotStore manages a list of snapshots with save/recall/crossfade/persist.
 *
 * Storage format: `<project-dir>/vivid-snapshots.json`
 */

#include "easing.h"

#include <string>
#include <vector>
#include <map>
#include <array>

namespace vivid {

class Chain;

/**
 * @brief A captured set of parameter values across all operators
 */
struct Snapshot {
    std::string name;

    /// operator name -> (param name -> float[4])
    std::map<std::string, std::map<std::string, std::array<float, 4>>> values;
};

/**
 * @brief Manages a list of snapshots with crossfade support
 *
 * Snapshots are per-project, saved to `vivid-snapshots.json` in the project directory.
 *
 * @par Example
 * @code
 * auto& store = chain.snapshots();
 * store.capture("Look A", chain);        // save current params
 * store.recall(0, chain, 2.0f);          // crossfade to snapshot 0 over 2s
 * store.update(dt, chain);               // call each frame to tick crossfade
 * store.save("path/to/vivid-snapshots.json");
 * @endcode
 */
class SnapshotStore {
public:
    SnapshotStore() = default;

    // -------------------------------------------------------------------------
    /// @name Capture / Recall
    /// @{

    /**
     * @brief Capture current chain params as a new snapshot
     * @param name Display name for the snapshot
     * @param chain Chain to read params from
     * @return Index of the new snapshot
     */
    int capture(const std::string& name, Chain& chain);

    /**
     * @brief Recall a snapshot by index
     * @param index Snapshot index
     * @param chain Chain to apply params to
     * @param duration Crossfade duration in seconds (0 = hard cut)
     * @param easing Easing curve for crossfade (default: linear)
     */
    void recall(int index, Chain& chain, float duration = 0.0f,
                EasingCurve easing = EasingCurve::linear());

    /**
     * @brief Tick crossfade interpolation (call each frame)
     * @param dt Delta time in seconds
     * @param chain Chain to apply interpolated params to
     */
    void update(float dt, Chain& chain);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Persistence
    /// @{

    /**
     * @brief Save all snapshots to JSON file
     * @param path File path (typically <project-dir>/vivid-snapshots.json)
     * @return true on success
     */
    bool save(const std::string& path) const;

    /**
     * @brief Load snapshots from JSON file
     * @param path File path
     * @return true on success (false if file not found or parse error)
     */
    bool load(const std::string& path);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Management
    /// @{

    /** @brief Get snapshot count */
    [[nodiscard]] int size() const { return static_cast<int>(m_snapshots.size()); }

    /** @brief Get snapshot by index (nullptr if out of range) */
    [[nodiscard]] const Snapshot* get(int index) const;

    /** @brief Get all snapshots */
    [[nodiscard]] const std::vector<Snapshot>& list() const { return m_snapshots; }

    /** @brief Remove a snapshot by index */
    void remove(int index);

    /** @brief Rename a snapshot */
    void rename(int index, const std::string& name);

    /** @brief Move a snapshot from one position to another (reorder) */
    void move(int from, int to);

    /** @brief Get active snapshot index (-1 = none) */
    [[nodiscard]] int activeIndex() const { return m_activeIndex; }

    /** @brief Check if a crossfade is in progress */
    [[nodiscard]] bool isCrossfading() const { return m_crossfading; }

    /** @brief Get crossfade progress (0..1) */
    [[nodiscard]] float crossfadeProgress() const;

    /// @}

private:
    void applySnapshot(const Snapshot& snap, Chain& chain);
    void applyInterpolated(const Snapshot& from, const Snapshot& to, float t, Chain& chain);

    std::vector<Snapshot> m_snapshots;
    int m_activeIndex = -1;

    // Crossfade state
    bool m_crossfading = false;
    int m_crossfadeFrom = -1;
    int m_crossfadeTo = -1;
    float m_crossfadeT = 0.0f;
    float m_crossfadeDuration = 0.0f;

    // Snapshot of params at crossfade start (for interpolating from current state)
    Snapshot m_startSnapshot;

    // Easing curve for current crossfade
    EasingCurve m_easingCurve;
};

} // namespace vivid
