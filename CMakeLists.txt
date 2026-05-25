cmake_minimum_required(VERSION 3.14)
project(Rho VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Locate xic (Xi framework)
# ---------------------------------------------------------------------------
if(NOT TARGET Xi AND NOT TARGET Xi::Xi)
  # Check for a local xic checkout first (../xic or -DXI_DIR=...)
  set(_XI_DEFAULT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../xic")
  if(NOT DEFINED XI_DIR)
    set(XI_DIR "${_XI_DEFAULT_PATH}")
  endif()

  if(EXISTS "${XI_DIR}/CMakeLists.txt")
    message(STATUS "Xi: using local checkout at ${XI_DIR}")
    set(XI_BUILD_AS_OBJECTS ON  CACHE BOOL "" FORCE)
    set(XI_BUILD_GRAPHICS   OFF CACHE BOOL "" FORCE)
    set(XI_BUILD_HARDWARE   OFF CACHE BOOL "" FORCE)
    set(XI_BUILD_TLS        OFF CACHE BOOL "" FORCE)
    set(XI_BUILD_DEFLATE    OFF CACHE BOOL "" FORCE)
    set(XI_BUILD_LZ4        OFF CACHE BOOL "" FORCE)
    set(XI_BUILD_ZSTD       OFF CACHE BOOL "" FORCE)
    add_subdirectory("${XI_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/xic")
  else()
    message(STATUS "Xi: not found locally, fetching from GitHub (xipid/xic)...")
    include(FetchContent)
    FetchContent_Declare(
      xic
      GIT_REPOSITORY https://github.com/xipid/xic.git
      GIT_TAG        main
      GIT_SHALLOW    TRUE
    )
    set(XI_BUILD_AS_OBJECTS ON  CACHE BOOL "" FORCE)
    set(XI_BUILD_GRAPHICS   OFF CACHE BOOL "" FORCE)
    set(XI_BUILD_HARDWARE   OFF CACHE BOOL "" FORCE)
    set(XI_BUILD_TLS        OFF CACHE BOOL "" FORCE)
    set(XI_BUILD_DEFLATE    OFF CACHE BOOL "" FORCE)
    set(XI_BUILD_LZ4        OFF CACHE BOOL "" FORCE)
    set(XI_BUILD_ZSTD       OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(xic)
  endif()
endif()

# ---------------------------------------------------------------------------
# Rho header-only library interface target
# ---------------------------------------------------------------------------
add_library(Rho INTERFACE)
target_include_directories(Rho INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(Rho INTERFACE Xi)



# ---------------------------------------------------------------------------
# Rho Daemon Executable
# ---------------------------------------------------------------------------
add_executable(rhod src/rhod/main.cpp)
target_link_libraries(rhod PRIVATE Rho Xi)

# ---------------------------------------------------------------------------
# Tests — one executable per file in tests/
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)

file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")

foreach(TEST_SRC ${TEST_SOURCES})
  get_filename_component(TEST_NAME "${TEST_SRC}" NAME_WE)

  add_executable("${TEST_NAME}" "${TEST_SRC}")

  target_link_libraries("${TEST_NAME}" PRIVATE Rho Xi Threads::Threads)

  set_target_properties("${TEST_NAME}" PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
  )
endforeach()
