target_sources(oomph_common PRIVATE utils.cpp)
target_sources(oomph_common PRIVATE rank_topology.cpp)

if (OOMPH_WITH_MPI)
    add_subdirectory(mpi)
endif()

if (OOMPH_WITH_UCX)
    add_subdirectory(ucx)
endif()

if (OOMPH_WITH_LIBFABRIC)
    #add_subdirectory(libfabric)
endif()
