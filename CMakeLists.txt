cmake_minimum_required(VERSION 3.25)
project(lockstep_tactics LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Strict warnings for our own targets only; fetched dependencies keep theirs.
add_library(lockstep_warnings INTERFACE)
if(MSVC)
  target_compile_options(lockstep_warnings INTERFACE /W4 /WX /permissive- /utf-8)
else()
  target_compile_options(lockstep_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

# MinGW executables otherwise need libstdc++-6.dll on PATH at run time, and a
# shell opened before the toolchain was installed does not have it.
if(MINGW)
  add_link_options(-static)
endif()

add_library(lockstep_core STATIC core/src/world.cpp core/src/replay.cpp)
target_include_directories(lockstep_core PUBLIC core/include)
target_link_libraries(lockstep_core PRIVATE lockstep_warnings)

# Deliberately broken builds, to show the determinism checks catch what they
# claim to. Never set for a real build. See docs/notes/06.
set(LOCKSTEP_RISKY "" CACHE STRING "unordered|float: build a deliberately non-deterministic variant")
if(LOCKSTEP_RISKY STREQUAL "unordered")
  target_compile_definitions(lockstep_core PRIVATE LOCKSTEP_RISKY_UNORDERED)
elseif(LOCKSTEP_RISKY STREQUAL "float")
  target_compile_definitions(lockstep_core PRIVATE LOCKSTEP_RISKY_FLOAT)
elseif(NOT LOCKSTEP_RISKY STREQUAL "")
  message(FATAL_ERROR "LOCKSTEP_RISKY must be empty, unordered or float")
endif()

add_executable(replay tools/replay.cpp)
target_link_libraries(replay PRIVATE lockstep_core lockstep_warnings)

add_executable(bench tools/bench.cpp)
target_link_libraries(bench PRIVATE lockstep_core lockstep_warnings)

add_executable(lockstep tools/lockstep.cpp)
target_link_libraries(lockstep PRIVATE lockstep_core lockstep_warnings)
if(EMSCRIPTEN)
  # Let the Node build read and write real files (replays recorded elsewhere).
  target_link_options(lockstep PRIVATE -sNODERAWFS=1)
endif()

include(FetchContent)
# doctest 2.4.11 still declares cmake_minimum_required(3.0), which CMake 4 rejects
# (and below 3.10 warns about).
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
  GIT_SHALLOW TRUE
  SYSTEM)
FetchContent_MakeAvailable(doctest)

add_executable(core_tests tests/test_hash.cpp tests/test_world.cpp tests/test_fixed.cpp tests/test_rng.cpp tests/test_setup.cpp tests/test_combat.cpp tests/test_replay.cpp)
target_link_libraries(core_tests PRIVATE lockstep_core lockstep_warnings doctest::doctest_with_main)

# The viewer pulls in raylib, so it is opt-in: the determinism CI never needs it.
option(LOCKSTEP_VIEWER "Build the raylib match viewer" OFF)
if(LOCKSTEP_VIEWER)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  if(EMSCRIPTEN)
    set(PLATFORM Web CACHE STRING "" FORCE)
  endif()
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.0
    GIT_SHALLOW TRUE
    SYSTEM)
  FetchContent_MakeAvailable(raylib)
  add_executable(viewer tools/viewer.cpp)
  target_link_libraries(viewer PRIVATE lockstep_core lockstep_warnings raylib)
  if(EMSCRIPTEN)
    set_target_properties(viewer PROPERTIES OUTPUT_NAME index SUFFIX ".html")
    target_link_options(viewer PRIVATE -sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1
      "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/tools/shell.html")
    set_property(TARGET viewer APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/shell.html)
  endif()
endif()

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
