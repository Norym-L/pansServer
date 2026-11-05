cmake_minimum_required(VERSION 3.25)

if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_C_COMPILER "/usr/bin/gcc" CACHE FILEPATH "C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "/usr/bin/g++" CACHE FILEPATH "C++ compiler" FORCE)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

project(pans VERSION 0.1.0 LANGUAGES CXX C)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(PANS_BUILD_EXAMPLES "Build examples" ON)
option(PANS_BUILD_TESTS "Build tests" ON)
option(PANS_BUILD_TOOLS "Build tools" ON)

if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif ()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

cmake_host_system_information(RESULT CPU_NUMS QUERY NUMBER_OF_LOGICAL_CORES)
message(STATUS "CPU NUMS on current server is: ${CPU_NUMS}")
math(EXPR JOBS "${CPU_NUMS} + ${CPU_NUMS} / 2")
set(CMAKE_BUILD_PARALLEL_LEVEL ${JOBS} CACHE STRING "并行编译线程数")
message(STATUS "Compiler default work jobs: ${CMAKE_BUILD_PARALLEL_LEVEL}")


include(cmake/pansOptions.cmake)
include(cmake/pansDependencies.cmake)
include(cmake/functions.cmake)

add_subdirectory(cmake/boost-context)
add_subdirectory(pans)
if (PANS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
if (PANS_BUILD_TOOLS)
    add_subdirectory(tools)
endif ()

set(PANS_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/pans")

configure_package_config_file(
        cmake/pansConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/pansConfig.cmake"
        INSTALL_DESTINATION "${PANS_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/pansConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
)

install(
        FILES
        "${CMAKE_CURRENT_BINARY_DIR}/pansConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/pansConfigVersion.cmake"
        DESTINATION "${PANS_INSTALL_CMAKEDIR}"
)

install(
        EXPORT pansTargets
        FILE pansTargets.cmake
        NAMESPACE pans::
        DESTINATION "${PANS_INSTALL_CMAKEDIR}"
)
