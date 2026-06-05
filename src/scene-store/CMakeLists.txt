# src/scene-store/CMakeLists.txt
# Durable store for daemon-side scenes and their press triggers,
# persisted to SQLite (shares nodes.db with the other stores).
# Foundation for the scene-controller epic (#119): the SceneOrchestrator
# (#121) drains trigger lookups from here and the D-Bus surface (#122)
# does CRUD over it.

target_sources(zwaved PRIVATE
    SceneStore.cpp
)

target_link_libraries(zwaved PRIVATE PkgConfig::SQLITE3)
