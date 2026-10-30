cmake_minimum_required(VERSION 3.28...3.30)

# Standalone build based on obsproject/obs-plugintemplate: no obs-studio
# source tree needed. Windows/macOS download OBS sources + obs-deps per
# buildspec.json; Linux uses the system libobs (ppa:obsproject/obs-studio).
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE corridor-key-filter.c)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
