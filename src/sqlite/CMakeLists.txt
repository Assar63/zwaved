# src/sqlite/CMakeLists.txt
# Shared SQLite plumbing for the persistent stores: the `Db` connection
# wrapper (which is where the busy timeout / WAL / synchronous pragmas every
# connection to the shared nodes.db needs are applied) and the `Stmt` RAII
# prepare/bind/step wrapper. Extracted in #233/#234 — the seven stores each
# carried a private copy, which is how the pragmas came to be missing from
# all of them and the checked write from two.

target_sources(zwaved PRIVATE
    Sqlite.cpp
)

target_link_libraries(zwaved PRIVATE PkgConfig::SQLITE3)
