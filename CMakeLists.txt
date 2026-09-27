cmake_minimum_required(VERSION 3.25)

project(
  ItchBook
  VERSION 0.1.0
  DESCRIPTION "Low-latency Nasdaq TotalView-ITCH 5.0 feed handler and limit order book"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ITCHBOOK_BUILD_TESTS "Build unit tests" ON)
option(ITCHBOOK_BUILD_BENCHMARKS "Build benchmarks" ON)
option(ITCHBOOK_BUILD_APPS "Build command-line tools" ON)
option(ITCHBOOK_NATIVE "Tune code generation for the build machine (-march=native)" OFF)
option(ITCHBOOK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(ITCHBOOK_SANITIZERS "" CACHE STRING "Comma-separated sanitizers, e.g. address,undefined or thread")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(Dependencies)

find_package(Threads REQUIRED)

add_subdirectory(src)

if(ITCHBOOK_BUILD_APPS)
  add_subdirectory(apps)
endif()

if(ITCHBOOK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(ITCHBOOK_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
