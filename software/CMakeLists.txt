# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(CCACHE_ENABLE 0 CACHE BOOL "Disable ccache for reproducible Windows builds" FORCE)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/bsp")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(software)
