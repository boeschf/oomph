# ---------------------------------------------------------------------
# global implementation detail options
# ---------------------------------------------------------------------
set(OOMPH_USE_FAST_PIMPL OFF CACHE BOOL "store private implementations on stack")
set(OOMPH_ENABLE_BARRIER ON CACHE BOOL "enable thread barrier (disable for task based runtime)")
mark_as_advanced(OOMPH_USE_FAST_PIMPL)

# ---------------------------------------------------------------------
# compiler and linker flags
# ---------------------------------------------------------------------
function(oomph_target_compile_options target)
    set_target_properties(${target} PROPERTIES INTERFACE_POSITION_INDEPENDENT_CODE ON)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

function(oomph_target_link_options target)
    target_link_libraries(${target} PUBLIC oomph)
    target_link_libraries(${target} PUBLIC oomph_common)
endfunction()

function(oomph_shared_lib_options target)
    oomph_target_compile_options(${target})
    oomph_target_link_options(${target})
endfunction()

# ---------------------------------------------------------------------
# common library (static): independent of backend
# ---------------------------------------------------------------------
add_library(oomph_common STATIC)
oomph_target_compile_options(oomph_common)
target_link_libraries(oomph_common PUBLIC oomph)

# ---------------------------------------------------------------------
# install rules
# ---------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS oomph oomph_common
    EXPORT oomph-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

