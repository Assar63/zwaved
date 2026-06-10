# src/message-bus/CMakeLists.txt
# In-process publish/subscribe bus. Thin wrapper around eventpp; kept
# in its own module so the underlying library can be replaced without
# disturbing publishers or subscribers.
#
# MessageBus.gen.cpp is emitted by scripts/codegen/ at build time --
# it carries the explicit instantiations of subscribe<T>/publish<T>
# for every event declared in InterfaceManifest.yml. It #includes
# MessageBusInternal.hpp for the template bodies, hence the include
# directory below.
#
# The bus core is built ONCE into the `zwaved_bus` STATIC library and
# linked by the daemon and every test executable, rather than recompiled
# into each (#140). MessageBus.gen.cpp is a ~25 MB object (all those
# template instantiations); recompiling it per test exe under a parallel
# build is what OOM-killed the gnu CI job (#138's `--parallel 2` cap was
# the stop-gap). A STATIC archive is safe here because neither
# MessageBus.cpp nor the generated TU carries `__attribute__((constructor))`
# -- they're pulled into a binary by ordinary symbol reference (everything
# calls publish/subscribe). The constructor-bearing modules (Logger, Config,
# the orchestrators, the threads) must stay *direct sources* of the `zwaved`
# executable: in a passively-linked archive their `.init_array` self-
# registration would be silently dropped (use an OBJECT library or
# --whole-archive if one ever needs sharing).

set_source_files_properties(
    "${ZWAVED_GENERATED_DIR}/MessageBus.gen.cpp"
    PROPERTIES GENERATED TRUE
)

add_library(zwaved_bus STATIC
    MessageBus.cpp
    "${ZWAVED_GENERATED_DIR}/MessageBus.gen.cpp"
)
add_dependencies(zwaved_bus zwaved_codegen)

# PUBLIC so consumers (the daemon + every test exe) inherit the headers
# (MessageBus.hpp / MessageBusInternal.hpp + the generated MessageBus.gen.hpp)
# and the eventpp dependency just by linking the lib.
target_include_directories(zwaved_bus PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${ZWAVED_GENERATED_DIR}"
)
target_link_libraries(zwaved_bus PUBLIC eventpp::eventpp)

target_link_libraries(zwaved PRIVATE zwaved_bus)
