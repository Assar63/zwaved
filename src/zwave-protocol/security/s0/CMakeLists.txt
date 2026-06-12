# src/zwave-protocol/security/s0/CMakeLists.txt
# Security S0 (CC 0x98) — encrypted command encapsulation. Phase 1 (#162)
# lands the crypto primitives; nonce table, network key, and the
# encapsulation codec follow in later phases of the epic (#26).

target_sources(zwaved PRIVATE
    Crypto.cpp
    NetworkKey.cpp
    NonceTable.cpp
    Security.cpp
    # Constructor-armed (CONFIG_SECURITY_PRIO) — must be direct zwaved
    # sources, never passively-linked archive members, or their .init_array
    # entries are dropped and the services never run.
    NetworkKeyService.cpp
    NonceResponder.cpp
)

# AES-128 ECB/OFB/CBC + RAND_bytes via OpenSSL libcrypto (no TLS).
target_link_libraries(zwaved PRIVATE PkgConfig::OPENSSL)
