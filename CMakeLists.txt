cmake_minimum_required(VERSION 3.20)
project(zwaved VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)

# Capture a build-tree signal alongside PROJECT_VERSION:
# `git describe --tags --dirty --always` gives "v0.1.0",
# "v0.1.0-12-gabcdef0", or "v0.1.0-12-gabcdef0-dirty" depending on
# how far the working tree is from the most recent tag. Re-evaluated
# on reconfigure — for live dirty-state tracking, wire a custom
# target with BYPRODUCTS instead.
find_package(Git QUIET)
set(ZWAVED_GIT_DESCRIBE "unknown")
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty --always
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _zwaved_git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _zwaved_git_rc
    )
    if(_zwaved_git_rc EQUAL 0 AND NOT "${_zwaved_git_describe}" STREQUAL "")
        set(ZWAVED_GIT_DESCRIBE "${_zwaved_git_describe}")
    endif()
endif()

# Generated header, consumed via #include "Version.hpp" — the path is
# wired into the daemon's include directories in src/CMakeLists.txt.
configure_file(
    "${CMAKE_SOURCE_DIR}/src/Version.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/Version.hpp"
    @ONLY
)

# ----------------------------------------------------------------------
# Codegen: scripts/codegen/ → ${CMAKE_BINARY_DIR}/generated/
#
# InterfaceManifest.yml is the single source of truth for the
# daemon's bus + D-Bus + CC surface; the Python generator under
# scripts/codegen/ turns it into C++ headers/sources. Phase 1 emits
# only Manifest.gen.hpp as a sanity-check artefact — nothing in the
# daemon consumes it yet, but adding the custom command here means a
# manifest edit retriggers the generator on the next build, and the
# CMake build graph already includes the codegen by the time later
# phases plug in real outputs.
# ----------------------------------------------------------------------
find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import yaml, jinja2"
    RESULT_VARIABLE _zwaved_codegen_deps_rc
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT _zwaved_codegen_deps_rc EQUAL 0)
    message(FATAL_ERROR
        "scripts/codegen requires PyYAML and Jinja2.\n"
        "On Debian/Ubuntu: sudo apt install python3-yaml python3-jinja2"
    )
endif()

set(ZWAVED_MANIFEST   "${CMAKE_SOURCE_DIR}/InterfaceManifest.yml")
set(ZWAVED_CODEGEN    "${CMAKE_SOURCE_DIR}/scripts/codegen/generate.py")
set(ZWAVED_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

set(ZWAVED_GENERATED_HEADERS
    "${ZWAVED_GENERATED_DIR}/MessageBus.gen.hpp"
    "${ZWAVED_GENERATED_DIR}/DBusMethods.gen.hpp"
    "${ZWAVED_GENERATED_DIR}/DBusSignals.gen.hpp"
    "${ZWAVED_GENERATED_DIR}/application/BinarySwitch.gen.hpp"
    "${ZWAVED_GENERATED_DIR}/application/Basic.gen.hpp"
    "${ZWAVED_GENERATED_DIR}/application/Association.gen.hpp"
    "${ZWAVED_GENERATED_DIR}/application/MultichannelAssociation.gen.hpp"
)
set(ZWAVED_GENERATED_SOURCES
    "${ZWAVED_GENERATED_DIR}/MessageBus.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/DBusMethods.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/DBusSignals.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/CcTranslator.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/BinarySwitch.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/Basic.gen.cpp"
)

# Reconfigure when any generator source changes so the OUTPUT list
# stays accurate as new targets land.
file(GLOB_RECURSE ZWAVED_CODEGEN_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/scripts/codegen/*.py"
    "${CMAKE_SOURCE_DIR}/scripts/codegen/templates/*"
)

add_custom_command(
    OUTPUT ${ZWAVED_GENERATED_HEADERS} ${ZWAVED_GENERATED_SOURCES}
    COMMAND "${Python3_EXECUTABLE}" "${ZWAVED_CODEGEN}"
        --manifest "${ZWAVED_MANIFEST}"
        --out      "${ZWAVED_GENERATED_DIR}"
    DEPENDS "${ZWAVED_MANIFEST}" ${ZWAVED_CODEGEN_SOURCES}
    COMMENT "Codegen: InterfaceManifest.yml → ${ZWAVED_GENERATED_DIR}/"
    VERBATIM
)

add_custom_target(zwaved_codegen DEPENDS ${ZWAVED_GENERATED_HEADERS} ${ZWAVED_GENERATED_SOURCES})

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

# Header-only event dispatcher / queue. Expected to be provided by the
# system / cross-sysroot (e.g. Yocto meta-oe recipe, distro package);
# not vendored in-tree.
find_package(eventpp REQUIRED)

# External API backend selector. Accepted values: dbus, ubus, both.
# D-Bus is the only backend implemented today; ubus is reserved for future support.
set(ZWAVED_EXTERNAL_API "dbus" CACHE STRING "External API backends to build (dbus, ubus, both)")
set_property(CACHE ZWAVED_EXTERNAL_API PROPERTY STRINGS "dbus" "ubus" "both")

set(ZWAVED_BUILD_DBUS OFF)
set(ZWAVED_BUILD_UBUS OFF)
if(ZWAVED_EXTERNAL_API STREQUAL "dbus" OR ZWAVED_EXTERNAL_API STREQUAL "both")
    set(ZWAVED_BUILD_DBUS ON)
endif()
if(ZWAVED_EXTERNAL_API STREQUAL "ubus" OR ZWAVED_EXTERNAL_API STREQUAL "both")
    set(ZWAVED_BUILD_UBUS ON)
endif()

if(ZWAVED_BUILD_DBUS)
    pkg_check_modules(SDBUS_CPP REQUIRED IMPORTED_TARGET sdbus-c++)
endif()

# Logger sink selector. `stdout` (default) writes to stdout — captured
# by journald when zwaved runs under systemd. `syslog` calls syslog(3),
# which is the right fit for OpenWRT / non-systemd hosts where logd or
# rsyslog routes to /var/log/messages.
set(ZWAVED_LOGGER_SINK "stdout" CACHE STRING "Logger sink (stdout, syslog)")
set_property(CACHE ZWAVED_LOGGER_SINK PROPERTY STRINGS "stdout" "syslog")

# Process source subdirectories
add_subdirectory(src)

# zwaved doesn't consume any generated header today, so the
# dependency edge is added explicitly so the codegen still runs as
# part of a normal `cmake --build`.
add_dependencies(zwaved zwaved_codegen)

if(ZWAVED_LOGGER_SINK STREQUAL "syslog")
    target_compile_definitions(zwaved PRIVATE ZWAVED_LOGGER_SYSLOG=1)
endif()

option(ZWAVED_BUILD_UTILS "Build companion utility apps under utils/" ON)
if(ZWAVED_BUILD_UTILS)
    add_subdirectory(utils)
endif()

option(ZWAVED_BUILD_TESTS "Build unit tests under tests/" ON)
if(ZWAVED_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()

# Target: fix-tidy - Fix issues with clang-tidy
add_custom_target(fix-tidy
    COMMAND clang-tidy --fix -p ${CMAKE_BINARY_DIR}
    COMMENT "Fixing issues with clang-tidy..."
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Target: check - Check code without fixing
add_custom_target(check
    COMMAND clang-tidy -p ${CMAKE_BINARY_DIR}
    COMMENT "Checking code analysis (no fixes)..."
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
