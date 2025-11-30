#include <vivid/storage/storage.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace vivid::storage {

using json = nlohmann::json;

struct Storage::Impl {
    json data;
};

Storage::Storage(const std::string& path)
    : impl_(std::make_unique<Impl>())
    , path_(path) {
    load();
}

Storage::~Storage() {
    if (dirty_) {
        save();
    }
}

Storage::Storage(Storage&& other) noexcept
    : impl_(std::move(other.impl_))
    , path_(std::move(other.path_))
    , dirty_(other.dirty_) {
    other.dirty_ = false;
}

Storage& Storage::operator=(Storage&& other) noexcept {
    if (this != &other) {
        if (dirty_) {
            save();
        }
        impl_ = std::move(other.impl_);
        path_ = std::move(other.path_);
        dirty_ = other.dirty_;
        other.dirty_ = false;
    }
    return *this;
}

bool Storage::load() {
    if (!std::filesystem::exists(path_)) {
        impl_->data = json::object();
        return true;  // New file, empty data is valid
    }

    std::ifstream file(path_);
    if (!file.is_open()) {
        std::cerr << "[vivid-storage] Failed to open: " << path_ << "\n";
        return false;
    }

    try {
        file >> impl_->data;
        dirty_ = false;
        std::cout << "[vivid-storage] Loaded: " << path_ << " ("
                  << impl_->data.size() << " keys)\n";
        return true;
    } catch (const json::parse_error& e) {
        std::cerr << "[vivid-storage] Parse error in " << path_ << ": " << e.what() << "\n";
        impl_->data = json::object();
        return false;
    }
}

bool Storage::save() {
    std::ofstream file(path_);
    if (!file.is_open()) {
        std::cerr << "[vivid-storage] Failed to write: " << path_ << "\n";
        return false;
    }

    try {
        file << std::setw(2) << impl_->data << std::endl;
        dirty_ = false;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[vivid-storage] Write error: " << e.what() << "\n";
        return false;
    }
}

bool Storage::has(const std::string& key) const {
    return impl_->data.contains(key);
}

bool Storage::remove(const std::string& key) {
    if (impl_->data.contains(key)) {
        impl_->data.erase(key);
        dirty_ = true;
        return true;
    }
    return false;
}

void Storage::clear() {
    impl_->data.clear();
    dirty_ = true;
}

// String
void Storage::setString(const std::string& key, const std::string& value) {
    impl_->data[key] = value;
    dirty_ = true;
}

std::string Storage::getString(const std::string& key, const std::string& defaultValue) const {
    if (impl_->data.contains(key) && impl_->data[key].is_string()) {
        return impl_->data[key].get<std::string>();
    }
    return defaultValue;
}

// Int
void Storage::setInt(const std::string& key, int value) {
    impl_->data[key] = value;
    dirty_ = true;
}

int Storage::getInt(const std::string& key, int defaultValue) const {
    if (impl_->data.contains(key) && impl_->data[key].is_number_integer()) {
        return impl_->data[key].get<int>();
    }
    return defaultValue;
}

// Float
void Storage::setFloat(const std::string& key, float value) {
    impl_->data[key] = value;
    dirty_ = true;
}

float Storage::getFloat(const std::string& key, float defaultValue) const {
    if (impl_->data.contains(key) && impl_->data[key].is_number()) {
        return impl_->data[key].get<float>();
    }
    return defaultValue;
}

// Double
void Storage::setDouble(const std::string& key, double value) {
    impl_->data[key] = value;
    dirty_ = true;
}

double Storage::getDouble(const std::string& key, double defaultValue) const {
    if (impl_->data.contains(key) && impl_->data[key].is_number()) {
        return impl_->data[key].get<double>();
    }
    return defaultValue;
}

// Bool
void Storage::setBool(const std::string& key, bool value) {
    impl_->data[key] = value;
    dirty_ = true;
}

bool Storage::getBool(const std::string& key, bool defaultValue) const {
    if (impl_->data.contains(key) && impl_->data[key].is_boolean()) {
        return impl_->data[key].get<bool>();
    }
    return defaultValue;
}

// Template specializations
template<>
std::string Storage::get<std::string>(const std::string& key, const std::string& defaultValue) const {
    return getString(key, defaultValue);
}

template<>
int Storage::get<int>(const std::string& key, const int& defaultValue) const {
    return getInt(key, defaultValue);
}

template<>
float Storage::get<float>(const std::string& key, const float& defaultValue) const {
    return getFloat(key, defaultValue);
}

template<>
double Storage::get<double>(const std::string& key, const double& defaultValue) const {
    return getDouble(key, defaultValue);
}

template<>
bool Storage::get<bool>(const std::string& key, const bool& defaultValue) const {
    return getBool(key, defaultValue);
}

template<>
void Storage::set<std::string>(const std::string& key, const std::string& value) {
    setString(key, value);
}

template<>
void Storage::set<int>(const std::string& key, const int& value) {
    setInt(key, value);
}

template<>
void Storage::set<float>(const std::string& key, const float& value) {
    setFloat(key, value);
}

template<>
void Storage::set<double>(const std::string& key, const double& value) {
    setDouble(key, value);
}

template<>
void Storage::set<bool>(const std::string& key, const bool& value) {
    setBool(key, value);
}

} // namespace vivid::storage
