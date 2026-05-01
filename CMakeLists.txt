cmake_minimum_required(VERSION 3.20)
project(zwaved CXX)

set(CMAKE_CXX_STANDARD 26)

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
