idf_component_register(SRCS "main.c" "button.c" "data_storage.c" "wifi_sync.c"
    INCLUDE_DIRS ""
    REQUIRES display sensors wifi esp_http_client json nvs_flash spiffs
)
