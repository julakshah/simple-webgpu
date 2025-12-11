# Require the CMakeVersion for Ubuntu 22.04
cmake_minimum_required(VERSION 3.22)

project(
  simple-webgpu-project
  VERSION 1.0
  LANGUAGES C CXX
)

set(CMAKE_C_STANDARD  17)
set(CMAKE_CXX_STANDARD 17)


# Turn on warnings that help flag the most common potential issues in code.
# set(CMAKE_C_FLAGS "-Wall -Wextra")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wdouble-promotion")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wduplicated-branches")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wduplicated-cond")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wlogical-op")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmisleading-indentation")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wnull-dereference")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wshadow")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wsign-conversion")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wunreachable-code")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wunused")
# Enable debugging through GDB.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

# Some setups don't detect clang-tidy automatically, so make sure it gets picked
# up here. (like Vim lol)
set(CMAKE_C_CLANG_TIDY "clang-tidy")

# The WebGPU-distribution repo lets us choose which webgpu implementation to use
set(WEBGPU_BACKEND "DAWN") # Google's implementation of webgpu
#set(WEBGPU_BACKEND "WGPU") # Implementation in Rust

add_subdirectory(WebGPU-distribution)
add_subdirectory(glfw)
add_subdirectory(src)

# For testing stuff I guess (Steve had these so we can change if we use
# something else)
include(CTest)
add_subdirectory(test)
