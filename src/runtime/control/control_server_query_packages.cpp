#include "runtime/control/control_server_internal.h"

namespace vivid {

std::string handle_get_discovery_report(PackageManager* package_manager) {
    if (!package_manager)
        return json_err("package manager not available");

    const auto& report = package_manager->last_discovery_report();
    nlohmann::json result = nlohmann::json::object();
    result["workspace_detected"] = report.workspace_detected;

    nlohmann::json scopes = nlohmann::json::array();
    for (const auto& s : report.scopes_searched) {
        scopes.push_back({{"scope", s.scope}, {"root", s.root}, {"exists", s.exists}});
    }
    result["scopes"] = std::move(scopes);

    nlohmann::json loaded = nlohmann::json::array();
    for (const auto& p : report.loaded_packages) {
        loaded.push_back({
            {"name", p.name},
            {"version", p.version},
            {"scope", p.source_scope},
            {"path", p.path},
            {"operators", p.operators.size() + p.gpu_operators.size()}
        });
    }
    result["loaded"] = std::move(loaded);

    nlohmann::json skipped = nlohmann::json::array();
    for (const auto& s : report.skipped_packages) {
        skipped.push_back({
            {"name", s.name},
            {"path", s.path},
            {"scope", s.source_scope},
            {"reason", s.reason},
            {"detail", s.detail}
        });
    }
    result["skipped"] = std::move(skipped);
    return json_ok(std::move(result));
}

std::string handle_list_packages(PackageManager* package_manager) {
    if (!package_manager)
        return json_err("package manager not available");

    auto packages = package_manager->list();
    nlohmann::json res = nlohmann::json::object();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& pkg : packages) {
        nlohmann::json p = nlohmann::json::object();
        p["name"] = pkg.name;
        p["version"] = pkg.version;
        if (!pkg.vivid_core.empty()) p["vivid_core"] = pkg.vivid_core;
        if (!pkg.source_scope.empty()) p["source_scope"] = pkg.source_scope;
        if (!pkg.path.empty()) p["path"] = pkg.path;
        if (!pkg.build_type.empty()) p["build_type"] = pkg.build_type;
        p["description"] = pkg.description;
        p["author"] = pkg.author;
        nlohmann::json ops = nlohmann::json::array();
        for (const auto& op : pkg.operators) ops.push_back(op);
        for (const auto& op : pkg.gpu_operators) ops.push_back(op);
        p["operators"] = std::move(ops);
        p["linked"] = pkg.linked;
        arr.push_back(std::move(p));
    }
    res["packages"] = std::move(arr);
    return json_ok(std::move(res));
}

std::string handle_read_package_docs(PackageManager* package_manager, const nlohmann::json& root) {
    if (!package_manager)
        return json_err("package manager not available");
    if (!root.contains("name") || !root["name"].is_string())
        return json_err("missing 'name'");

    std::string name = root["name"].get<std::string>();
    if (!is_safe_package_name(name))
        return json_err("invalid package name");
    if (!package_manager->is_installed(name))
        return json_err("package not installed: " + name);

    auto readme_path = std::filesystem::path(PackageManager::packages_dir()) / name / "README.md";
    std::ifstream f(readme_path);
    if (!f.is_open())
        return json_ok_msg("No README.md found for package '" + name + "'");

    std::ostringstream ss;
    ss << f.rdbuf();
    return json_ok(nlohmann::json{{"name", name}, {"content", ss.str()}});
}

