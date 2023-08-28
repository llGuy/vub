cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(example LANGUAGES CXX)

find_package(Vulkan)

file(GLOB_RECURSE SOURCES "*.cc" "*.h")

add_executable(example ${SOURCES})

target_link_libraries(example PUBLIC
  ${Vulkan_LIBRARY})
target_include_directories(example PUBLIC
  ${Vulkan_INCLUDE_DIRS})

target_compile_definitions(example PUBLIC PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
