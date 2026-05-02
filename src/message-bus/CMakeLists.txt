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

set_source_files_properties(
    "${ZWAVED_GENERATED_DIR}/MessageBus.gen.cpp"
    PROPERTIES GENERATED TRUE
)

target_sources(zwaved PRIVATE
    MessageBus.cpp
    "${ZWAVED_GENERATED_DIR}/MessageBus.gen.cpp"
)

target_include_directories(zwaved PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(zwaved PRIVATE eventpp::eventpp)
