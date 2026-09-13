# =============================================================================
# XSigma Intel TBB -
# Multithreading Backend (Threading Building Blocks) Parallel Task Scheduling

# This module configures Intel TBB as the parallel task scheduling backend. It is activated when
# PARALLEL_ENABLE_TBB=ON, which is set by parallel_backend.cmake when PARALLEL_BACKEND=tbb is
# selected.
#
# Provides automatic fallback support: 1. First attempts to find system-installed TBB (vcpkg, apt,
# homebrew, Intel oneAPI) 2. If not found, automatically downloads and builds TBB from source 3.
# Creates the Tbb::tbb interface target for linking

cmake_minimum_required(VERSION 3.16)

include_guard(GLOBAL)

# Gate: only proceed if TBB parallel backend is requested PARALLEL_ENABLE_TBB is set by
# parallel_backend.cmake when PARALLEL_BACKEND=tbb
if(NOT PARALLEL_ENABLE_TBB)
  message(STATUS "Intel TBB parallel backend is disabled (PARALLEL_ENABLE_TBB=OFF)")
  return()
endif()

message(STATUS "Configuring Intel TBB multithreading support...")

# =============================================================================
# Configuration
# Options

# Option to force building TBB from source (useful for testing)
option(PROJECT_TBB_FORCE_BUILD_FROM_SOURCE
       "Force building TBB from source instead of using system TBB" OFF
)
mark_as_advanced(PROJECT_TBB_FORCE_BUILD_FROM_SOURCE)

# TBB version to use when building from source
set(PROJECT_TBB_VERSION "v2021.13.0" CACHE STRING "TBB version to build from source")
mark_as_advanced(PROJECT_TBB_VERSION)

# TBB repository URL
set(PROJECT_TBB_REPOSITORY "https://github.com/oneapi-src/oneTBB.git" CACHE STRING
                                                                            "TBB repository URL"
)
mark_as_advanced(PROJECT_TBB_REPOSITORY)

# =============================================================================
# Step 1: Try to find
# system-installed TBB

set(TBB_FOUND FALSE)
set(TBB_FROM_SOURCE FALSE)

if(NOT PROJECT_TBB_FORCE_BUILD_FROM_SOURCE)
  message(STATUS "Searching for system-installed Intel TBB...")

  find_package(TBB QUIET)

  if(TBB_FOUND AND TARGET TBB::tbb)
    message(STATUS "✅ Found system-installed Intel TBB")
    message(STATUS "   TBB Include Dir: ${TBB_INCLUDE_DIR}")
    if(TBB_LIBRARY_RELEASE)
      message(STATUS "   TBB Library: ${TBB_LIBRARY_RELEASE}")
    endif()
    if(TARGET TBB::tbb)
      message(STATUS "   TBB::tbb target available")
    endif()

    if(NOT TARGET Tbb::tbb)
      add_library(Tbb::tbb INTERFACE IMPORTED)
      target_link_libraries(Tbb::tbb INTERFACE TBB::tbb)
    endif()

    message(STATUS "Intel TBB multithreading configuration complete")
    return()
  else()
    message(STATUS "❌ System-installed Intel TBB not found")

    # ============================================================================= Windows + Clang:
    # Require system-installed TBB
    #
    # Building TBB from source on Windows with Clang has compatibility issues: - Clang targeting
    # MSVC ABI doesn't support TBB's -fPIC flags - DLL export/import symbol visibility issues -
    # Linux-specific linker flags incompatible with lld-link Solution: Require users to install TBB
    # via package manager

    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      message(
        FATAL_ERROR
          "\n"
          "================================================================================\n"
          "ERROR: Intel TBB not found - System installation required on Windows with Clang\n"
          "================================================================================\n"
          "\n"
          "Building TBB from source is not supported on Windows with Clang due to\n"
          "compiler compatibility issues. You must install TBB using a package manager.\n"
          "\n"
          "RECOMMENDED INSTALLATION METHODS:\n"
          "\n"
          "1. CI / official binaries (recommended):\n"
          "   .github/workflows/install/install-deps-windows.ps1 -WithTbb\n"
          "   (downloads oneTBB Windows binaries and sets TBB_ROOT)\n"
          "\n"
          "2. Using vcpkg:\n"
          "   vcpkg install tbb:x64-windows\n"
          "   -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake\n"
          "\n"
          "3. Manual installation:\n"
          "   - Download from: https://github.com/uxlfoundation/oneTBB/releases\n"
          "   - Extract and set TBB_ROOT / TBB_DIR (lib/cmake/tbb)\n"
          "\n"
          "4. Alternative: Use Visual Studio (MSVC) compiler instead of Clang:\n"
          "   python setup.py config.ninja.vs22.test.tbb\n"
          "\n"
          "For more information, see: https://github.com/uxlfoundation/oneTBB\n"
          "================================================================================\n"
      )
    endif()

    message(STATUS "   Will build TBB from source as fallback")
  endif()
endif()

# =============================================================================
# Step 2: Build TBB
# from source using FetchContent

message(STATUS "Building Intel TBB from source...")
message(STATUS "   Repository: ${PROJECT_TBB_REPOSITORY}")
message(STATUS "   Version: ${PROJECT_TBB_VERSION}")

