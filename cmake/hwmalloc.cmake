if(NOT _hwmalloc_already_fetched)
    find_package(HWMALLOC QUIET)
endif()

if(NOT HWMALLOC_FOUND)
    if(EXISTS ${PROJECT_SOURCE_DIR}/extern/hwmalloc)
        set(FETCHCONTENT_SOURCE_DIR_HWMALLOC ${PROJECT_SOURCE_DIR}/extern/hwmalloc)
    endif()

    set(_hwmalloc_repository "git@github.com:boeschf/hwmalloc.git")
    #set(_hwmalloc_tag        "master")
    set(_hwmalloc_tag        "oomph_integration")
    if(NOT _hwmalloc_already_fetched)
        message(STATUS "Fetching HWMALLOC ${_hwmalloc_tag} from ${_hwmalloc_repository}")
    endif()
    include(FetchContent)
    FetchContent_Declare(
        hwmalloc
        GIT_REPOSITORY ${_hwmalloc_repository}
        GIT_TAG        ${_hwmalloc_tag}
    )
    FetchContent_MakeAvailable(hwmalloc)
    set(_hwmalloc_already_fetched ON CACHE INTERNAL "")
endif()


