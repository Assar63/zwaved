# src/node-metadata/CMakeLists.txt
# Human-authored descriptive per-node metadata (#83) — free-form
# key/value strings in a node_metadata table in the shared nodes.db, on
# its own SQLite connection. Read/written over D-Bus
# (Set/Get/DeleteNodeMetadata); the daemon never acts on it.

target_sources(zwaved PRIVATE
    NodeMetadata.cpp
)

target_link_libraries(zwaved PRIVATE PkgConfig::SQLITE3)
