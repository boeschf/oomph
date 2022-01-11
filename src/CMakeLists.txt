target_sources(oomph_common PRIVATE utils.cpp)
target_sources(oomph_common PRIVATE thread_id.cpp)
target_sources(oomph_common PRIVATE rank_topology.cpp)

if (OOMPH_WITH_MPI)
    add_subdirectory(mpi)
endif()

if (OOMPH_WITH_UCX)
    add_subdirectory(ucx)
endif()

if (OOMPH_WITH_LIBFABRIC)
    #message("cmake -DOOMPH_WITH_TESTING=ON -DOOMPH_WITH_LIBFABRIC=ON -DLIBFABRIC_ROOT=/apps/daint/UES/biddisco/gcc/10.2.0/libfabric -DOOMPH_LIBFABRIC_PROVIDER=gni -DMPIEXEC_EXECUTABLE=/usr/bin/srun /apps/daint/UES/biddisco/src/ghex/extern/oomph/")
    add_subdirectory(libfabric)
endif()
