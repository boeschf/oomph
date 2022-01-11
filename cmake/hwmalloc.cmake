set(OOMPH_USE_GPU "OFF" CACHE BOOL "Use GPU")
set(OOMPH_GPU_TYPE "AUTO" CACHE
    STRING "Choose the GPU type: AMD | NVIDIA | AUTO (environment-based) | EMULATE")
set_property(CACHE OOMPH_GPU_TYPE PROPERTY STRINGS "AMD" "NVIDIA" "AUTO" "EMULATE")

if (OOMPH_USE_GPU)
    if (OOMPH_GPU_TYPE STREQUAL "AUTO")
        find_package(hip)
        if (hip_FOUND)
            set(oomph_gpu_mode "AMD")
        else() # assume cuda elsewhere; TO DO: might be refined
            set(oomph_gpu_mode "NVIDIA")
        endif()
    elseif (OOMPH_GPU_TYPE STREQUAL "AMD")
        set(oomph_gpu_mode "AMD")
    elseif (OOMPH_GPU_TYPE STREQUAL "NVIDIA")
        set(oomph_gpu_mode "NVIDIA")
    else()
        set(oomph_gpu_mode "EMULATE")
    endif()
endif()

set(_hwmalloc_repository "https://github.com/ghex-org/hwmalloc.git")
set(_hwmalloc_tag        "master")
if(NOT _hwmalloc_already_fetched)
    message(STATUS "Fetching HWMALLOC ${_hwmalloc_tag} from ${_hwmalloc_repository}")
endif()

if(EXISTS ${PROJECT_SOURCE_DIR}/extern/hwmalloc)
    set(FETCHCONTENT_SOURCE_DIR_HWMALLOC ${PROJECT_SOURCE_DIR}/extern/hwmalloc)
endif()
include(FetchContent)
FetchContent_Declare(
    hwmalloc
    GIT_REPOSITORY ${_hwmalloc_repository}
    GIT_TAG        ${_hwmalloc_tag}
)

if (OOMPH_USE_GPU)
    set(HWMALLOC_ENABLE_DEVICE ON CACHE INTERNAL "")  # Forces the value
    if (oomph_gpu_mode STREQUAL "AMD")
        set(HWMALLOC_DEVICE_RUNTIME "hip" CACHE INTERNAL "")  # Forces the value
    elseif (oomph_gpu_mode STREQUAL "NVIDIA")
        set(HWMALLOC_DEVICE_RUNTIME "cuda" CACHE INTERNAL "")  # Forces the value
    else()
        set(HWMALLOC_DEVICE_RUNTIME "emulate" CACHE INTERNAL "")  # Forces the value
    endif()
else()
    set(HWMALLOC_ENABLE_DEVICE OFF CACHE INTERNAL "")  # Forces the value
endif()

FetchContent_MakeAvailable(hwmalloc)
set(_hwmalloc_already_fetched ON CACHE INTERNAL "")


