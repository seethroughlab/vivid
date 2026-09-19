#include "cli/control_handlers_internal.h"

#include "app/app.h"
#include "app/edit_gateway.h"
#include "app/project_paths.h"
#include "audio/vst3_host.h"
#include "work/work_records.h"

#include <filesystem>
#include <string>

namespace vivid {

namespace fs = std::filesystem;

// ADR-0061/0062 — the WORK RECORDS surface, gate-1 slice: enough for an agent to publish honest
// candidates for the Review workspace without reimplementing the records layer. The records
// themselves are plain JSON under <project>/work/ that an agent reads/writes directly (that is the
// point of the format); these handlers cover the two things only the app can do well:
//   work_status            — where the records are, what the document derives from, what is open
//   work_snapshot_version  — an immutable version of the LOADED project folder (complete copy +
//                            SHA-256 per dependency + plugin identities), optionally INTO another
//                            project's records (the candidate-copy flow: edit a copy, snapshot it into
//                            the original's work/, never touching the foreground document)
// The full work/version/review/execution tool families are gate 2 (ADR-0063).
void register_work_handlers(Handlers& handlers_) {
    handlers_["work_status"] = [](const ControlCtx& c, const json&) {
        if (!c.app) return err(code::kInternal, "no app");
        json r = ok();
        const std::string& p = c.app->project.current_project_path;
        const bool folder = !p.empty() && project_paths::is_folder_project(p);
        r["project_dir"] = folder ? p : "";
        r["is_folder_project"] = folder;
        r["preferred_version"] = c.session ? std::string(session::session_preferred_version(c.session)) : std::string();
        r["dirty"] = c.app->edit_gateway ? c.app->edit_gateway->dirty() : false;
        if (!folder) { r["has_records"] = false; r["summary"] = "no folder project open — work records need a project folder"; return r; }
        std::error_code ec;
        const bool has = fs::is_directory(work::work_dir(p), ec);
        r["has_records"] = has;
        if (has) {
            work::Brief b; r["brief_rev"] = work::load_brief(p, b) ? b.rev : 0;
            int open = 0; json reviews = json::array();
            for (const auto& rv : work::list_reviews(p)) {
                if (rv.status == "open") ++open;
                reviews.push_back({ {"id", rv.id}, {"status", rv.status}, {"question", rv.question}, {"candidates", rv.candidates} });
            }
            r["open_reviews"] = open; r["reviews"] = reviews;
            r["versions"] = work::list_versions(p).size();
            r["candidates"] = work::list_candidates(p).size();
        }
        return r;
    };

    // {label, source?="manual", parent?="", brief_rev?=0, into?=<records project dir>} -> {id, dependencies, plugins}.
    // Reads project.json from DISK, so the loaded document must be saved first: an unsaved document
    // is refused rather than snapshotting something the creator has not seen written.
    handlers_["work_snapshot_version"] = [](const ControlCtx& c, const json& b) {
        if (!c.app) return err(code::kInternal, "no app");
        const std::string& p = c.app->project.current_project_path;
        if (p.empty() || !project_paths::is_folder_project(p)) return err(code::kBadArg, "the loaded project is not a folder project");
        if (c.app->edit_gateway && c.app->edit_gateway->dirty())
            return err(code::kConflict, "the document has unsaved changes — save_project first (a snapshot reads project.json on disk)");
        work::SnapshotOptions opt;
        opt.label = b.value("label", std::string());
        opt.source = b.value("source", std::string("manual"));
        opt.parent = b.value("parent", std::string());
        opt.brief_rev = b.value("brief_rev", 0);
        const std::string into = b.value("into", p);
        if (!into.empty() && into != p) {
            std::error_code ec;
            if (!fs::is_regular_file(fs::path(into) / "project.json", ec)) return err(code::kBadArg, "into: not a project folder: " + into);
        }
        std::string e;
        const std::string id = work::snapshot_version_into(p, into, opt, &e);
        if (id.empty()) return err(code::kIoError, e);
        work::Version v;
        json r = ok(); r["id"] = id; r["records_dir"] = into;
        if (work::load_version(into, id, v)) { r["dependencies"] = v.dependencies.size(); r["plugins"] = v.plugins.size(); r["document_sha256"] = v.document_sha256; }
        return r;
    };
}

}  // namespace vivid
