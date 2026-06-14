# src/span-store/CMakeLists.txt
# Durable per-peer Security S2 SPAN state, persisted to SQLite (shares
# nodes.db with the node registry). Loaded at startup and saved as SPANs
# advance so a daemon restart resumes in lockstep instead of forcing a
# Nonce-Sync (SOS) resync on the next encrypted frame.

target_sources(zwaved PRIVATE
    SpanStore.cpp
)

target_link_libraries(zwaved PRIVATE PkgConfig::SQLITE3)
