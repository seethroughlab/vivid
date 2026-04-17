# Capture the Vivid core source commit at configure time.
#
# Defines:
#   VIVID_CORE_COMMIT    — full 40-char sha, or empty when git is absent /
#                          not in a worktree / rev-parse fails.
#   VIVID_CORE_REPO_URL  — remote.origin.url, or empty.
#
# Consumers wire these into compile definitions on `vivid` and
# `vivid_runtime_testlib` via target_compile_definitions, then read them as
# string macros (with "" fallback) in C++ code. Used by the project-lockfile
# feature to populate lockfile.vivid_core.commit.
#
# The sha is baked in at configure time, not build time — rerun cmake to
# refresh.

find_package(Git QUIET)

set(VIVID_CORE_COMMIT   "")
set(VIVID_CORE_REPO_URL "")

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} rev-parse HEAD
        OUTPUT_VARIABLE _vivid_git_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _vivid_git_commit_rc)
    if(_vivid_git_commit_rc EQUAL 0)
        set(VIVID_CORE_COMMIT "${_vivid_git_commit}")
    endif()

    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} config --get remote.origin.url
        OUTPUT_VARIABLE _vivid_git_url
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _vivid_git_url_rc)
    if(_vivid_git_url_rc EQUAL 0)
        set(VIVID_CORE_REPO_URL "${_vivid_git_url}")
    endif()
endif()

if(VIVID_CORE_COMMIT)
    message(STATUS "Vivid core commit: ${VIVID_CORE_COMMIT}")
else()
    message(STATUS "Vivid core commit: (none — non-git build)")
endif()
