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

include(GoogleTest)

set(SRC_DIR "${CMAKE_SOURCE_DIR}/src/zwave-protocol")

# Tests verify production code; they don't need to *be* production
# code. clang-tidy rules like bugprone-unchecked-optional-access
# trip on perfectly correct gtest patterns (the macros confuse the
# nullness analysis), and bugprone-easily-swappable-parameters fires
# on every TEST() macro. Disable clang-tidy for the test targets so
# the suite stays focused on functional correctness.
function(zwaved_test_target target)
    set_target_properties("${target}" PROPERTIES CXX_CLANG_TIDY "")
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
)
target_include_directories(BinarySwitch_test PRIVATE "${SRC_DIR}/application")
target_link_libraries(BinarySwitch_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(BinarySwitch_test)
gtest_discover_tests(BinarySwitch_test)

# ---- Association ----------------------------------------------------
add_executable(Association_test
    Association_test.cpp
    "${SRC_DIR}/application/Association.cpp"
)
target_include_directories(Association_test PRIVATE "${SRC_DIR}/application")
target_link_libraries(Association_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(Association_test)
gtest_discover_tests(Association_test)

# ---- MultichannelAssociation ----------------------------------------
add_executable(MultichannelAssociation_test
    MultichannelAssociation_test.cpp
    "${SRC_DIR}/application/MultichannelAssociation.cpp"
)
target_include_directories(MultichannelAssociation_test PRIVATE "${SRC_DIR}/application")
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
