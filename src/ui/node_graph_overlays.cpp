#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
#include <algorithm>
#include <cctype>

namespace vivid::ui {

namespace {
bool same_package_browser_entries(const std::vector<PackageBrowserEntry>& a,
                                  const std::vector<PackageBrowserEntry>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& lhs = a[i];
        const auto& rhs = b[i];
        if (lhs.name != rhs.name ||
            lhs.description != rhs.description ||
            lhs.version != rhs.version ||
            lhs.author != rhs.author ||
            lhs.category != rhs.category ||
            lhs.tags != rhs.tags ||
            lhs.installed != rhs.installed ||
            lhs.linked != rhs.linked) {
            return false;
        }
    }
    return true;
}
} // namespace

void NodeGraphUI::refresh_package_browser_snapshot_if_ready() {
    if (!pkg_browser_open_) return;
    if (!pkg_browser_callbacks_.list_entries || !pkg_browser_callbacks_.fetch_state) return;

    auto state = pkg_browser_callbacks_.fetch_state();
    if (state != PackageBrowserFetchState::Ready &&
        state != PackageBrowserFetchState::Error) {
        return;
    }

    auto fresh = pkg_browser_callbacks_.list_entries();
    if (!same_package_browser_entries(fresh, pkg_browser_all_)) {
        pkg_browser_all_ = std::move(fresh);
        rebuild_pkg_browser_items();
    }
}

void NodeGraphUI::toggle_package_browser() {
    pkg_browser_open_ = !pkg_browser_open_;
    if (pkg_browser_open_) example_browser_open_ = false;
    if (pkg_browser_open_ && pkg_browser_callbacks_.list_entries) {
        // Refresh catalog and load entries
        if (pkg_browser_callbacks_.fetch_state && pkg_browser_callbacks_.refresh) {
            auto state = pkg_browser_callbacks_.fetch_state();
            if (state == PackageBrowserFetchState::Idle ||
                state == PackageBrowserFetchState::Error) {
                pkg_browser_callbacks_.refresh();
            }
        }
        pkg_browser_all_ = pkg_browser_callbacks_.list_entries();
        rebuild_pkg_browser_items();
        pkg_browser_sel_ = 0;
        pkg_browser_scroll_ = 0;
        pkg_action_error_.clear();
    }
}

void NodeGraphUI::notify_pkg_action_complete(bool success, const std::string& error) {
    pkg_action_pending_ = false;
    pkg_action_name_.clear();
    if (!success && !error.empty())
        pkg_action_error_ = error;
    if (pkg_browser_callbacks_.list_entries)
        pkg_browser_all_ = pkg_browser_callbacks_.list_entries();
    rebuild_pkg_browser_items();
}

void NodeGraphUI::toggle_example_browser() {
    example_browser_open_ = !example_browser_open_;
    if (example_browser_open_) {
        pkg_browser_open_ = false;
        example_browser_filter_.clear();
        example_browser_sel_ = 0;
        example_browser_scroll_ = 0;
        example_browser_env_ = 0;
        example_browser_difficulty_ = 0;
        example_browser_sort_ = 0;
        example_browser_core_only_ = true;
        example_browser_package_only_ = false;
        example_action_error_.clear();
        example_warn_id_.clear();
        rebuild_example_items();
    }
}

void NodeGraphUI::set_examples(std::vector<ExampleEntry> examples) {
    example_entries_all_ = std::move(examples);
    rebuild_example_items();
}

void NodeGraphUI::set_example_open_callback(std::function<void(const std::string&)> cb) {
    example_open_callback_ = std::move(cb);
}

void NodeGraphUI::set_example_package_checker(
    std::function<bool(const std::vector<std::string>&, std::string&)> cb) {
    example_package_checker_ = std::move(cb);
}

void NodeGraphUI::open_graph_meta_editor(const GraphMetaEditData& data) {
    graph_meta_data_ = data;
    graph_meta_editor_open_ = true;
    graph_meta_active_field_ = 0;
    graph_meta_error_.clear();
    graph_meta_fields_ = {
        &graph_meta_data_.id,
        &graph_meta_data_.title,
        &graph_meta_data_.description,
        &graph_meta_data_.tags_csv,
        &graph_meta_data_.difficulty,
        &graph_meta_data_.domains_csv,
        &graph_meta_data_.requires_packages_csv,
        &graph_meta_data_.featured_rank
    };
    text_edit_.reset(static_cast<int>(graph_meta_fields_[graph_meta_active_field_]->size()));
}

void NodeGraphUI::set_graph_meta_save_callback(
    std::function<bool(const GraphMetaEditData&, std::string&)> cb) {
    graph_meta_save_callback_ = std::move(cb);
}

void NodeGraphUI::set_package_browser_callbacks(PackageBrowserCallbacks callbacks) {
    pkg_browser_callbacks_ = std::move(callbacks);
}