include(FetchContent)

FetchContent_Declare(
  oneTBB
  GIT_REPOSITORY ${PROJECT_TBB_REPOSITORY}
  GIT_TAG ${PROJECT_TBB_VERSION}
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
)

set(TBB_TEST OFF CACHE BOOL "Disable TBB tests" FORCE)
set(TBB_EXAMPLES OFF CACHE BOOL "Disable TBB examples" FORCE)
set(TBB_STRICT OFF CACHE BOOL "Disable strict mode for compatibility" FORCE)

if(POLICY CMP0126)
  cmake_policy(SET CMP0126 NEW)
endif()

if(WIN32)
  set(TBB_WINDOWS_DRIVER OFF CACHE BOOL "Build TBB for Windows kernel mode" FORCE)
  set(CMAKE_POSITION_INDEPENDENT_CODE OFF CACHE BOOL "Disable PIC on Windows" FORCE)
  set(TBB_ENABLE_PIC OFF CACHE BOOL "Disable TBB PIC on Windows" FORCE)
  set(CMAKE_CXX_COMPILE_OPTIONS_PIC "" CACHE STRING "Clear PIC options" FORCE)
  set(CMAKE_C_COMPILE_OPTIONS_PIC "" CACHE STRING "Clear PIC options" FORCE)
endif()

message(STATUS "Downloading Intel TBB source code...")

if(WIN32)
  set(_SAVED_CMAKE_POSITION_INDEPENDENT_CODE ${CMAKE_POSITION_INDEPENDENT_CODE})
  set(_SAVED_CMAKE_CXX_COMPILE_OPTIONS_PIC "${CMAKE_CXX_COMPILE_OPTIONS_PIC}")
  set(_SAVED_CMAKE_C_COMPILE_OPTIONS_PIC "${CMAKE_C_COMPILE_OPTIONS_PIC}")

  set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
  set(CMAKE_CXX_COMPILE_OPTIONS_PIC "")
  set(CMAKE_C_COMPILE_OPTIONS_PIC "")
endif()

FetchContent_MakeAvailable(oneTBB)

if(WIN32)
  set(CMAKE_POSITION_INDEPENDENT_CODE ${_SAVED_CMAKE_POSITION_INDEPENDENT_CODE})
  set(CMAKE_CXX_COMPILE_OPTIONS_PIC "${_SAVED_CMAKE_CXX_COMPILE_OPTIONS_PIC}")
  set(CMAKE_C_COMPILE_OPTIONS_PIC "${_SAVED_CMAKE_C_COMPILE_OPTIONS_PIC}")
endif()

FetchContent_GetProperties(oneTBB)
if(NOT onetbb_POPULATED)
  message(FATAL_ERROR "Failed to download Intel TBB from ${PROJECT_TBB_REPOSITORY}")
endif()

message(STATUS "✅ Successfully downloaded Intel TBB source")
message(STATUS "   Source directory: ${onetbb_SOURCE_DIR}")
message(STATUS "   Binary directory: ${onetbb_BINARY_DIR}")

# =============================================================================
# Step 3: Verify
# TBB::tbb target was created

if(NOT TARGET TBB::tbb)
  message(FATAL_ERROR "TBB::tbb target was not created after building from source")
endif()

set(TBB_FROM_SOURCE TRUE)
set(TBB_FOUND TRUE)

message(STATUS "✅ Successfully built Intel TBB from source")
message(STATUS "   TBB::tbb target available")

# =============================================================================
# Step 4: Create
# Tbb::tbb interface target

if(NOT TARGET Tbb::tbb)
  add_library(Tbb::tbb INTERFACE IMPORTED)
  target_link_libraries(Tbb::tbb INTERFACE TBB::tbb)
endif()

# =============================================================================
# Step 5: Configure
# output directories for the tbb target

if(TBB_FROM_SOURCE AND TARGET tbb)
  foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set_target_properties(
      tbb
      PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config_upper}
                 "${PROJECT_BINARY_DIR}/bin/${config_upper}"
                 ARCHIVE_OUTPUT_DIRECTORY_${config_upper}
                 "${PROJECT_BINARY_DIR}/lib/${config_upper}"
                 LIBRARY_OUTPUT_DIRECTORY_${config_upper}
                 "${PROJECT_BINARY_DIR}/lib/${config_upper}"
    )
  endforeach()

  set_target_properties(
    tbb
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
               ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
               LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
               FOLDER "ThirdParty/tbb"
  )
  message(STATUS "Configured output directories for TBB target 'tbb'")
endif()

# =============================================================================
# Step 6: Export TBB
# information for other modules (e.g. tbb_memory.cmake)

set(TBB_FOUND TRUE CACHE BOOL "TBB was found or built successfully" FORCE)
set(TBB_FROM_SOURCE ${TBB_FROM_SOURCE} CACHE BOOL "TBB was built from source" FORCE)

if(TBB_FROM_SOURCE)
  set(TBB_SOURCE_DIR ${onetbb_SOURCE_DIR} CACHE PATH "TBB source directory" FORCE)
  set(TBB_BINARY_DIR ${onetbb_BINARY_DIR} CACHE PATH "TBB binary directory" FORCE)
endif()

message(STATUS "Intel TBB multithreading configuration complete")
