#include "test_helpers.h"
#include "app/project_state.h"
#include <string>

int main() {
    vivid::ProjectState project;

    project.remember_project_path("/tmp/a.vivid.json");
    project.remember_project_path("/tmp/b.vivid.json");
    project.remember_project_path("/tmp/a.vivid.json");
    CHECK(project.current_project_path == "/tmp/a.vivid.json");
    CHECK(project.recent_project_paths.size() == 2);
    CHECK(project.recent_project_paths[0] == "/tmp/a.vivid.json");
    CHECK(project.recent_project_paths[1] == "/tmp/b.vivid.json");

    for (int i = 0; i < 10; ++i)
        project.remember_project_path("/tmp/project-" + std::to_string(i) + ".json");
    CHECK(project.recent_project_paths.size() == 8);
    CHECK(project.recent_project_paths[0] == "/tmp/project-9.json");

    return vivid::test::summary("test_project_workflow");
}
