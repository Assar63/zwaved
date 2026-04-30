# src/zwave-protocol/application/CMakeLists.txt
# Application-layer command-class codecs. Each CC produces and parses
# the byte sequence that goes inside FUNC_ID_ZW_SEND_DATA payloads;
# none of them touch the dongle directly. New CCs (Multilevel Switch,
# Configuration, etc.) belong here.

target_sources(zwaved PRIVATE
    BinarySwitch.cpp
    Association.cpp
)
