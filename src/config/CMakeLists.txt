# src/config/CMakeLists.txt
# Daemon configuration loader. Parses a minimal INI-flavoured file by
# hand (no third-party dep) and publishes the resolved settings as
# retained events on MessageBus — every module subscribes on the bus
# rather than including this header.

target_sources(zwaved PRIVATE
    Config.cpp
)
