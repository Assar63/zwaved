# src/zwave-protocol/security/CMakeLists.txt
# Encrypted-transport command classes: S0 (CC 0x98, #26) and S2 (CC 0x9F, #27).

# Secret-handling primitives shared by both stacks (#238): constant-time
# comparison and zeroization, over OpenSSL's CRYPTO_memcmp / OPENSSL_cleanse.
target_sources(zwaved PRIVATE
    SecureMemory.cpp
)
target_link_libraries(zwaved PRIVATE PkgConfig::OPENSSL)

add_subdirectory(s0)
add_subdirectory(s2)
