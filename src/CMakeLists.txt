# src/CMakeLists.txt
# Collects source files from subdirectories

add_executable(zwaved
    main.cpp
    SignalHandler.cpp
    zwaved.h
)

# Add subdirectories for modular components
add_subdirectory(logger)
add_subdirectory(config)
add_subdirectory(message-bus)
add_subdirectory(node-registry)
add_subdirectory(zwave-dongle)
add_subdirectory(zwave-protocol)
add_subdirectory(external-api)

target_link_libraries(zwaved PRIVATE PkgConfig::LIBUDEV)

# Generated Version.hpp lives in the build tree alongside the rest of
# CMake's output; expose it on the daemon's include path.
target_include_directories(zwaved PRIVATE "${CMAKE_BINARY_DIR}/generated")

# Set output directory to root build directory
set_target_properties(zwaved PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
