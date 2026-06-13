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
    "${GENERATED_APPLICATION_DIR}/WakeUp.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/ZWavePlusInfo.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/SensorMultilevel.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/SensorBinary.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/ThermostatMode.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/ThermostatOperatingState.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/ThermostatFanMode.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/DoorLock.gen.cpp"
    "${GENERATED_APPLICATION_DIR}/UserCode.gen.cpp"
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

# ---- SensorMultilevel -----------------------------------------------
# SensorMultilevel shares the precision/scale/size codec with Thermostat
# Setpoint via EncodedValue, so the test links it too.
add_executable(SensorMultilevel_test
    SensorMultilevel_test.cpp
    "${SRC_DIR}/application/SensorMultilevel.cpp"
    "${SRC_DIR}/application/EncodedValue.cpp"
    "${GENERATED_APPLICATION_DIR}/SensorMultilevel.gen.cpp"
)
target_include_directories(SensorMultilevel_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(SensorMultilevel_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(SensorMultilevel_test)
gtest_discover_tests(SensorMultilevel_test)

# ---- SensorBinary ---------------------------------------------------
add_executable(SensorBinary_test
    SensorBinary_test.cpp
    "${SRC_DIR}/application/SensorBinary.cpp"
    "${GENERATED_APPLICATION_DIR}/SensorBinary.gen.cpp"
)
target_include_directories(SensorBinary_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(SensorBinary_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(SensorBinary_test)
gtest_discover_tests(SensorBinary_test)

# ---- ThermostatMode -------------------------------------------------
add_executable(ThermostatMode_test
    ThermostatMode_test.cpp
    "${SRC_DIR}/application/ThermostatMode.cpp"
    "${GENERATED_APPLICATION_DIR}/ThermostatMode.gen.cpp"
)
target_include_directories(ThermostatMode_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(ThermostatMode_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(ThermostatMode_test)
gtest_discover_tests(ThermostatMode_test)

# ---- EncodedValue ---------------------------------------------------
# Shared precision/scale/size value codec (SensorMultilevel + Setpoint).
add_executable(EncodedValue_test
    EncodedValue_test.cpp
    "${SRC_DIR}/application/EncodedValue.cpp"
)
target_include_directories(EncodedValue_test PRIVATE "${SRC_DIR}/application")
target_link_libraries(EncodedValue_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(EncodedValue_test)
gtest_discover_tests(EncodedValue_test)

# ---- ThermostatSetpoint ---------------------------------------------
# No .gen.cpp (Set/Get carry args); links the hand-written codec plus the
# shared EncodedValue helper.
add_executable(ThermostatSetpoint_test
    ThermostatSetpoint_test.cpp
    "${SRC_DIR}/application/ThermostatSetpoint.cpp"
    "${SRC_DIR}/application/EncodedValue.cpp"
)
target_include_directories(ThermostatSetpoint_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(ThermostatSetpoint_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(ThermostatSetpoint_test)
gtest_discover_tests(ThermostatSetpoint_test)

# ---- ThermostatOperatingState ---------------------------------------
add_executable(ThermostatOperatingState_test
    ThermostatOperatingState_test.cpp
    "${SRC_DIR}/application/ThermostatOperatingState.cpp"
    "${GENERATED_APPLICATION_DIR}/ThermostatOperatingState.gen.cpp"
)
target_include_directories(ThermostatOperatingState_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(ThermostatOperatingState_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(ThermostatOperatingState_test)
gtest_discover_tests(ThermostatOperatingState_test)

# ---- ThermostatFanMode ----------------------------------------------
add_executable(ThermostatFanMode_test
    ThermostatFanMode_test.cpp
    "${SRC_DIR}/application/ThermostatFanMode.cpp"
    "${GENERATED_APPLICATION_DIR}/ThermostatFanMode.gen.cpp"
)
target_include_directories(ThermostatFanMode_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(ThermostatFanMode_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(ThermostatFanMode_test)
gtest_discover_tests(ThermostatFanMode_test)

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

# ---- Configuration --------------------------------------------------
# Note: Configuration has no .gen.cpp (constants-only header), so the
# test executable links only the hand-written Configuration.cpp.
add_executable(Configuration_test
    Configuration_test.cpp
    "${SRC_DIR}/application/Configuration.cpp"
)
target_include_directories(Configuration_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(Configuration_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(Configuration_test)
gtest_discover_tests(Configuration_test)

# ---- Notification ---------------------------------------------------
# Note: Notification has no .gen.cpp (constants-only header), so the
# test executable links only the hand-written Notification.cpp.
add_executable(Notification_test
    Notification_test.cpp
    "${SRC_DIR}/application/Notification.cpp"
)
target_include_directories(Notification_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(Notification_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(Notification_test)
gtest_discover_tests(Notification_test)

# ---- ColorSwitch ----------------------------------------------------
# No .gen.cpp (Get/Set carry variable/argument payloads).
add_executable(ColorSwitch_test
    ColorSwitch_test.cpp
    "${SRC_DIR}/application/ColorSwitch.cpp"
)
target_include_directories(ColorSwitch_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(ColorSwitch_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(ColorSwitch_test)
gtest_discover_tests(ColorSwitch_test)

# ---- CentralScene ---------------------------------------------------
# No .gen.cpp (push-only; decode-only, no encoders).
add_executable(CentralScene_test
    CentralScene_test.cpp
    "${SRC_DIR}/application/CentralScene.cpp"
)
target_include_directories(CentralScene_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(CentralScene_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(CentralScene_test)
gtest_discover_tests(CentralScene_test)

# ---- SceneActivation ------------------------------------------------
# No .gen.cpp (push-only; decode-only, no encoders).
add_executable(SceneActivation_test
    SceneActivation_test.cpp
    "${SRC_DIR}/application/SceneActivation.cpp"
)
target_include_directories(SceneActivation_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(SceneActivation_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(SceneActivation_test)
gtest_discover_tests(SceneActivation_test)

# ---- Indicator ------------------------------------------------------
add_executable(Indicator_test
    Indicator_test.cpp
    "${SRC_DIR}/application/Indicator.cpp"
    "${GENERATED_APPLICATION_DIR}/Indicator.gen.cpp"
)
target_include_directories(Indicator_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(Indicator_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(Indicator_test)
gtest_discover_tests(Indicator_test)

# ---- Crc16Encap -----------------------------------------------------
# No .gen.cpp (header-only constants; hand-written CRC + verify/unwrap).
add_executable(Crc16Encap_test
    Crc16Encap_test.cpp
    "${SRC_DIR}/application/Crc16Encap.cpp"
)
target_include_directories(Crc16Encap_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(Crc16Encap_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(Crc16Encap_test)
gtest_discover_tests(Crc16Encap_test)

# ---- S2 crypto ------------------------------------------------------
# Security S2 (CC 0x9F) phase 1: AES-128-CCM + CMAC + X25519 over libcrypto.
add_executable(s2_crypto_test
    s2_crypto_test.cpp
    "${SRC_DIR}/security/s2/Crypto.cpp"
)
target_include_directories(s2_crypto_test PRIVATE
    "${SRC_DIR}/security/s2"
)
target_link_libraries(s2_crypto_test PRIVATE GTest::gtest GTest::gtest_main PkgConfig::OPENSSL)
zwaved_test_target(s2_crypto_test)
gtest_discover_tests(s2_crypto_test)

# ---- S2 network keys ------------------------------------------------
# Security S2 (CC 0x9F) phase 2: multi-class key load-or-generate. Pure core
# (no bus); links libcrypto for RAND_bytes.
add_executable(s2_network_keys_test
    s2_network_keys_test.cpp
    "${SRC_DIR}/security/s2/NetworkKeys.cpp"
)
target_include_directories(s2_network_keys_test PRIVATE
    "${SRC_DIR}/security/s2"
)
target_link_libraries(s2_network_keys_test PRIVATE GTest::gtest GTest::gtest_main PkgConfig::OPENSSL)
zwaved_test_target(s2_network_keys_test)
gtest_discover_tests(s2_network_keys_test)

# ---- S2 public key + DSK --------------------------------------------
# Security S2 (CC 0x9F) phase 6: PUBLIC_KEY_REPORT codec + DSK helpers.
add_executable(s2_public_key_test
    s2_public_key_test.cpp
    "${SRC_DIR}/security/s2/PublicKey.cpp"
    "${SRC_DIR}/security/s2/Crypto.cpp"
)
target_include_directories(s2_public_key_test PRIVATE
    "${SRC_DIR}/security/s2"
)
target_link_libraries(s2_public_key_test PRIVATE GTest::gtest GTest::gtest_main PkgConfig::OPENSSL)
zwaved_test_target(s2_public_key_test)
gtest_discover_tests(s2_public_key_test)

# ---- S2 key install -------------------------------------------------
# Security S2 (CC 0x9F) phase 7: NETWORK_KEY_GET/REPORT/VERIFY + TRANSFER_END.
add_executable(s2_key_install_test
    s2_key_install_test.cpp
    "${SRC_DIR}/security/s2/KeyInstall.cpp"
    "${SRC_DIR}/security/s2/Kex.cpp"
)
target_include_directories(s2_key_install_test PRIVATE
    "${SRC_DIR}/security/s2"
)
target_link_libraries(s2_key_install_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(s2_key_install_test)
gtest_discover_tests(s2_key_install_test)

# ---- S2 KEX handshake -----------------------------------------------
# Security S2 (CC 0x9F) phase 5: KEX_GET/REPORT/SET/FAIL codec + grant policy.
add_executable(s2_kex_test
    s2_kex_test.cpp
    "${SRC_DIR}/security/s2/Kex.cpp"
)
target_include_directories(s2_kex_test PRIVATE
    "${SRC_DIR}/security/s2"
)
target_link_libraries(s2_kex_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(s2_kex_test)
gtest_discover_tests(s2_kex_test)

# ---- S2 encapsulation codec -----------------------------------------
# Security S2 (CC 0x9F) phase 4: MESSAGE_ENCAPSULATION encrypt/decrypt.
add_executable(s2_encapsulation_test
    s2_encapsulation_test.cpp
    "${SRC_DIR}/security/s2/Encapsulation.cpp"
    "${SRC_DIR}/security/s2/Crypto.cpp"
)
target_include_directories(s2_encapsulation_test PRIVATE
    "${SRC_DIR}/security/s2"
)
target_link_libraries(s2_encapsulation_test PRIVATE GTest::gtest GTest::gtest_main PkgConfig::OPENSSL)
zwaved_test_target(s2_encapsulation_test)
gtest_discover_tests(s2_encapsulation_test)

# ---- S2 key derivation ----------------------------------------------
# Security S2 (CC 0x9F) phase 9 crypto: CKDF temp + network key derivations.
add_executable(s2_key_derivation_test
    s2_key_derivation_test.cpp
    "${SRC_DIR}/security/s2/KeyDerivation.cpp"
    "${SRC_DIR}/security/s2/Crypto.cpp"
)
target_include_directories(s2_key_derivation_test PRIVATE
    "${SRC_DIR}/security/s2"
)
target_link_libraries(s2_key_derivation_test PRIVATE GTest::gtest GTest::gtest_main PkgConfig::OPENSSL)
zwaved_test_target(s2_key_derivation_test)
gtest_discover_tests(s2_key_derivation_test)

# Security S2 (CC 0x9F) nonce-sync codec (#187/#199): NONCE_GET/REPORT + SPAN
# extension. Pure byte codec — no OpenSSL.
add_executable(s2_nonce_sync_test
    s2_nonce_sync_test.cpp
    "${SRC_DIR}/security/s2/NonceSync.cpp"
)
target_include_directories(s2_nonce_sync_test PRIVATE
    "${SRC_DIR}/security/s2"
)
target_link_libraries(s2_nonce_sync_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(s2_nonce_sync_test)
gtest_discover_tests(s2_nonce_sync_test)

# Security S2 (CC 0x9F) SPAN runtime (#187/#199): the per-peer nonce-sync state
# machine. Drives two managers through a handshake + encap round-trip.
add_executable(s2_span_manager_test
    s2_span_manager_test.cpp
    "${SRC_DIR}/security/s2/SpanManager.cpp"
    "${SRC_DIR}/security/s2/NonceSync.cpp"
    "${SRC_DIR}/security/s2/Span.cpp"
    "${SRC_DIR}/security/s2/Encapsulation.cpp"
    "${SRC_DIR}/security/s2/Crypto.cpp"
)
target_include_directories(s2_span_manager_test PRIVATE
    "${SRC_DIR}/security/s2"
)
target_link_libraries(s2_span_manager_test PRIVATE GTest::gtest GTest::gtest_main PkgConfig::OPENSSL)
zwaved_test_target(s2_span_manager_test)
gtest_discover_tests(s2_span_manager_test)

# ---- S2 SPAN --------------------------------------------------------
# Security S2 (CC 0x9F) phase 3: CTR_DRBG SPAN nonce protocol.
add_executable(s2_span_test
    s2_span_test.cpp
    "${SRC_DIR}/security/s2/Span.cpp"
    "${SRC_DIR}/security/s2/Crypto.cpp"
)
target_include_directories(s2_span_test PRIVATE
    "${SRC_DIR}/security/s2"
)
target_link_libraries(s2_span_test PRIVATE GTest::gtest GTest::gtest_main PkgConfig::OPENSSL)
zwaved_test_target(s2_span_test)
gtest_discover_tests(s2_span_test)

# ---- S0 crypto ------------------------------------------------------
# Security S0 (CC 0x98) phase 1: AES primitives over OpenSSL libcrypto.
add_executable(s0_crypto_test
    s0_crypto_test.cpp
    "${SRC_DIR}/security/s0/Crypto.cpp"
)
target_include_directories(s0_crypto_test PRIVATE
    "${SRC_DIR}/security/s0"
)
target_link_libraries(s0_crypto_test PRIVATE GTest::gtest GTest::gtest_main PkgConfig::OPENSSL)
zwaved_test_target(s0_crypto_test)
gtest_discover_tests(s0_crypto_test)

# ---- S0 network key -------------------------------------------------
# Security S0 (CC 0x98) phase 2: load-or-generate key persistence. Pure
# core (no bus); links libcrypto for RAND_bytes.
add_executable(s0_network_key_test
    s0_network_key_test.cpp
    "${SRC_DIR}/security/s0/NetworkKey.cpp"
)
target_include_directories(s0_network_key_test PRIVATE
    "${SRC_DIR}/security/s0"
)
target_link_libraries(s0_network_key_test PRIVATE GTest::gtest GTest::gtest_main PkgConfig::OPENSSL)
zwaved_test_target(s0_network_key_test)
gtest_discover_tests(s0_network_key_test)

# ---- S0 nonce table -------------------------------------------------
# Security S0 (CC 0x98) phase 3: per-peer issued-nonce pool. Pure core
# (no bus); links libcrypto for RAND_bytes.
add_executable(s0_nonce_table_test
    s0_nonce_table_test.cpp
    "${SRC_DIR}/security/s0/NonceTable.cpp"
)
target_include_directories(s0_nonce_table_test PRIVATE
    "${SRC_DIR}/security/s0"
)
target_link_libraries(s0_nonce_table_test PRIVATE GTest::gtest GTest::gtest_main PkgConfig::OPENSSL)
zwaved_test_target(s0_nonce_table_test)
gtest_discover_tests(s0_nonce_table_test)

# ---- S0 security codec ----------------------------------------------
# Security S0 (CC 0x98) phase 3: NONCE_GET / NONCE_REPORT wire codec.
add_executable(s0_security_test
    s0_security_test.cpp
    "${SRC_DIR}/security/s0/Security.cpp"
)
target_include_directories(s0_security_test PRIVATE
    "${SRC_DIR}/security/s0"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(s0_security_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(s0_security_test)
gtest_discover_tests(s0_security_test)

# ---- S0 encapsulation codec -----------------------------------------
# Security S0 (CC 0x98) phase 4: MESSAGE_ENCAPSULATION encrypt/decrypt.
# Composes the crypto primitives; links libcrypto.
add_executable(s0_encapsulation_test
    s0_encapsulation_test.cpp
    "${SRC_DIR}/security/s0/Encapsulation.cpp"
    "${SRC_DIR}/security/s0/Crypto.cpp"
)
target_include_directories(s0_encapsulation_test PRIVATE
    "${SRC_DIR}/security/s0"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(s0_encapsulation_test PRIVATE GTest::gtest GTest::gtest_main PkgConfig::OPENSSL)
zwaved_test_target(s0_encapsulation_test)
gtest_discover_tests(s0_encapsulation_test)

# ---- S0 inbound decrypt seam ----------------------------------------
# Security S0 (CC 0x98) phase 5: cc-translator's inbound MESSAGE_ENCAPSULATION
# decrypt path. Links the seam (armed by its constructor attribute), the full
# S0 stack, Logger, the bus, and libcrypto.
add_executable(s0_inbound_decrypt_test
    s0_inbound_decrypt_test.cpp
    "${CMAKE_SOURCE_DIR}/src/cc-translator/Encapsulation.cpp"
    "${SRC_DIR}/application/Crc16Encap.cpp"     # other unwrappers the seam calls
    "${SRC_DIR}/application/TransportService.cpp"
    "${SRC_DIR}/security/s0/Encapsulation.cpp"
    "${SRC_DIR}/security/s0/NetworkKey.cpp"
    "${SRC_DIR}/security/s0/NonceTable.cpp"
    "${SRC_DIR}/security/s0/Security.cpp"
    "${SRC_DIR}/security/s0/Crypto.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(s0_inbound_decrypt_test PRIVATE
    "${SRC_DIR}/security/s0"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(s0_inbound_decrypt_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    PkgConfig::OPENSSL
    zwaved_bus
)
zwaved_test_target(s0_inbound_decrypt_test)
gtest_discover_tests(s0_inbound_decrypt_test)

# ---- NodeRegistry secure flag ---------------------------------------
# Security S0 (CC 0x98) phase 6: the per-node `secure` column + API.
add_executable(node_registry_secure_test
    node_registry_secure_test.cpp
    "${CMAKE_SOURCE_DIR}/src/node-registry/NodeRegistry.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(node_registry_secure_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/node-registry"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
)
target_link_libraries(node_registry_secure_test PRIVATE
    GTest::gtest GTest::gtest_main PkgConfig::SQLITE3 zwaved_bus)
zwaved_test_target(node_registry_secure_test)
gtest_discover_tests(node_registry_secure_test)

# ---- S0 outbound encrypt-on-send ------------------------------------
# Security S0 (CC 0x98) phase 5b (#175): the outbound nonce->encrypt flow.
add_executable(s0_outbound_test
    s0_outbound_test.cpp
    "${CMAKE_SOURCE_DIR}/src/orchestrator/SecurityOutboundOrchestrator.cpp"
    "${SRC_DIR}/security/s0/Encapsulation.cpp"
    "${SRC_DIR}/security/s0/NetworkKey.cpp"
    "${SRC_DIR}/security/s0/NonceTable.cpp"
    "${SRC_DIR}/security/s0/Security.cpp"
    "${SRC_DIR}/security/s0/Crypto.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(s0_outbound_test PRIVATE
    "${SRC_DIR}/security/s0"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(s0_outbound_test PRIVATE
    GTest::gtest GTest::gtest_main PkgConfig::OPENSSL zwaved_bus)
zwaved_test_target(s0_outbound_test)
gtest_discover_tests(s0_outbound_test)

# ---- S0 inclusion bootstrap -----------------------------------------
# Security S0 (CC 0x98) phase 6: the bootstrap orchestrator sequence.
# Links the orchestrator (armed by its constructor attribute), the full S0
# stack, NodeRegistry, Logger, the bus, sqlite3, and libcrypto.
add_executable(s0_bootstrap_test
    s0_bootstrap_test.cpp
    "${CMAKE_SOURCE_DIR}/src/orchestrator/SecurityBootstrapOrchestrator.cpp"
    "${CMAKE_SOURCE_DIR}/src/node-registry/NodeRegistry.cpp"
    "${SRC_DIR}/security/s0/Encapsulation.cpp"
    "${SRC_DIR}/security/s0/NetworkKey.cpp"
    "${SRC_DIR}/security/s0/NonceTable.cpp"
    "${SRC_DIR}/security/s0/Security.cpp"
    "${SRC_DIR}/security/s0/Crypto.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(s0_bootstrap_test PRIVATE
    "${SRC_DIR}/security/s0"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(s0_bootstrap_test PRIVATE
    GTest::gtest GTest::gtest_main PkgConfig::SQLITE3 PkgConfig::OPENSSL zwaved_bus)
zwaved_test_target(s0_bootstrap_test)
gtest_discover_tests(s0_bootstrap_test)

# ---- S2 inclusion bootstrap -----------------------------------------
# Security S2 (CC 0x9F) phase 9 (#187): the bootstrap orchestrator's
# plaintext key-agreement phase (KEX negotiation → public-key exchange →
# ECDH → temp-key derivation). Links the orchestrator (armed by its
# constructor attribute) + the S2 codecs it drives.
add_executable(s2_bootstrap_test
    s2_bootstrap_test.cpp
    "${CMAKE_SOURCE_DIR}/src/orchestrator/SecurityS2BootstrapOrchestrator.cpp"
    "${SRC_DIR}/security/s2/Crypto.cpp"
    "${SRC_DIR}/security/s2/Kex.cpp"
    "${SRC_DIR}/security/s2/PublicKey.cpp"
    "${SRC_DIR}/security/s2/KeyDerivation.cpp"
    "${SRC_DIR}/security/s2/NonceSync.cpp"
    "${SRC_DIR}/security/s2/Span.cpp"
    "${SRC_DIR}/security/s2/SpanManager.cpp"
    "${SRC_DIR}/security/s2/Encapsulation.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(s2_bootstrap_test PRIVATE
    "${SRC_DIR}/security/s2"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(s2_bootstrap_test PRIVATE
    GTest::gtest GTest::gtest_main PkgConfig::OPENSSL zwaved_bus)
zwaved_test_target(s2_bootstrap_test)
gtest_discover_tests(s2_bootstrap_test)

# ---- S0 nonce responder ---------------------------------------------
# Security S0 (CC 0x98) phase 3: bus-driven NONCE_GET -> NONCE_REPORT
# reactor. Links the responder (armed by its constructor attribute), the
# nonce table + codec, Logger, the bus, and libcrypto.
add_executable(s0_nonce_responder_test
    s0_nonce_responder_test.cpp
    "${SRC_DIR}/security/s0/NonceResponder.cpp"
    "${SRC_DIR}/security/s0/NonceTable.cpp"
    "${SRC_DIR}/security/s0/Security.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(s0_nonce_responder_test PRIVATE
    "${SRC_DIR}/security/s0"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(s0_nonce_responder_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    PkgConfig::OPENSSL
    zwaved_bus
)
zwaved_test_target(s0_nonce_responder_test)
gtest_discover_tests(s0_nonce_responder_test)

# ---- TransportService -----------------------------------------------
# No .gen.cpp (header-only constants; hand-written split + reassembler + FCS).
add_executable(TransportService_test
    TransportService_test.cpp
    "${SRC_DIR}/application/TransportService.cpp"
)
target_include_directories(TransportService_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(TransportService_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(TransportService_test)
gtest_discover_tests(TransportService_test)

# ---- Supervision ----------------------------------------------------
# No .gen.cpp (header-only constants; hand-written encap + report decode).
add_executable(Supervision_test
    Supervision_test.cpp
    "${SRC_DIR}/application/Supervision.cpp"
)
target_include_directories(Supervision_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(Supervision_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(Supervision_test)
gtest_discover_tests(Supervision_test)

# ---- MultiChannel ---------------------------------------------------
# No .gen.cpp (header-only constants; hand-written encap/decap codec).
add_executable(MultiChannel_test
    MultiChannel_test.cpp
    "${SRC_DIR}/application/MultiChannel.cpp"
)
target_include_directories(MultiChannel_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(MultiChannel_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(MultiChannel_test)
gtest_discover_tests(MultiChannel_test)

# ---- DoorLock -------------------------------------------------------
add_executable(DoorLock_test
    DoorLock_test.cpp
    "${SRC_DIR}/application/DoorLock.cpp"
    "${GENERATED_APPLICATION_DIR}/DoorLock.gen.cpp"
)
target_include_directories(DoorLock_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(DoorLock_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(DoorLock_test)
gtest_discover_tests(DoorLock_test)

# ---- UserCode -------------------------------------------------------
add_executable(UserCode_test
    UserCode_test.cpp
    "${SRC_DIR}/application/UserCode.cpp"
    "${GENERATED_APPLICATION_DIR}/UserCode.gen.cpp"
)
target_include_directories(UserCode_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(UserCode_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(UserCode_test)
gtest_discover_tests(UserCode_test)

# ---- Meter ----------------------------------------------------------
# Note: Meter has no .gen.cpp (the Get carries a scale arg, so the encoder
# is hand-written), so the test links only the hand-written Meter.cpp.
add_executable(Meter_test
    Meter_test.cpp
    "${SRC_DIR}/application/Meter.cpp"
)
target_include_directories(Meter_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(Meter_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(Meter_test)
gtest_discover_tests(Meter_test)

# ---- WakeUp ---------------------------------------------------------
add_executable(WakeUp_test
    WakeUp_test.cpp
    "${SRC_DIR}/application/WakeUp.cpp"
    "${GENERATED_APPLICATION_DIR}/WakeUp.gen.cpp"
)
target_include_directories(WakeUp_test PRIVATE
    "${SRC_DIR}/application"
    "${GENERATED_APPLICATION_DIR}"
)
target_link_libraries(WakeUp_test PRIVATE GTest::gtest GTest::gtest_main)
zwaved_test_target(WakeUp_test)
gtest_discover_tests(WakeUp_test)

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
target_link_libraries(MultichannelAssociation_test PRIVATE GTest::gtest GTest::gtest_main zwaved_bus)
zwaved_test_target(MultichannelAssociation_test)
gtest_discover_tests(MultichannelAssociation_test)

# ---- PendingQueue ---------------------------------------------------
# Persistence test target — different from the protocol-layer codecs.
# Links sqlite3 because the SUT opens a real file; each TEST_F() gets
# its own tmpdir DB via the fixture and tears it down on completion.
add_executable(PendingQueue_test
    PendingQueue_test.cpp
    "${CMAKE_SOURCE_DIR}/src/pending-queue/PendingQueue.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(PendingQueue_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/pending-queue"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
)
add_dependencies(PendingQueue_test zwaved_codegen)
target_link_libraries(PendingQueue_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    PkgConfig::SQLITE3
    zwaved_bus
)
zwaved_test_target(PendingQueue_test)
gtest_discover_tests(PendingQueue_test)

# ---- SceneStore -----------------------------------------------------
add_executable(SceneStore_test
    SceneStore_test.cpp
    "${CMAKE_SOURCE_DIR}/src/scene-store/SceneStore.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(SceneStore_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/scene-store"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
)
add_dependencies(SceneStore_test zwaved_codegen)
target_link_libraries(SceneStore_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    PkgConfig::SQLITE3
    zwaved_bus
)
zwaved_test_target(SceneStore_test)
gtest_discover_tests(SceneStore_test)

# ---- MessageBus -----------------------------------------------------
# Reentrancy regression test — links the real bus + its generated
# event instantiations. eventpp is header-only; its include path comes
# in via the eventpp::eventpp interface target.
add_executable(MessageBus_test
    MessageBus_test.cpp
)
target_include_directories(MessageBus_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
)
add_dependencies(MessageBus_test zwaved_codegen)
target_link_libraries(MessageBus_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    zwaved_bus
)
zwaved_test_target(MessageBus_test)
gtest_discover_tests(MessageBus_test)

# ---- PolicyRegister -------------------------------------------------
# Persistence + serialization + merge logic. Links sqlite3 (real file
# per fixture) and the bus (mutations publish PolicyChanged).
add_executable(PolicyRegister_test
    PolicyRegister_test.cpp
    "${CMAKE_SOURCE_DIR}/src/policy-register/PolicyRegister.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(PolicyRegister_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/policy-register"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
)
add_dependencies(PolicyRegister_test zwaved_codegen)
target_link_libraries(PolicyRegister_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    PkgConfig::SQLITE3
    zwaved_bus
)
zwaved_test_target(PolicyRegister_test)
gtest_discover_tests(PolicyRegister_test)

# ---- NodeMetadata ---------------------------------------------------
# Descriptive per-node key/value store. Links sqlite3 (real file per
# fixture) and the bus (mutations publish NodeMetadataChanged).
add_executable(NodeMetadata_test
    NodeMetadata_test.cpp
    "${CMAKE_SOURCE_DIR}/src/node-metadata/NodeMetadata.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(NodeMetadata_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/node-metadata"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
)
add_dependencies(NodeMetadata_test zwaved_codegen)
target_link_libraries(NodeMetadata_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    PkgConfig::SQLITE3
    zwaved_bus
)
zwaved_test_target(NodeMetadata_test)
gtest_discover_tests(NodeMetadata_test)

# ---- WakeUpOrchestrator ---------------------------------------------
# Bus-driven: seeds the persistent queue, publishes a wake-up, asserts
# on the emitted command sequence. Links the orchestrator (armed by its
# constructor attribute), the real queue + bus, Logger, and sqlite3.
add_executable(WakeUpOrchestrator_test
    WakeUpOrchestrator_test.cpp
    "${CMAKE_SOURCE_DIR}/src/orchestrator/WakeUpOrchestrator.cpp"
    "${CMAKE_SOURCE_DIR}/src/pending-queue/PendingQueue.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(WakeUpOrchestrator_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/pending-queue"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
)
add_dependencies(WakeUpOrchestrator_test zwaved_codegen)
target_link_libraries(WakeUpOrchestrator_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    PkgConfig::SQLITE3
    zwaved_bus
)
zwaved_test_target(WakeUpOrchestrator_test)
gtest_discover_tests(WakeUpOrchestrator_test)

# ---- SceneOrchestrator ----------------------------------------------
add_executable(SceneOrchestrator_test
    SceneOrchestrator_test.cpp
    "${CMAKE_SOURCE_DIR}/src/orchestrator/SceneOrchestrator.cpp"
    "${CMAKE_SOURCE_DIR}/src/scene-store/SceneStore.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(SceneOrchestrator_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/scene-store"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
)
add_dependencies(SceneOrchestrator_test zwaved_codegen)
target_link_libraries(SceneOrchestrator_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    PkgConfig::SQLITE3
    zwaved_bus
)
zwaved_test_target(SceneOrchestrator_test)
gtest_discover_tests(SceneOrchestrator_test)

# ---- ThermostatOrchestrator -----------------------------------------
add_executable(ThermostatOrchestrator_test
    ThermostatOrchestrator_test.cpp
    "${CMAKE_SOURCE_DIR}/src/orchestrator/ThermostatOrchestrator.cpp"
    "${CMAKE_SOURCE_DIR}/src/node-metadata/NodeMetadata.cpp"
    "${CMAKE_SOURCE_DIR}/src/node-registry/NodeRegistry.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(ThermostatOrchestrator_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/node-metadata"
    "${CMAKE_SOURCE_DIR}/src/node-registry"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
)
add_dependencies(ThermostatOrchestrator_test zwaved_codegen)
target_link_libraries(ThermostatOrchestrator_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    PkgConfig::SQLITE3
    zwaved_bus
)
zwaved_test_target(ThermostatOrchestrator_test)
gtest_discover_tests(ThermostatOrchestrator_test)

# ---- InclusionOrchestrator ------------------------------------------
# Bus-driven: publishes NodeIncluded, asserts on the lifeline + policy
# command sequence. Links the orchestrator (armed by its constructor),
# the policy register, the bus, Logger, and sqlite3.
add_executable(InclusionOrchestrator_test
    InclusionOrchestrator_test.cpp
    "${CMAKE_SOURCE_DIR}/src/orchestrator/InclusionOrchestrator.cpp"
    "${CMAKE_SOURCE_DIR}/src/policy-register/PolicyRegister.cpp"
    "${CMAKE_SOURCE_DIR}/src/logger/Logger.cpp"
)
target_include_directories(InclusionOrchestrator_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src/policy-register"
    "${CMAKE_SOURCE_DIR}/src/message-bus"
    "${ZWAVED_GENERATED_DIR}"
)
add_dependencies(InclusionOrchestrator_test zwaved_codegen)
target_link_libraries(InclusionOrchestrator_test PRIVATE
    GTest::gtest
    GTest::gtest_main
    PkgConfig::SQLITE3
    zwaved_bus
)
zwaved_test_target(InclusionOrchestrator_test)
gtest_discover_tests(InclusionOrchestrator_test)

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
