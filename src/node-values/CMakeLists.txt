# src/node-values/CMakeLists.txt
# Per-node value cache (#213): last-known dynamic values (on/off, level,
# battery, sensor readings, config params, …) each with a timestamp, in a
# node_values table in the shared nodes.db on its own SQLite connection.
# Fed by a bus recorder on the typed CC report events; read by the node-info
# view (#45). Distinct from node-registry (identity) and node-metadata (labels).

target_sources(zwaved PRIVATE
    NodeValues.cpp
    Recorder.cpp
)

target_link_libraries(zwaved PRIVATE PkgConfig::SQLITE3)
