cmake_minimum_required(VERSION 3.20)
project(EdenResearchReader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32)
  # MinGW hides some Windows/POSIX-compat declarations under strict -std=c++20
  set(CMAKE_CXX_EXTENSIONS ON)
else()
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(EDEN_BUILD_TESTS "Build unit tests" ON)
option(EDEN_BUILD_UI "Build the Qt user interface" ON)
option(EDEN_SANITIZE "Enable address and undefined behaviour sanitizers" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(cmake/Warnings.cmake)

if(EDEN_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(src/core)

if(EDEN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(EDEN_BUILD_UI AND EXISTS ${CMAKE_SOURCE_DIR}/src/ui/CMakeLists.txt)
  add_subdirectory(src/ui)
endif()
