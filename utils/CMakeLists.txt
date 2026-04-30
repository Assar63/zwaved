# utils/CMakeLists.txt
# Optional companion utilities. Independent of the zwaved daemon binary;
# enabled via the ZWAVED_BUILD_UTILS option in the root CMakeLists.

pkg_check_modules(NCURSES REQUIRED IMPORTED_TARGET ncurses)

# sdbus-c++ may already have been resolved by the daemon build (when
# ZWAVED_BUILD_DBUS is on); only resolve it again if not.
if(NOT TARGET PkgConfig::SDBUS_CPP)
    pkg_check_modules(SDBUS_CPP REQUIRED IMPORTED_TARGET sdbus-c++)
endif()

add_subdirectory(zwave-terminal)
