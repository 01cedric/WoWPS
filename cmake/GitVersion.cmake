# Writes core/version.hpp from `git describe`, so the version shown in the client
# is always the last tag reachable from HEAD.
#
# Run as a script (cmake -P) from a build-time custom target, not just at configure
# time - otherwise tagging a release would not change the binary until someone
# happened to re-run cmake.
#
# Expects: SRC_DIR, IN_FILE, OUT_FILE

find_package(Git QUIET)

set(WOWEE_GIT_VERSION "unknown")
if(GIT_FOUND)
    # The last tag reachable from HEAD - the released version this build descends from.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${SRC_DIR}
        OUTPUT_VARIABLE _git_tag
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_result
    )
    if(_git_result EQUAL 0 AND _git_tag)
        set(WOWEE_GIT_VERSION "${_git_tag}")
    else()
        # No tags (shallow clone, fresh fork): fall back to the short commit.
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${SRC_DIR}
            OUTPUT_VARIABLE _git_sha
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _sha_result
        )
        if(_sha_result EQUAL 0 AND _git_sha)
            set(WOWEE_GIT_VERSION "${_git_sha}")
        endif()
    endif()
endif()

# Source archives have no .git history. A delivery's explicit build identifier
# takes precedence so rebuilding the same checkpoint preserves its identity.
if(EXISTS "${SRC_DIR}/BUILD_VERSION")
    file(STRINGS "${SRC_DIR}/BUILD_VERSION" _delivery_version LIMIT_COUNT 1)
    if(_delivery_version MATCHES "^([0-9][0-9]\\.[0-9][0-9]( HF[1-9][0-9]*)?|B[0-9]+ / [0-9][0-9]\\.[0-9][0-9])$")
        set(WOWEE_GIT_VERSION "${_delivery_version}")
    endif()
endif()

# Date only, no clock time: a timestamp would differ on every build and force a
# recompile of everything including this header.
string(TIMESTAMP WOWEE_BUILD_DATE "%Y-%m-%d" UTC)

configure_file(${IN_FILE} ${OUT_FILE}.tmp @ONLY)

# Only touch the real header when the version actually changed; rewriting it every
# build would rebuild every translation unit that includes it.
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT_FILE}.tmp ${OUT_FILE}
)
file(REMOVE ${OUT_FILE}.tmp)
