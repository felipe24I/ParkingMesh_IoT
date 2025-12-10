idf_component_register(
    SRCS "parking_mesh.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash driver esp_wifi esp_event esp_netif mqtt
)

