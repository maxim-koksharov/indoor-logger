idf_component_register(SRCS "main.c" "data_store.c" "client_registry.c" "http_server.c"
    INCLUDE_DIRS ""
    REQUIRES display wifi spiffs nvs_flash esp_http_server json
)
