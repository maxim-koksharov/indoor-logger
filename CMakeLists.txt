cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-threadsafe-statics")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(indoor_logger)
