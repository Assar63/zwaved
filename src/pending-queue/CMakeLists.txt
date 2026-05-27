# src/pending-queue/CMakeLists.txt
# Per-node durable command queue, persisted to SQLite (shares
# nodes.db with the node registry). Drained by WakeUpOrchestrator
# on incoming WAKE_UP_NOTIFICATION; fed by InclusionOrchestrator
# and by the protocol thread when the daemon decides a Set / Get
# is for a known-sleeping node.

target_sources(zwaved PRIVATE
    PendingQueue.cpp
)

target_link_libraries(zwaved PRIVATE PkgConfig::SQLITE3)
