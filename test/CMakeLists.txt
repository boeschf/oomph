include(gtest)

add_subdirectory(mpi_runner)

# ---------------------------------------------------------------------
# compile tests
# ---------------------------------------------------------------------

# list of tests to be executed
set(parallel_tests test_context test_send_recv test_send_multi test_cancel test_locality
    test_channel)
if (OOMPH_ENABLE_BARRIER)
    list(APPEND parallel_tests test_barrier)
endif()

# creates an object library (i.e. *.o file)
function(compile_test t_)
    set(t ${t_}_obj)
    add_library(${t} OBJECT ${t_}.cpp)
    oomph_target_compile_options(${t})
    target_link_libraries(${t} PRIVATE GTest::gtest)
    target_link_libraries(${t} PUBLIC oomph)
endfunction()

# compile an object library for each test
# tests will be compiled only once and then linked against all enabled oomph backends
foreach(t ${parallel_tests})
    compile_test(${t})
endforeach()

# ---------------------------------------------------------------------
# link and register tests
# ---------------------------------------------------------------------

# creates an executable by linking to object file and to selected oomph backend
function(reg_parallel_test t_ lib n)
    set(t ${t_}_${lib})
    add_executable(${t} $<TARGET_OBJECTS:${t_}_obj>)
    oomph_target_compile_options(${t})
    target_link_libraries(${t} PRIVATE gtest_main_mpi)
    target_link_libraries(${t} PRIVATE oomph_${lib})
    add_test(
        NAME ${t}
        COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${n} ${MPIEXEC_PREFLAGS}
            $<TARGET_FILE:${t}> ${MPIEXEC_POSTFLAGS})
endfunction()

if (OOMPH_WITH_MPI)
    foreach(t ${parallel_tests})
        reg_parallel_test(${t} mpi 4)
    endforeach()
endif()

if (OOMPH_WITH_UCX)
    foreach(t ${parallel_tests})
        reg_parallel_test(${t} ucx 4)
    endforeach()
endif()

if (OOMPH_WITH_LIBFABRIC)
    foreach(t ${parallel_tests})
        reg_parallel_test(${t} libfabric 4)
    endforeach()
endif()
