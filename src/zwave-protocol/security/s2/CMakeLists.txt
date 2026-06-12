# src/zwave-protocol/security/s2/CMakeLists.txt
# Security S2 (CC 0x9F) — second-generation encrypted encapsulation (#27).
# Phase 1 (#179) lands the crypto primitives; the network keys, SPAN, KEX,
# encapsulation codec and integration follow in later phases of the epic.

target_sources(zwaved PRIVATE
    Crypto.cpp
)

# AES-128-CCM + CMAC + Curve25519 ECDH, all via OpenSSL libcrypto (no TLS,
# no libsodium — see #27).
target_link_libraries(zwaved PRIVATE PkgConfig::OPENSSL)
