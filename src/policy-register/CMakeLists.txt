# src/policy-register/CMakeLists.txt
# Per-device / per-node post-inclusion policy store (#66). Two tables in
# the shared nodes.db (device_policies, node_policy_overrides) on its own
# SQLite connection. Read by InclusionOrchestrator (#67) and
# WakeUpOrchestrator (#68); written via the D-Bus policy CRUD surface
# (#69).

target_sources(zwaved PRIVATE
    PolicyRegister.cpp
)

target_link_libraries(zwaved PRIVATE PkgConfig::SQLITE3)
