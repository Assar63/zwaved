# src/CMakeLists.txt
# Collects source files from subdirectories

# Create the main executable
add_executable(zwaved main.cpp
        zwaved.h)

# Add subdirectories for modular components
add_subdirectory(zwave-dongle)
add_subdirectory(zwave-protocol)

target_sources(zwaved PRIVATE
        SignalHandler.cpp
)

# Link with libudev
target_include_directories(zwaved PRIVATE ${LIBUDEV_INCLUDE_DIRS})
target_link_libraries(zwaved ${LIBUDEV_LIBRARIES})

# Set output directory to root build directory
set_target_properties(zwaved PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)



