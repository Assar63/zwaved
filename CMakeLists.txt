cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_COMPILER g++-15)
project(zwaved)

set(CMAKE_CXX_STANDARD 26)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUDEV REQUIRED libudev)

# Process source subdirectories
add_subdirectory(src)

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
