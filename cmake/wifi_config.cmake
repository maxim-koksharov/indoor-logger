# Load WiFi credentials from /workspace/wifi.env as compile definitions.
# Expected format per line: KEY=VALUE
function(load_wifi_config)
    set(WIFI_ENV_FILE "${CMAKE_SOURCE_DIR}/../../wifi.env")
    if(EXISTS "${WIFI_ENV_FILE}")
        file(STRINGS "${WIFI_ENV_FILE}" WIFI_LINES)
        foreach(LINE ${WIFI_LINES})
            # Skip empty lines and comments
            string(STRIP "${LINE}" LINE)
            if(LINE STREQUAL "" OR LINE MATCHES "^#")
                continue()
            endif()
            if(LINE MATCHES "^([A-Za-z_0-9]+)=(.+)$")
                set(KEY "${CMAKE_MATCH_1}")
                set(VALUE "${CMAKE_MATCH_2}")
                # Pass as -DKEY="value" so the macro expands to a C string literal.
                # add_compile_options preserves the quoting better than add_compile_definitions
                # when the value contains spaces.
                add_compile_options(-D${KEY}="${VALUE}")
                message(STATUS "WiFi env loaded: ${KEY}")
            endif()
        endforeach()
    else()
        message(WARNING "wifi.env not found, using Kconfig defaults")
    endif()
endfunction()
