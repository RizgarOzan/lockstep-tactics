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

add_library(lockstep_core STATIC core/src/world.cpp)
target_include_directories(lockstep_core PUBLIC core/include)
target_link_libraries(lockstep_core PRIVATE lockstep_warnings)

add_executable(replay tools/replay.cpp)
target_link_libraries(replay PRIVATE lockstep_core lockstep_warnings)

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

add_executable(core_tests tests/test_hash.cpp tests/test_world.cpp)
target_link_libraries(core_tests PRIVATE lockstep_core lockstep_warnings doctest::doctest_with_main)

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
