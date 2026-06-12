# src/zwave-protocol/security/s0/CMakeLists.txt
# Security S0 (CC 0x98) — encrypted command encapsulation. Phase 1 (#162)
# lands the crypto primitives; nonce table, network key, and the
# encapsulation codec follow in later phases of the epic (#26).

target_sources(zwaved PRIVATE
    Crypto.cpp
    NetworkKey.cpp
    # Constructor-armed (CONFIG_SECURITY_PRIO) — must be a direct zwaved
    # source, never a passively-linked archive member, or its .init_array
    # entry is dropped and the key is never loaded.
    NetworkKeyService.cpp
)

# AES-128 ECB/OFB/CBC + RAND_bytes via OpenSSL libcrypto (no TLS).
target_link_libraries(zwaved PRIVATE PkgConfig::OPENSSL)
