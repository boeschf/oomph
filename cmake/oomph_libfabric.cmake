# set all libfabric related options and values

#------------------------------------------------------------------------------
# Enable libfabric support
#------------------------------------------------------------------------------
set(OOMPH_WITH_LIBFABRIC OFF CACHE BOOL "Build with LIBFABRIC backend")

if (OOMPH_WITH_LIBFABRIC)
    find_package(Libfabric REQUIRED)
    add_library(oomph_libfabric SHARED)
    add_library(oomph::libfabric ALIAS oomph_libfabric)
    oomph_shared_lib_options(oomph_libfabric)
    target_link_libraries(oomph_libfabric PUBLIC libfabric::libfabric)
    install(TARGETS oomph_libfabric
        EXPORT oomph-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    # ---------------------------------------------------------------------
    # Function to add config defines to a list that depends on a namespace variable
    # #defines that match the namespace can later be written out to a file
    # ---------------------------------------------------------------------
    function(oomph_libfabric_add_config_define_namespace)
      set(options)
      set(one_value_args DEFINE NAMESPACE)
      set(multi_value_args VALUE)
      cmake_parse_arguments(OPTION
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

      set(DEF_VAR OOMPH_LIBFABRIC_CONFIG_DEFINITIONS_${OPTION_NAMESPACE})

      # to avoid extra trailing spaces (no value), use an if check
      if(OPTION_VALUE)
        set_property(GLOBAL APPEND PROPERTY ${DEF_VAR} "${OPTION_DEFINE} ${OPTION_VALUE}")
      else()
        set_property(GLOBAL APPEND PROPERTY ${DEF_VAR} "${OPTION_DEFINE}")
      endif()

    endfunction()

    # ---------------------------------------------------------------------
    # Function to write out all the config defines for a given namespace
    # into a config file
    # ---------------------------------------------------------------------
    function(oomph_libfabric_write_config_defines_file)
      set(options)
      set(one_value_args TEMPLATE NAMESPACE FILENAME)
      set(multi_value_args)
      cmake_parse_arguments(OPTION
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

      get_property(DEFINITIONS_VAR GLOBAL PROPERTY
        OOMPH_LIBFABRIC_CONFIG_DEFINITIONS_${OPTION_NAMESPACE})

      if(DEFINED DEFINITIONS_VAR)
        list(SORT DEFINITIONS_VAR)
        list(REMOVE_DUPLICATES DEFINITIONS_VAR)
      endif()

      set(oomph_config_defines "\n")
      foreach(def ${DEFINITIONS_VAR})
        set(oomph_config_defines "${oomph_config_defines}#define ${def}\n")
      endforeach()

      # if the user has not specified a template, generate a proper header file
      if (NOT OPTION_TEMPLATE)
        string(TOUPPER ${OPTION_NAMESPACE} NAMESPACE_UPPER)
        set(PREAMBLE
          "\n"
          "// Do not edit this file! It has been generated by the cmake configuration step.\n"
          "\n"
          "#ifndef OOMPH_LIBFABRIC_CONFIG_${NAMESPACE_UPPER}_HPP\n"
          "#define OOMPH_LIBFABRIC_CONFIG_${NAMESPACE_UPPER}_HPP\n"
        )
        set(TEMP_FILENAME "${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${NAMESPACE_UPPER}")
        file(WRITE ${TEMP_FILENAME}
            ${PREAMBLE}
            ${oomph_config_defines}
            "#endif\n"
        )
        configure_file("${TEMP_FILENAME}" "${OPTION_FILENAME}" COPYONLY)
        file(REMOVE "${TEMP_FILENAME}")
      else()
        configure_file("${OPTION_TEMPLATE}"
                       "${OPTION_FILENAME}"
                       @ONLY)
      endif()
    endfunction()

    include(CMakeParseArguments)

    #------------------------------------------------------------------------------
    # Hardware device selection
    #------------------------------------------------------------------------------
    set(OOMPH_LIBFABRIC_PROVIDER "tcp" CACHE
        STRING "The provider (cxi(Cray Slingshot)/gni(Cray Gemini)/psm2(Intel Omni-Path)/sockets/tcp/verbs(Infiniband))")
    set_property(CACHE OOMPH_LIBFABRIC_PROVIDER PROPERTY STRINGS
        "cxi" "gni" "psm2" "tcp" "verbs")
    # formerly also supported "sockets", but now deprecated

    oomph_libfabric_add_config_define_namespace(
        DEFINE HAVE_LIBFABRIC_PROVIDER
        VALUE  "\"${OOMPH_LIBFABRIC_PROVIDER}\""
        NAMESPACE libfabric)

    if(OOMPH_LIBFABRIC_PROVIDER MATCHES "verbs")
        oomph_libfabric_add_config_define_namespace(
            DEFINE HAVE_LIBFABRIC_VERBS
            NAMESPACE libfabric)
    elseif(OOMPH_LIBFABRIC_PROVIDER MATCHES "gni")
        oomph_libfabric_add_config_define_namespace(
            DEFINE HAVE_LIBFABRIC_GNI
            NAMESPACE libfabric)
        # add pmi library
        set(_libfabric_libraries ${_libfabric_libraries} PMIx::libpmix)
    elseif(OOMPH_LIBFABRIC_PROVIDER MATCHES "cxi")
        oomph_libfabric_add_config_define_namespace(
            DEFINE HAVE_LIBFABRIC_CXI
            NAMESPACE libfabric)
    elseif(OOMPH_LIBFABRIC_PROVIDER MATCHES "tcp")
        oomph_libfabric_add_config_define_namespace(
            DEFINE HAVE_LIBFABRIC_TCP
            NAMESPACE libfabric)
    elseif(OOMPH_LIBFABRIC_PROVIDER MATCHES "sockets")
        message(WARNING "The Sockets provider is deprecated in favor of the tcp, udp, "
            "and utility providers")
        oomph_libfabric_add_config_define_namespace(
            DEFINE HAVE_LIBFABRIC_SOCKETS
            NAMESPACE libfabric)
    elseif(OOMPH_LIBFABRIC_PROVIDER MATCHES "psm2")
        oomph_libfabric_add_config_define_namespace(
            DEFINE HAVE_LIBFABRIC_PSM2
            NAMESPACE libfabric)
    endif()

    #------------------------------------------------------------------------------
    # Performance counters
    #------------------------------------------------------------------------------
    set(OOMPH_LIBFABRIC_WITH_PERFORMANCE_COUNTERS OFF BOOL
        STRING "Enable libfabric parcelport performance counters (default: OFF)")
    set_property(CACHE OOMPH_LIBFABRIC_PROVIDER PROPERTY STRINGS "tcp" "sockets" "psm2" "verbs" "gni")
    mark_as_advanced(OOMPH_LIBFABRIC_WITH_PERFORMANCE_COUNTERS)

    if (OOMPH_LIBFABRIC_WITH_PERFORMANCE_COUNTERS)
      oomph_libfabric_add_config_define_namespace(
          DEFINE    OOMPH_LIBFABRIC_HAVE_PERFORMANCE_COUNTERS
          NAMESPACE libfabric)
    endif()

    #------------------------------------------------------------------------------
    # used by template expansion for location of print.hpp
    #------------------------------------------------------------------------------
    set(OOMPH_SRC_LIBFABRIC_DIR "${PROJECT_SOURCE_DIR}/src/libfabric")

    #------------------------------------------------------------------------------
    # Write options to file in build dir
    #------------------------------------------------------------------------------
    oomph_libfabric_write_config_defines_file(
        NAMESPACE libfabric
        FILENAME  "${PROJECT_BINARY_DIR}/src/libfabric/oomph_libfabric_defines.hpp"
        TEMPLATE  "${OOMPH_SRC_LIBFABRIC_DIR}/libfabric_defines_template.hpp"
    )
    target_include_directories(oomph_libfabric PRIVATE "${PROJECT_BINARY_DIR}/src/libfabric")
endif()



