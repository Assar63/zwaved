# tests/CMakeLists.txt
#
# One executable per file; each links the specific protocol-layer
# source files under test rather than the whole `zwaved` target.
# Keeping the dep set narrow means a test exe builds in seconds and
# doesn't drag in libudev / sdbus-c++ / sqlite3.
#
# `gtest_discover_tests` registers each TEST() with CTest at build
# time; run the suite with:
#     ctest --test-dir cmake-build-gnu --output-on-failure
#
# CC tests pull in the matching application/<Name>.gen.{hpp,cpp}
# from the codegen output via ZWAVED_GENERATED_DIR (also a dep on
# the zwaved_codegen target, so the .gen files exist before the
# test compiles).

include(GoogleTest)

set(SRC_DIR "${CMAKE_SOURCE_DIR}/src/zwave-protocol")
set(GENERATED_APPLICATION_DIR "${ZWAVED_GENERATED_DIR}/application")

set_source_files_properties(
    "${GENERATED_APPLICATION_DIR}/BinarySwitch.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/MultilevelSwitch.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/Basic.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/Battery.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/ManufacturerSpecific.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/NodeVersion.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/ZWavePlusInfo.gen.cpp"
    PROPERTIES GENERATED TRUE
)

# Tests verify production code; they don't need to *be* production
# code. clang-tidy rules like bugprone-unchecked-optional-access
# trip on perfectly correct gtest patterns (the macros confuse the
# nullness analysis), and bugprone-easily-swappable-parameters fires
# on every TEST() macro. Disable clang-tidy for the test targets so
# the suite stays focused on functional correctness.
function(zwaved_test_target target)
    set_target_properties("${target}" PROPERTIES CXX_CLANG_TIDY "")
    add_dependencies("${target}" zwaved_codegen)
endfunction()

# ---- HostApi --------------------------------------------------------
add_executable(HostApi_test
    HostApi_test.cpp
    "${SRC_DIR}/HostApi.cpp"
    "${SRC_DIR}/ZwaveDataFrame.cpp"
)
target_include_directories(HostApi_test PRIVATE "${SRC_DIR}")
target_link_libraries(HostApi_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(HostApi_test)
gtest_discover_tests(HostApi_test)

# ---- BinarySwitch ---------------------------------------------------
add_executable(BinarySwitch_test
    BinarySwitch_test.cpp
    "${SRC_DIR}/application/BinarySwitch.cpp"
    "${GENERATED_APPLICATION_DIR}/BinarySwitch.gen.cpp"
)
target_include_directories(BinarySwitch_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(BinarySwitch_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(BinarySwitch_test)
gtest_discover_tests(BinarySwitch_test)

# ---- Basic ----------------------------------------------------------
add_executable(Basic_test
    Basic_test.cpp
    "${SRC_DIR}/application/Basic.cpp"
    "${GENERATED_APPLICATION_DIR}/Basic.gen.cpp"
)
target_include_directories(Basic_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(Basic_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(Basic_test)
gtest_discover_tests(Basic_test)

# ---- MultilevelSwitch -----------------------------------------------
add_executable(MultilevelSwitch_test
    MultilevelSwitch_test.cpp
    "${SRC_DIR}/application/MultilevelSwitch.cpp"
    "${GENERATED_APPLICATION_DIR}/MultilevelSwitch.gen.cpp"
)
target_include_directories(MultilevelSwitch_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(MultilevelSwitch_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(MultilevelSwitch_test)
gtest_discover_tests(MultilevelSwitch_test)

# ---- Battery --------------------------------------------------------
add_executable(Battery_test
    Battery_test.cpp
    "${SRC_DIR}/application/Battery.cpp"
    "${GENERATED_APPLICATION_DIR}/Battery.gen.cpp"
)
target_include_directories(Battery_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(Battery_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(Battery_test)
gtest_discover_tests(Battery_test)

# ---- ManufacturerSpecific -------------------------------------------
add_executable(ManufacturerSpecific_test
    ManufacturerSpecific_test.cpp
    "${SRC_DIR}/application/ManufacturerSpecific.cpp"
    "${GENERATED_APPLICATION_DIR}/ManufacturerSpecific.gen.cpp"
)
target_include_directories(ManufacturerSpecific_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(ManufacturerSpecific_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(ManufacturerSpecific_test)
gtest_discover_tests(ManufacturerSpecific_test)

# ---- ZWavePlusInfo --------------------------------------------------
add_executable(ZWavePlusInfo_test
    ZWavePlusInfo_test.cpp
    "${SRC_DIR}/application/ZWavePlusInfo.cpp"
    "${GENERATED_APPLICATION_DIR}/ZWavePlusInfo.gen.cpp"
)
target_include_directories(ZWavePlusInfo_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(ZWavePlusInfo_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(ZWavePlusInfo_test)
gtest_discover_tests(ZWavePlusInfo_test)

# ---- NodeVersion ----------------------------------------------------
add_executable(NodeVersion_test
    NodeVersion_test.cpp
    "${SRC_DIR}/application/NodeVersion.cpp"
    "${GENERATED_APPLICATION_DIR}/NodeVersion.gen.cpp"
)
target_include_directories(NodeVersion_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(NodeVersion_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(NodeVersion_test)
gtest_discover_tests(NodeVersion_test)

# ---- Association ----------------------------------------------------
add_executable(Association_test
    Association_test.cpp
    "${SRC_DIR}/application/Association.cpp"
)
target_include_directories(Association_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(Association_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(Association_test)
gtest_discover_tests(Association_test)

# ---- MultichannelAssociation ----------------------------------------
add_executable(MultichannelAssociation_test
    MultichannelAssociation_test.cpp
    "${SRC_DIR}/application/MultichannelAssociation.cpp"
)
target_include_directories(MultichannelAssociation_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(MultichannelAssociation_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(MultichannelAssociation_test)
gtest_discover_tests(MultichannelAssociation_test)

# ---- FrameTransport -------------------------------------------------
# Exercises the real poll() / read() / write() syscalls — drives the
# transport over one end of a socketpair via SerialPort::adoptFd from
# the other side.
add_executable(FrameTransport_test
    FrameTransport_test.cpp
    "${SRC_DIR}/FrameTransport.cpp"
    "${SRC_DIR}/SerialPort.cpp"
    "${SRC_DIR}/ZwaveDataFrame.cpp"
)
target_include_directories(FrameTransport_test PRIVATE "${SRC_DIR}")
target_link_libraries(FrameTransport_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(FrameTransport_test)
gtest_discover_tests(FrameTransport_test)
