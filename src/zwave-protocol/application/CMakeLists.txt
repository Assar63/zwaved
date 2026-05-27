# src/zwave-protocol/application/CMakeLists.txt
# Application-layer command-class codecs. Each CC produces and parses
# the byte sequence that goes inside FUNC_ID_ZW_SEND_DATA payloads;
# none of them touch the dongle directly. New CCs (Multilevel Switch,
# Configuration, etc.) belong here.
#
# The .gen.{hpp,cpp} files under cmake-build-*/generated/application/
# are emitted by scripts/codegen/ from InterfaceManifest.yml. Every CC
# gets a constants header; CCs whose `wire:` shape is fully expressed
# in the manifest also get encoder bodies in a .gen.cpp. The
# hand-written .{hpp,cpp} pairs include the .gen.hpp and provide the
# irregular bits (struct Report, decode functions, multi-frame
# encoders).

set_source_files_properties(
    "${ZWAVED_GENERATED_DIR}/application/BinarySwitch.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/MultilevelSwitch.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/Basic.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/Battery.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/ManufacturerSpecific.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/NodeVersion.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/ZWavePlusInfo.gen.cpp"
    PROPERTIES GENERATED TRUE
)

target_sources(zwaved PRIVATE
    Basic.cpp
    Battery.cpp
    BinarySwitch.cpp
    ManufacturerSpecific.cpp
    MultilevelSwitch.cpp
    NodeVersion.cpp
    ZWavePlusInfo.cpp
    Association.cpp
    MultichannelAssociation.cpp
    "${ZWAVED_GENERATED_DIR}/application/BinarySwitch.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/MultilevelSwitch.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/Basic.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/Battery.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/ManufacturerSpecific.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/NodeVersion.gen.cpp"
    "${ZWAVED_GENERATED_DIR}/application/ZWavePlusInfo.gen.cpp"
)

# Hand-written .{hpp,cpp} pairs `#include "<Name>.gen.hpp"`; expose
# the generated application/ subdir on the daemon's include path so
# both forms work from anywhere in the tree.
target_include_directories(zwaved PRIVATE "${ZWAVED_GENERATED_DIR}/application")
