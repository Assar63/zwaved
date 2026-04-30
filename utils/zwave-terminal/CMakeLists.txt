# utils/zwave-terminal/CMakeLists.txt
# ncurses TUI client for the zwaved D-Bus interface.

add_executable(zwave-terminal main.cpp)

target_link_libraries(zwave-terminal PRIVATE
    PkgConfig::NCURSES
    PkgConfig::SDBUS_CPP
)

set_target_properties(zwave-terminal PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
