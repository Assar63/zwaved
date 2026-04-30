# src/node-registry/CMakeLists.txt
# In-memory registry of currently-included Z-Wave nodes. Populated by
# the protocol thread on inclusion/exclusion completion; queried by
# external-API backends.

target_sources(zwaved PRIVATE
    NodeRegistry.cpp
)