std::string handle_list_package_examples(PackageManager* package_manager, const nlohmann::json& root) {
    if (!package_manager)
        return json_err("package manager not available");
    if (!root.contains("name") || !root["name"].is_string())
        return json_err("missing 'name'");

    std::string name = root["name"].get<std::string>();
    if (!is_safe_package_name(name))
        return json_err("invalid package name");
    if (!package_manager->is_installed(name))
        return json_err("package not installed: " + name);

    auto graphs_dir = std::filesystem::path(PackageManager::packages_dir()) / name / "graphs";
    nlohmann::json res = nlohmann::json::object();
    res["name"] = name;
    nlohmann::json arr = nlohmann::json::array();
    std::error_code ec;
    if (std::filesystem::is_directory(graphs_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(graphs_dir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;
            nlohmann::json ex = nlohmann::json::object();
            ex["filename"] = entry.path().filename().string();
            vivid::ExampleEntry edata;
            if (load_example_entry_from_graph(entry.path(), graphs_dir, edata)) {
                ex["description"] = edata.summary;
                if (!edata.content_kind.empty()) ex["content_kind"] = edata.content_kind;
                if (!edata.category.empty()) ex["category"] = edata.category;
                if (!edata.family.empty()) ex["family"] = edata.family;
                if (!edata.role.empty()) ex["role"] = edata.role;
                if (!edata.playability.empty()) ex["playability"] = edata.playability;
                if (!edata.domains.empty()) {
                    nlohmann::json darr = nlohmann::json::array();
                    for (const auto& d : edata.domains) darr.push_back(d);
                    ex["domains"] = std::move(darr);
                }
            } else {
                ex["description"] = "";
            }
            arr.push_back(std::move(ex));
        }
    }
    res["examples"] = std::move(arr);
    return json_ok(std::move(res));
}

std::string handle_read_package_example(PackageManager* package_manager, const nlohmann::json& root) {
    if (!package_manager)
        return json_err("package manager not available");
    if (!root.contains("name") || !root["name"].is_string() ||
        !root.contains("filename") || !root["filename"].is_string())
        return json_err("missing 'name' or 'filename'");

    std::string name = root["name"].get<std::string>();
    std::string filename = root["filename"].get<std::string>();
    if (!is_safe_package_name(name))
        return json_err("invalid package name");
    if (filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos ||
        filename.find("..") != std::string::npos) {
        return json_err("invalid filename");
    }
    if (!package_manager->is_installed(name))
        return json_err("package not installed: " + name);

    auto file_path = std::filesystem::path(PackageManager::packages_dir()) / name / "graphs" / filename;
    std::ifstream f(file_path);
    if (!f.is_open())
        return json_err("example not found: " + filename);

    std::ostringstream ss;
    ss << f.rdbuf();
    return json_ok(nlohmann::json{{"name", name}, {"filename", filename}, {"content", ss.str()}});
}

std::string handle_package_catalog(PackageCatalog* package_catalog) {
    if (!package_catalog)
        return json_err("package catalog not available");

    auto entries = package_catalog->entries();
    nlohmann::json resp = nlohmann::json::object();
    resp["ok"] = true;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries) {
        nlohmann::json obj = nlohmann::json::object();
        obj["name"] = e.name;
        obj["description"] = e.description;
        obj["version"] = e.version;
        if (!e.vivid_core.empty()) obj["vivid_core"] = e.vivid_core;
        obj["author"] = e.author;
        obj["url"] = e.url;
        if (!e.category.empty()) obj["category"] = e.category;
        if (!e.description_short.empty()) obj["description_short"] = e.description_short;
        if (!e.status.empty()) obj["status"] = e.status;
        if (!e.status_note.empty()) obj["status_note"] = e.status_note;
        if (!e.preview_image_url.empty()) obj["preview_image_url"] = e.preview_image_url;
        if (!e.repo_url.empty()) obj["repo_url"] = e.repo_url;
        if (!e.homepage_url.empty()) obj["homepage_url"] = e.homepage_url;
        if (!e.install_url.empty()) obj["install_url"] = e.install_url;
        obj["installed"] = e.installed;
        if (e.installed) obj["installed_version"] = e.installed_version;
        arr.push_back(std::move(obj));
    }
    resp["packages"] = std::move(arr);
    return resp.dump();
}

