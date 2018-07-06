cmake_minimum_required(VERSION 3.2)

project(Qute)
set(Tutorial_VERSION_MAJOR 1)
set(Tutorial_VERSION_MINOR 0)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (LOGGING STREQUAL off)
  add_definitions(-DNO_LOGGING)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
if (CMAKE_BUILD_TYPE)
  message("Build type set to ${CMAKE_BUILD_TYPE}.")
else()
  set(CMAKE_BUILD_TYPE Release)
endif ()

include_directories("include")
include_directories("docopt")

add_subdirectory("docopt")
add_subdirectory("src")
