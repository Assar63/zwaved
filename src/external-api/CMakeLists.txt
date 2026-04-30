# src/external-api/CMakeLists.txt
# External API surface for zwaved. Currently exposes the host API over D-Bus
# (sdbus-c++); ubus is reserved for future support.

target_sources(zwaved PRIVATE
    ExternalApiThread.cpp
)

if(ZWAVED_BUILD_DBUS)
    target_sources(zwaved PRIVATE DBusBackend.cpp)
    target_compile_definitions(zwaved PRIVATE ZWAVED_HAS_DBUS=1)
    target_link_libraries(zwaved PRIVATE PkgConfig::SDBUS_CPP)
endif()

if(ZWAVED_BUILD_UBUS)
    # Placeholder: ubus backend not yet implemented.
    target_compile_definitions(zwaved PRIVATE ZWAVED_HAS_UBUS=1)
    message(WARNING "ZWAVED_EXTERNAL_API requests ubus, but the ubus backend is not implemented yet")
endif()