void NodeGraphUI::rebuild_pkg_browser_items() {
    pkg_browser_entries_.clear();

    // Category names for filtering
    static const char* cat_names[] = { "", "audio", "gpu", "control", "utility" };
    bool filter_installed = (pkg_browser_category_ == 5);
    const char* cat_filter = (pkg_browser_category_ >= 1 && pkg_browser_category_ <= 4)
                             ? cat_names[pkg_browser_category_] : nullptr;

    for (const auto& e : pkg_browser_all_) {
        // Category filter
        if (filter_installed && !e.installed) continue;
        if (cat_filter && e.category != cat_filter) continue;

        // Text search filter
        if (!pkg_browser_filter_.empty()) {
            // Case-insensitive search in name, description, tags
            auto contains = [](const std::string& haystack, const std::string& needle) {
                if (needle.empty()) return true;
                auto it = std::search(haystack.begin(), haystack.end(),
                                      needle.begin(), needle.end(),
                                      [](char a, char b) {
                                          return std::tolower(static_cast<unsigned char>(a)) ==
                                                 std::tolower(static_cast<unsigned char>(b));
                                      });
                return it != haystack.end();
            };

            bool match = contains(e.name, pkg_browser_filter_) ||
                         contains(e.description, pkg_browser_filter_);
            if (!match) {
                for (const auto& tag : e.tags) {
                    if (contains(tag, pkg_browser_filter_)) { match = true; break; }
                }
            }
            if (!match) continue;
        }

        pkg_browser_entries_.push_back(e);
    }

    // Clamp selection
    if (pkg_browser_sel_ >= static_cast<int>(pkg_browser_entries_.size()))
        pkg_browser_sel_ = std::max(0, static_cast<int>(pkg_browser_entries_.size()) - 1);
    if (pkg_browser_scroll_ > static_cast<int>(pkg_browser_entries_.size()) - kPkgBrowserMaxVisible)
        pkg_browser_scroll_ = std::max(0, static_cast<int>(pkg_browser_entries_.size()) - kPkgBrowserMaxVisible);
}

void NodeGraphUI::rebuild_example_items() {
    example_entries_.clear();

    auto icontains = [](const std::string& haystack, const std::string& needle) {
        if (needle.empty()) return true;
        auto it = std::search(haystack.begin(), haystack.end(),
                              needle.begin(), needle.end(),
                              [](char a, char b) {
                                  return std::tolower(static_cast<unsigned char>(a)) ==
                                         std::tolower(static_cast<unsigned char>(b));
                              });
        return it != haystack.end();
    };

    for (const auto& e : example_entries_all_) {
        if (example_browser_core_only_ && !e.requires_packages.empty()) continue;
        if (example_browser_package_only_ && e.requires_packages.empty()) continue;

        if (example_browser_env_ != 0) {
            static const char* kEnvNames[] = {"", "gpu", "audio", "control", "io"};
            const std::string target = kEnvNames[example_browser_env_];
            bool found = false;
            for (const auto& d : e.domains) {
                if (icontains(d, target)) { found = true; break; }
            }
            if (!found) continue;
        }

        if (example_browser_difficulty_ != 0) {
            static const char* kDiffNames[] = {"", "beginner", "intermediate", "advanced"};
            if (!icontains(e.difficulty, kDiffNames[example_browser_difficulty_])) continue;
        }

        if (!example_browser_filter_.empty()) {
            bool match = icontains(e.title, example_browser_filter_) ||
                         icontains(e.summary, example_browser_filter_) ||
                         icontains(e.id, example_browser_filter_) ||
                         icontains(e.path, example_browser_filter_);
            if (!match) {
                for (const auto& t : e.tags) {
                    if (icontains(t, example_browser_filter_)) {
                        match = true;
                        break;
                    }
                }
            }
            if (!match) continue;
        }

        example_entries_.push_back(e);
    }

    if (example_browser_sort_ == 0) {
        std::sort(example_entries_.begin(), example_entries_.end(),
                  [](const ExampleEntry& a, const ExampleEntry& b) {
                      if (a.featured_rank != b.featured_rank)
                          return a.featured_rank < b.featured_rank;
                      return a.title < b.title;
                  });
    } else {
        std::sort(example_entries_.begin(), example_entries_.end(),
                  [](const ExampleEntry& a, const ExampleEntry& b) {
                      return a.title < b.title;
                  });
    }

    if (example_browser_sel_ >= static_cast<int>(example_entries_.size()))
        example_browser_sel_ = std::max(0, static_cast<int>(example_entries_.size()) - 1);
    int max_scroll = std::max(0, static_cast<int>(example_entries_.size()) - kPkgBrowserMaxVisible);
    if (example_browser_scroll_ > max_scroll) example_browser_scroll_ = max_scroll;
}

} // namespace vivid::ui