std::string handle_check_package_updates(PackageCatalog* package_catalog,
                                         PackageManager* package_manager,
                                         const nlohmann::json& root) {
    if (!package_catalog || !package_manager)
        return json_err("package catalog/manager not available");

    std::string core_version = root.contains("core_version") && root["core_version"].is_string()
        ? root["core_version"].get<std::string>()
        : "0.1.0";
    const bool include_all_installed = root.contains("include_all_installed") &&
        root["include_all_installed"].is_boolean() &&
        root["include_all_installed"].get<bool>();

    auto entries = package_catalog->entries();
    nlohmann::json resp = nlohmann::json::object();
    resp["ok"] = true;
    resp["core_version"] = core_version;

    nlohmann::json updates = nlohmann::json::array();
    int64_t update_count = 0;
    int64_t incompatible_count = 0;
    for (const auto& e : entries) {
        if (!e.installed) continue;

        PackageInfo installed;
        installed.name = e.name;
        installed.version = e.installed_version;
        auto assessment = PackageManager::assess_update(
            installed, e.version, e.vivid_core, core_version);

        if (!include_all_installed && !assessment.update_available) continue;

        nlohmann::json obj = nlohmann::json::object();
        obj["name"] = assessment.package_name;
        obj["installed_version"] = assessment.installed_version;
        obj["remote_version"] = assessment.remote_version;
        if (!assessment.remote_vivid_core.empty())
            obj["vivid_core"] = assessment.remote_vivid_core;
        obj["update_available"] = assessment.update_available;
        obj["compatible"] = assessment.compatible;
        obj["constraint_valid"] = assessment.constraint_valid;
        obj["classification"] = update_class_str(assessment.classification);
        obj["message"] = assessment.message;

        if (assessment.update_available) update_count++;
        if (assessment.classification == PackageUpdateClass::IncompatibleUpdate)
            incompatible_count++;

        updates.push_back(std::move(obj));
    }
    resp["updates_available"] = update_count;
    resp["incompatible_updates"] = incompatible_count;
    resp["packages"] = std::move(updates);
    return resp.dump();
}

std::string handle_check_core_updates(AppUpdateManager* app_update_manager, const nlohmann::json& root) {
    if (!app_update_manager)
        return json_err("core update manager unavailable");

    const bool force_refresh = root.contains("force_refresh") &&
        root["force_refresh"].is_boolean() &&
        root["force_refresh"].get<bool>();
    if (force_refresh) app_update_manager->refresh();
    if (app_update_manager->fetch_state() == AppUpdateFetchState::Idle)
        app_update_manager->refresh();
    for (int i = 0; i < 200; ++i) {
        auto st = app_update_manager->fetch_state();
        if (st != AppUpdateFetchState::Fetching) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    nlohmann::json resp = nlohmann::json::object();
    resp["ok"] = true;

    const auto st = app_update_manager->fetch_state();
    switch (st) {
        case AppUpdateFetchState::Idle:     resp["state"] = "idle"; break;
        case AppUpdateFetchState::Fetching: resp["state"] = "fetching"; break;
        case AppUpdateFetchState::Ready:    resp["state"] = "ready"; break;
        case AppUpdateFetchState::Error:    resp["state"] = "error"; break;
    }

    auto info = app_update_manager->latest();
    resp["update_available"] = info.update_available;
    resp["current_version"] = info.current_version;
    resp["latest_version"] = info.latest_version;
    resp["download_url"] = info.download_url;
    resp["release_notes_url"] = info.release_notes_url;
    resp["title"] = info.title;
    resp["publication_date"] = info.publication_date;
    resp["minimum_system_version"] = info.minimum_system_version;
    resp["appcast_url"] = AppUpdateManager::appcast_url();
    if (st == AppUpdateFetchState::Error)
        resp["error"] = app_update_manager->fetch_error();
    return resp.dump();
}

} // namespace vivid
