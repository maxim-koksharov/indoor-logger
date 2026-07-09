cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS ../../components)

# Load WiFi credentials from wifi.env as compile definitions
include(${CMAKE_SOURCE_DIR}/../../cmake/wifi_config.cmake)
load_wifi_config()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(client)
