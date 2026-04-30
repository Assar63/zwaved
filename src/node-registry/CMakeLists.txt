# src/node-registry/CMakeLists.txt
# In-memory registry of currently-included Z-Wave nodes, persisted to
# SQLite so the list survives daemon restarts. Populated by the
# protocol thread on inclusion/exclusion completion; queried by
# external-API backends.

target_sources(zwaved PRIVATE
    NodeRegistry.cpp
)

target_link_libraries(zwaved PRIVATE PkgConfig::SQLITE3)
