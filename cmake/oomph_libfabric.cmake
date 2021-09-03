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
    set_property(TARGET oomph_libfabric PROPERTY CXX_STANDARD 17)
    install(TARGETS oomph_libfabric
        EXPORT oomph-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    # ---------------------------------------------------------------------
    # Function to create a config define with a given 'namespace'
    # This is useful for writing out a config file for a subdir
    # of a project so that cmake option changes only trigger rebuilds
    # for the subset of the project using that #include
    # ---------------------------------------------------------------------
    function(oomph_libfabric_add_config_cond_define definition)
      # if(ARGN) ignores an argument "0"
      set(Args ${ARGN})
      list(LENGTH Args ArgsLen)
      if(ArgsLen GREATER 0)
        set_property(GLOBAL APPEND PROPERTY OOMPH_LIBFABRIC_CONFIG_COND_DEFINITIONS "${definition} ${ARGN}")
      else()
        set_property(GLOBAL APPEND PROPERTY OOMPH_LIBFABRIC_CONFIG_COND_DEFINITIONS "${definition}")
      endif()

    endfunction()

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

      if (${OPTION_NAMESPACE} STREQUAL "default")
        get_property(DEFINITIONS_VAR GLOBAL PROPERTY OOMPH_LIBFABRIC_CONFIG_DEFINITIONS)
        get_property(COND_DEFINITIONS_VAR GLOBAL PROPERTY OOMPH_LIBFABRIC_CONFIG_COND_DEFINITIONS)
      else()
        get_property(DEFINITIONS_VAR GLOBAL PROPERTY
          OOMPH_LIBFABRIC_CONFIG_DEFINITIONS_${OPTION_NAMESPACE})
      endif()

      if(DEFINED DEFINITIONS_VAR)
        list(SORT DEFINITIONS_VAR)
        list(REMOVE_DUPLICATES DEFINITIONS_VAR)
      endif()

      set(hpx_config_defines "\n")
      foreach(def ${DEFINITIONS_VAR})
        # C++17 specific variable
        string(FIND ${def} "HAVE_CXX17" _pos)
        if(NOT ${_pos} EQUAL -1)
          set(hpx_config_defines
             "${hpx_config_defines}#if __cplusplus >= 201500\n#define ${def}\n#endif\n")
        else()
          # C++14 specific variable
          string(FIND ${def} "HAVE_CXX14" _pos)
          if(NOT ${_pos} EQUAL -1)
            set(hpx_config_defines
               "${hpx_config_defines}#if __cplusplus >= 201402\n#define ${def}\n#endif\n")
          else()
            set(hpx_config_defines "${hpx_config_defines}#define ${def}\n")
          endif()
        endif()
      endforeach()

      if(DEFINED COND_DEFINITIONS_VAR)
        list(SORT COND_DEFINITIONS_VAR)
        list(REMOVE_DUPLICATES COND_DEFINITIONS_VAR)
        set(hpx_config_defines "${hpx_config_defines}\n")
      endif()
      foreach(def ${COND_DEFINITIONS_VAR})
        string(FIND ${def} " " _pos)
        if(NOT ${_pos} EQUAL -1)
          string(SUBSTRING ${def} 0 ${_pos} defname)
        else()
          set(defname ${def})
          string(STRIP ${defname} defname)
        endif()
        string(FIND ${def} "HAVE_CXX17" _pos)
        if(NOT ${_pos} EQUAL -1)
          set(hpx_config_defines
             "${hpx_config_defines}#if __cplusplus >= 201500 && !defined(${defname})\n#define ${def}\n#endif\n")
        else()
          # C++14 specific variable
          string(FIND ${def} "HAVE_CXX14" _pos)
          if(NOT ${_pos} EQUAL -1)
            set(hpx_config_defines
               "${hpx_config_defines}#if __cplusplus >= 201402 && !defined(${defname})\n#define ${def}\n#endif\n")
          else()
            set(hpx_config_defines
              "${hpx_config_defines}#if !defined(${defname})\n#define ${def}\n#endif\n")
          endif()
        endif()
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
            ${hpx_config_defines}
            "\n#endif\n"
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

    # ---------------------------------------------------------------------
    # Categories that options might be assigned to
    # ---------------------------------------------------------------------
    set(OOMPH_LIBFABRIC_OPTION_CATEGORIES
      "Generic"
      "Build Targets"
      "Transport"
      "Profiling"
      "Debugging"
    )

    # ---------------------------------------------------------------------
    # Function to create an option with a category type
    # ---------------------------------------------------------------------
    function(oomph_libfabric_option option type description default)
      set(options ADVANCED)
      set(one_value_args CATEGORY)
      set(multi_value_args STRINGS)
      cmake_parse_arguments(OOMPH_LIBFABRIC_OPTION "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

      if(NOT DEFINED ${option})
        set(${option} ${default} CACHE ${type} "${description}" FORCE)
        if(OOMPH_LIBFABRIC_OPTION_ADVANCED)
          mark_as_advanced(${option})
        endif()
      else()
        # make sure that dependent projects can overwrite any of the GHEX options
        unset(${option} PARENT_SCOPE)

        get_property(_option_is_cache_property CACHE "${option}" PROPERTY TYPE SET)
        if(NOT _option_is_cache_property)
          set(${option} ${default} CACHE ${type} "${description}" FORCE)
          if(OOMPH_LIBFABRIC_OPTION_ADVANCED)
            mark_as_advanced(${option})
          endif()
        else()
          set_property(CACHE "${option}" PROPERTY HELPSTRING "${description}")
          set_property(CACHE "${option}" PROPERTY TYPE "${type}")
        endif()
      endif()

      if(OOMPH_LIBFABRIC_OPTION_STRINGS)
        if("${type}" STREQUAL "STRING")
          set_property(CACHE "${option}" PROPERTY STRINGS "${OOMPH_LIBFABRIC_OPTION_STRINGS}")
        else()
          message(FATAL_ERROR "oomph_libfabric_option(): STRINGS can only be used if type is STRING !")
        endif()
      endif()

      set(_category "Generic")
      if(OOMPH_LIBFABRIC_OPTION_CATEGORY)
        set(_category "${OOMPH_LIBFABRIC_OPTION_CATEGORY}")
      endif()
      set(${option}Category ${_category} CACHE INTERNAL "")
    endfunction()


    #------------------------------------------------------------------------------
    # Logging
    #------------------------------------------------------------------------------
    oomph_libfabric_option(OOMPH_LIBFABRIC_WITH_LOGGING BOOL
      "Enable logging in the libfabric ParcelPort (default: OFF - Warning - severely impacts usability when enabled)"
      OFF CATEGORY "libfabric" ADVANCED)

    if (OOMPH_LIBFABRIC_WITH_LOGGING)
      oomph_libfabric_add_config_define_namespace(
          DEFINE    OOMPH_LIBFABRIC_HAVE_LOGGING
          NAMESPACE libfabric)
    endif()

    #------------------------------------------------------------------------------
    # Bootstrap options
    #------------------------------------------------------------------------------
    oomph_libfabric_option(OOMPH_LIBFABRIC_WITH_BOOTSTRAPPING BOOL
      "Configure the transport layerto enable bootstrap capabilities"
      ${PMIx_FOUND} CATEGORY "libfabric" ADVANCED)

    if (OOMPH_LIBFABRIC_WITH_BOOTSTRAPPING)
      message("Bootstrapping enabled")
      oomph_libfabric_add_config_define_namespace(
          DEFINE    OOMPH_LIBFABRIC_HAVE_BOOTSTRAPPING
          VALUE     std::true_type
          NAMESPACE libfabric)
    endif()

    #------------------------------------------------------------------------------
    # Hardware device selection
    #------------------------------------------------------------------------------
    oomph_libfabric_option(OOMPH_LIBFABRIC_PROVIDER STRING
      "The provider (verbs/gni/psm2/tcp/sockets)"
      "tcp" CATEGORY "libfabric" ADVANCED)

    oomph_libfabric_add_config_define_namespace(
        DEFINE OOMPH_LIBFABRIC_PROVIDER
        VALUE  "\"${OOMPH_LIBFABRIC_PROVIDER}\""
        NAMESPACE libfabric)

    if(OOMPH_LIBFABRIC_PROVIDER MATCHES "verbs")
        oomph_libfabric_add_config_define_namespace(
            DEFINE OOMPH_LIBFABRIC_VERBS
            NAMESPACE libfabric)
    elseif(OOMPH_LIBFABRIC_PROVIDER MATCHES "gni")
        oomph_libfabric_add_config_define_namespace(
            DEFINE OOMPH_LIBFABRIC_GNI
            NAMESPACE libfabric)
        # enable bootstrapping, add pmi library
        set(OOMPH_LIBFABRIC_WITH_BOOTSTRAPPING ON)
        set(_libfabric_libraries ${_libfabric_libraries} PMIx::libpmix)
    elseif(OOMPH_LIBFABRIC_PROVIDER MATCHES "tcp")
        oomph_libfabric_add_config_define_namespace(
            DEFINE OOMPH_LIBFABRIC_TCP
            NAMESPACE libfabric)
        # enable bootstrapping
        set(OOMPH_LIBFABRIC_WITH_BOOTSTRAPPING OFF)
    elseif(OOMPH_LIBFABRIC_PROVIDER MATCHES "sockets")
        oomph_libfabric_add_config_define_namespace(
            DEFINE OOMPH_LIBFABRIC_SOCKETS
            NAMESPACE libfabric)
        # enable bootstrapping
        set(OOMPH_LIBFABRIC_WITH_BOOTSTRAPPING ON)
    elseif(OOMPH_LIBFABRIC_PROVIDER MATCHES "psm2")
        oomph_libfabric_add_config_define_namespace(
            DEFINE OOMPH_LIBFABRIC_PSM2
            NAMESPACE libfabric)
    endif()

    #------------------------------------------------------------------------------
    # Domain and endpoint are fixed, but used to be options
    # leaving options in case they are needed again to support other platforms
    #------------------------------------------------------------------------------
    oomph_libfabric_option(OOMPH_LIBFABRIC_DOMAIN STRING
      "The libfabric domain (leave blank for default"
      "" CATEGORY "libfabric" ADVANCED)

    oomph_libfabric_add_config_define_namespace(
        DEFINE OOMPH_LIBFABRIC_DOMAIN
        VALUE  "\"${OOMPH_LIBFABRIC_DOMAIN}\""
        NAMESPACE libfabric)

    oomph_libfabric_option(OOMPH_LIBFABRIC_ENDPOINT STRING
      "The libfabric endpoint type (leave blank for default"
      "rdm" CATEGORY "libfabric" ADVANCED)

    oomph_libfabric_add_config_define_namespace(
        DEFINE OOMPH_LIBFABRIC_ENDPOINT
        VALUE  "\"${OOMPH_LIBFABRIC_ENDPOINT}\""
        NAMESPACE libfabric)

    #------------------------------------------------------------------------------
    # Performance counters
    #------------------------------------------------------------------------------
    oomph_libfabric_option(OOMPH_LIBFABRIC_WITH_PERFORMANCE_COUNTERS BOOL
      "Enable libfabric parcelport performance counters (default: OFF)"
      OFF CATEGORY "libfabric" ADVANCED)

    if (OOMPH_LIBFABRIC_WITH_PERFORMANCE_COUNTERS)
      oomph_libfabric_add_config_define_namespace(
          DEFINE    OOMPH_LIBFABRIC_HAVE_PERFORMANCE_COUNTERS
          NAMESPACE libfabric)
    endif()

    #------------------------------------------------------------------------------
    # Memory chunk/reservation options
    #------------------------------------------------------------------------------
    oomph_libfabric_option(OOMPH_LIBFABRIC_MEMORY_CHUNK_SIZE STRING
      "Number of bytes a default chunk in the memory pool can hold (default: 4K)"
      "4096" CATEGORY "libfabric" ADVANCED)

    oomph_libfabric_option(OOMPH_LIBFABRIC_64K_PAGES STRING
      "Number of 64K pages we reserve for default message buffers (default: 10)"
      "10" CATEGORY "libfabric" ADVANCED)

    oomph_libfabric_option(OOMPH_LIBFABRIC_MEMORY_COPY_THRESHOLD STRING
      "Cutoff size over which data is never copied into existing buffers (default: 4K)"
      "4096" CATEGORY "libfabric" ADVANCED)

    oomph_libfabric_add_config_define_namespace(
        DEFINE    OOMPH_LIBFABRIC_MEMORY_CHUNK_SIZE
        VALUE     ${OOMPH_LIBFABRIC_MEMORY_CHUNK_SIZE}
        NAMESPACE libfabric)

    # define the message header size to be equal to the chunk size
    oomph_libfabric_add_config_define_namespace(
        DEFINE    OOMPH_LIBFABRIC_MESSAGE_HEADER_SIZE
        VALUE     ${OOMPH_LIBFABRIC_MEMORY_CHUNK_SIZE}
        NAMESPACE libfabric)

    oomph_libfabric_add_config_define_namespace(
        DEFINE    OOMPH_LIBFABRIC_64K_PAGES
        VALUE     ${OOMPH_LIBFABRIC_64K_PAGES}
        NAMESPACE libfabric)

    oomph_libfabric_add_config_define_namespace(
        DEFINE    OOMPH_LIBFABRIC_MEMORY_COPY_THRESHOLD
        VALUE     ${OOMPH_LIBFABRIC_MEMORY_COPY_THRESHOLD}
        NAMESPACE libfabric)

    #------------------------------------------------------------------------------
    # Preposting options
    #------------------------------------------------------------------------------
    oomph_libfabric_option(OOMPH_LIBFABRIC_MAX_UNEXPECTED STRING
      "The number of pre-posted unexpected receive buffers (default: 32)"
      "32" CATEGORY "libfabric" ADVANCED)

    oomph_libfabric_add_config_define_namespace(
        DEFINE    OOMPH_LIBFABRIC_MAX_UNEXPECTED
        VALUE     ${OOMPH_LIBFABRIC_MAX_UNEXPECTED}
        NAMESPACE libfabric)

    oomph_libfabric_option(OOMPH_LIBFABRIC_MAX_EXPECTED STRING
      "The number of pre-posted receive buffers (default: 128)"
      "128" CATEGORY "libfabric" ADVANCED)

    oomph_libfabric_add_config_define_namespace(
        DEFINE    OOMPH_LIBFABRIC_MAX_EXPECTED
        VALUE     ${OOMPH_LIBFABRIC_MAX_EXPECTED}
        NAMESPACE libfabric)

    #------------------------------------------------------------------------------
    # Throttling options
    #------------------------------------------------------------------------------
    oomph_libfabric_option(OOMPH_LIBFABRIC_MAX_SENDS STRING
      "Threshold of active sends at which throttling is enabled (default: 16)"
      "16" CATEGORY "libfabric" ADVANCED)

    oomph_libfabric_add_config_define_namespace(
        DEFINE    OOMPH_LIBFABRIC_MAX_SENDS
        VALUE     ${OOMPH_LIBFABRIC_MAX_SENDS}
        NAMESPACE libfabric)

    #------------------------------------------------------------------------------
    # Write options to file in build dir
    #------------------------------------------------------------------------------
    oomph_libfabric_write_config_defines_file(
        NAMESPACE libfabric
        FILENAME  "${PROJECT_BINARY_DIR}/oomph_libfabric_defines.hpp"
    )
    target_include_directories(oomph_libfabric PRIVATE "${PROJECT_BINARY_DIR}")
endif()



