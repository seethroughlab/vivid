#pragma once

#include <string>
#include <optional>
#include <filesystem>

namespace vivid::storage {

/**
 * @brief Simple persistent key/value storage backed by JSON file.
 *
 * Provides easy storage for project settings, preferences, and cached data.
 * Data is stored in a JSON file and persists across application restarts.
 *
 * Usage:
 *   Storage store("settings.json");
 *   store.set("volume", 0.8f);
 *   store.set("lastFile", "/path/to/file.fbx");
 *   store.save();
 *
 *   // Later...
 *   float volume = store.get<float>("volume", 1.0f);  // 1.0f is default
 *   std::string path = store.getString("lastFile", "");
 */
class Storage {
public:
    /**
     * @brief Create or load a storage file.
     * @param path Path to the JSON file (created if doesn't exist).
     */
    explicit Storage(const std::string& path);
    ~Storage();

    // Non-copyable
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    Storage(Storage&&) noexcept;
    Storage& operator=(Storage&&) noexcept;

    /**
     * @brief Load data from file (called automatically in constructor).
     * @return true if file was loaded successfully.
     */
    bool load();

    /**
     * @brief Save data to file.
     * @return true if file was saved successfully.
     */
    bool save();

    /**
     * @brief Check if a key exists.
     */
    bool has(const std::string& key) const;

    /**
     * @brief Remove a key.
     * @return true if key existed and was removed.
     */
    bool remove(const std::string& key);

    /**
     * @brief Clear all stored data.
     */
    void clear();

    // String values
    void setString(const std::string& key, const std::string& value);
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;

    // Integer values
    void setInt(const std::string& key, int value);
    int getInt(const std::string& key, int defaultValue = 0) const;

    // Float values
    void setFloat(const std::string& key, float value);
    float getFloat(const std::string& key, float defaultValue = 0.0f) const;

    // Double values
    void setDouble(const std::string& key, double value);
    double getDouble(const std::string& key, double defaultValue = 0.0) const;

    // Boolean values
    void setBool(const std::string& key, bool value);
    bool getBool(const std::string& key, bool defaultValue = false) const;

    /**
     * @brief Template getter with default value.
     *
     * Supports: string, int, float, double, bool
     */
    template<typename T>
    T get(const std::string& key, const T& defaultValue = T{}) const;

    /**
     * @brief Template setter.
     *
     * Supports: string, int, float, double, bool
     */
    template<typename T>
    void set(const std::string& key, const T& value);

    /**
     * @brief Get path to the storage file.
     */
    const std::string& path() const { return path_; }

    /**
     * @brief Check if storage has unsaved changes.
     */
    bool dirty() const { return dirty_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string path_;
    bool dirty_ = false;
};

// Template specializations declared in header
template<> std::string Storage::get<std::string>(const std::string& key, const std::string& defaultValue) const;
template<> int Storage::get<int>(const std::string& key, const int& defaultValue) const;
template<> float Storage::get<float>(const std::string& key, const float& defaultValue) const;
template<> double Storage::get<double>(const std::string& key, const double& defaultValue) const;
template<> bool Storage::get<bool>(const std::string& key, const bool& defaultValue) const;

template<> void Storage::set<std::string>(const std::string& key, const std::string& value);
template<> void Storage::set<int>(const std::string& key, const int& value);
template<> void Storage::set<float>(const std::string& key, const float& value);
template<> void Storage::set<double>(const std::string& key, const double& value);
template<> void Storage::set<bool>(const std::string& key, const bool& value);

} // namespace vivid::storage
