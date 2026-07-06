// Headless tests for the pure project-path convention (app/src/app/project_paths.h):
// a path ending in ".json" is a legacy single-file project; any other path is a
// project FOLDER whose session JSON is "<dir>/project.json". This convention decides
// where save_project writes and where load_project looks for a co-located operator
// package, so a wrong classification silently loads/saves the wrong file.
#include "app/project_paths.h"
#include "test_helpers.h"

#include <filesystem>

using namespace vivid::project_paths;

static void test_is_folder_project() {
    // .json paths are single-file projects.
    CHECK(!is_folder_project("session.json"));
    CHECK(!is_folder_project("/a/b/project.json"));
    CHECK(!is_folder_project("/a/b/My Song.json"));
    // Everything else is a folder project.
    CHECK(is_folder_project("/a/b/mysong"));
    CHECK(is_folder_project("/a/b/mysong.vivid"));   // non-.json extension is still a folder
    CHECK(is_folder_project("mysong"));
    CHECK(is_folder_project("/a/b/"));
}

static void test_session_json_path() {
    namespace fs = std::filesystem;
    // A .json path resolves to itself.
    CHECK(session_json_path("/a/b/song.json") == "/a/b/song.json");
    // A folder path resolves to <dir>/project.json.
    CHECK(session_json_path("/a/b/song") == (fs::path("/a/b/song") / "project.json").string());
    CHECK(session_json_path("mysong") == (fs::path("mysong") / "project.json").string());
}

int main() {
    test_is_folder_project();
    test_session_json_path();
    return vivid::test::summary("test_project_paths");
}
