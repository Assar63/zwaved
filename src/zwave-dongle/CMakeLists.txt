# src/zwave/CMakeLists.txt
# Z-Wave functionality sources

target_sources(zwaved PRIVATE
    MonitorThread.cpp
    DeviceHandoff.cpp
)
