idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES esp-tls esp_wifi esp_event nvs_flash esp_driver_uart esp_netif mqtt
    INCLUDE_DIRS "."
)
