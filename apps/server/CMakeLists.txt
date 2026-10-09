cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS ../../components)

# Load WiFi credentials from wifi.env as compile definitions
include(${CMAKE_SOURCE_DIR}/../../cmake/wifi_config.cmake)
load_wifi_config()

# Generate partition table CSV before project() so the ESP-IDF partition
# tool uses our auto-generated file. The generator sizes the factory
# partition to the actual .bin size (plus a margin) when a previous build
# produced a bin; otherwise it falls back to a 1 MB factory.
set(_gen_script ${CMAKE_SOURCE_DIR}/../../cmake/generate_partitions.py)
set(_sdkconfig ${CMAKE_SOURCE_DIR}/sdkconfig)
set(_app_name "server")
set(_app_bin ${CMAKE_CURRENT_BINARY_DIR}/${_app_name}.bin)
set(_partitions_csv ${CMAKE_CURRENT_SOURCE_DIR}/partitions_${_app_name}.csv)

if(EXISTS "${_gen_script}" AND EXISTS "${_sdkconfig}")
    execute_process(
        COMMAND python3 ${_gen_script}
            --app ${_app_name}
            --sdkconfig ${_sdkconfig}
            --app-bin ${_app_bin}
            --output ${_partitions_csv}
        RESULT_VARIABLE _gen_result
    )
    if(NOT _gen_result EQUAL 0)
        message(FATAL_ERROR "Partition table generation failed")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(server)
