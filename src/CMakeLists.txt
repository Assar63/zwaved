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

# Set output directory to root build directory
set_target_properties(zwaved PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
