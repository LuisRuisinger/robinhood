cmake_minimum_required(VERSION 3.16)

project(robinhood
        VERSION 0.1.0
        LANGUAGES C CXX
)

# ================================================================================================
# Library
# ================================================================================================

add_library(robinhood INTERFACE)
add_library(lsr::robinhood ALIAS robinhood)

include(GNUInstallDirs)

target_sources(robinhood
        INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/robinhood/robinhood.h>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/robinhood/robinhood_map.hpp>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/robinhood/robinhood_set.hpp>
)

target_include_directories(robinhood
        INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(robinhood INTERFACE cxx_std_17)

# ================================================================================================
# Options
# ================================================================================================

option(ROBINHOOD_BUILD_TESTS "Build robinhood tests" ${PROJECT_IS_TOP_LEVEL})
option(ROBINHOOD_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
option(ROBINHOOD_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# ================================================================================================
# clang-tidy
# ================================================================================================

if(ROBINHOOD_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif()

# ================================================================================================
# Sanitizers
# ================================================================================================

if(ROBINHOOD_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(robinhood
                INTERFACE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
        )

        target_link_options(robinhood
                INTERFACE
                -fsanitize=address,undefined
        )
    else()
        message(WARNING "ROBINHOOD_ENABLE_SANITIZERS is only configured for Clang/GCC")
    endif()
endif()

# ================================================================================================
# Tests
# ================================================================================================

if(ROBINHOOD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ================================================================================================
# Install / package config
# ================================================================================================

include(CMakePackageConfigHelpers)

install(TARGETS robinhood
        EXPORT robinhood_targets
)

install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT robinhood_targets
        FILE robinhood_targets.cmake
        NAMESPACE lsr::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/robinhood
)

configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/robinhoodConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/robinhoodConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/robinhood
)

write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/robinhoodConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/robinhoodConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/robinhoodConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/robinhood
)
