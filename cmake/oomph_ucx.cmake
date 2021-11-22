# set all ucx related options and values

#------------------------------------------------------------------------------
# Enable ucx support
#------------------------------------------------------------------------------
set(OOMPH_WITH_UCX OFF CACHE BOOL "Build with UCX backend")

if (OOMPH_WITH_UCX)
    find_package(UCP REQUIRED)
    add_library(oomph_ucx SHARED)
    add_library(oomph::ucx ALIAS oomph_ucx)
    oomph_shared_lib_options(oomph_ucx)
    target_link_libraries(oomph_ucx PUBLIC UCP::libucp)

    set(OOMPH_UCX_USE_PMI OFF CACHE BOOL "use PMIx library for out-of-band initialization")
    if (OOMPH_UCX_USE_PMI)
        find_package(PMIx REQUIRED)
        target_link_libraries(oomph_ucx PRIVATE PMIx::libpmix)
        target_compile_definitions(oomph_ucx PRIVATE OOMPH_UCX_USE_PMI)
    endif()

    set(OOMPH_UCX_USE_SPIN_LOCK ON CACHE BOOL "use pthread spin locks")
    if (OOMPH_UCX_USE_SPIN_LOCK)
        find_package(Threads REQUIRED)
        target_link_libraries(oomph_ucx PRIVATE Threads::Threads)
        target_compile_definitions(oomph_ucx PRIVATE OOMPH_UCX_USE_SPIN_LOCK)
    endif()

    install(TARGETS oomph_ucx
        EXPORT oomph-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

