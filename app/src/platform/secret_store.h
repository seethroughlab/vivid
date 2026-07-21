#pragma once

#include <string>

namespace vivid {

/// A small secure key/value store for app secrets (the Gemini API key — ADR-0026). macOS backs it
/// with the login Keychain (Security.framework), so the secret never sits in a plaintext config
/// file. Other platforms are a noop returning false (the caller then reports "unconfigured").
/// `service` groups keys (use "com.vivid.app"); `account` names the secret (e.g. "gemini_api_key").
bool secret_set(const std::string& service, const std::string& account, const std::string& value);
bool secret_get(const std::string& service, const std::string& account, std::string& out);
bool secret_delete(const std::string& service, const std::string& account);

}  // namespace vivid
