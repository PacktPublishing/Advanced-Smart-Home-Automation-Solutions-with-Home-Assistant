#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "driver/uart.h"
#include "cJSON.h"
#include "led_strip.h"

#define WIFI_SSID       "your_ssid"
#define WIFI_PASSWORD   "your_password"
#define MQTT_BROKER_URI "your_MQTT_broker_IP"
#define MQTT_BROKER_USER "your_name"
#define MQTT_BROKER_PASSWORD "your_password"

#define MQTT_TOPIC_STATUS           "car1/gps/status"
#define MQTT_TOPIC_RAW              "car1/gps/raw"
#define MQTT_TOPIC_DIAG             "car1/gps/diagnostics"
#define PUB_UPDATE_RATE    5000

#define GPS_UART_NUM       UART_NUM_1
#define GPS_UART_RX_PIN    GPIO_NUM_4
#define GPS_UART_TX_PIN    GPIO_NUM_5
#define GPS_UART_BAUD_RATE 9600
#define GPS_BUF_SIZE       1024
#define GPS_UPDATE_RATE    200

#define LED_STRIP_GPIO      GPIO_NUM_8
#define LED_STRIP_LED_COUNT 1
#define LED_UPDATE_RATE     1000

static const char *TAG = "car1_gps";
static EventGroupHandle_t wifi_event_group;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;

static led_strip_handle_t led_strip = NULL;

#define WIFI_CONNECTED_BIT BIT0

typedef struct {
    double latitude;
    double longitude;
    double speed_kmh;
    int satellites;
    double altitude;
    double hdop;
    bool valid_fix;
} gps_data_t;

static gps_data_t gps = {0};

static double nmea_to_decimal(const char *nmea, char hemi)
{
    double raw = atof(nmea);
    int degrees = (int)(raw / 100);
    double minutes = raw - (degrees * 100);
    double decimal = degrees + (minutes / 60.0);
    if (hemi == 'S' || hemi == 'W') decimal = -decimal;
    return decimal;
}

static double knots_to_kmh(double knots)
{
    return knots * 1.852;
}

static void parse_gga(char *sentence)
{
    char *fields[20] = {0};
    int i = 0;
    char *token = strtok(sentence, ",");

    while (token && i < 20) {
        fields[i++] = token;
        token = strtok(NULL, ",");
    }

    if (i < 10) return;

    int fix_quality = atoi(fields[6]);
    if (fix_quality > 0) {
        gps.latitude = nmea_to_decimal(fields[2], fields[3][0]);
        gps.longitude = nmea_to_decimal(fields[4], fields[5][0]);
        gps.satellites = atoi(fields[7]);
        gps.hdop = atof(fields[8]);
        gps.altitude = atof(fields[9]);
        gps.valid_fix = true;
    } else {
        gps.hdop = 99.9;
        gps.satellites = 0;   
        gps.valid_fix = false;
    }
}

static void parse_rmc(char *sentence)
{
    char *fields[20] = {0};
    int i = 0;
    char *token = strtok(sentence, ",");

    while (token && i < 20) {
        fields[i++] = token;
        token = strtok(NULL, ",");
    }

    if (i < 8) return;

    if (fields[2][0] == 'A') {
        gps.latitude = nmea_to_decimal(fields[3], fields[4][0]);
        gps.longitude = nmea_to_decimal(fields[5], fields[6][0]);
        gps.speed_kmh = knots_to_kmh(atof(fields[7]));
        gps.valid_fix = true;
    } else {
        gps.valid_fix = false;
        ESP_LOGI(TAG, "gps.valid_fix = false");
    }
}

static void parse_nmea(char *line)
{
    if (strncmp(line, "$GPGGA", 6) == 0 || strncmp(line, "$GNGGA", 6) == 0) {
        parse_gga(line);
    } else if (strncmp(line, "$GPRMC", 6) == 0 || strncmp(line, "$GNRMC", 6) == 0) {
        parse_rmc(line);
    }
}

static void mqtt_publish_status(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "online", true);

    char *payload = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_STATUS, payload, 0, 1, 1);

    free(payload);
    cJSON_Delete(root);
}

static void mqtt_publish_raw(void)
{
    if (!gps.valid_fix) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "latitude", gps.latitude);
    cJSON_AddNumberToObject(root, "longitude", gps.longitude);
    cJSON_AddNumberToObject(root, "speed", gps.speed_kmh);
    cJSON_AddNumberToObject(root, "altitude", gps.altitude);
    
    char *payload = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_RAW, payload, 0, 1, 0);

    free(payload);
    cJSON_Delete(root);
}

static void mqtt_publish_diag(void)
{
    if (!gps.valid_fix) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "satellites", gps.satellites);
    cJSON_AddNumberToObject(root, "hdop", gps.hdop);

    char *payload = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_DIAG, payload, 0, 1, 0);

    free(payload);
    cJSON_Delete(root);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi start, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected");
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        mqtt_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi connected, got IP");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {}
    };

    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASSWORD);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    if (event_id == MQTT_EVENT_CONNECTED) {
        mqtt_connected = true;
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        mqtt_connected = false;
    }
}

static void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,

        .credentials = {
            .username = MQTT_BROKER_USER,
            .authentication = {
                .password = MQTT_BROKER_PASSWORD
            }
        //},
        }

        //.session = {
        ///    .last_will = {
        //        .topic = MQTT_TOPIC_STATUS,
        //        .msg = "{\"online\":false}",
        //        .qos = 1,
        //        .retain = 1
        //    }
        //}

    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

static void gps_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = GPS_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(GPS_UART_NUM, GPS_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(GPS_UART_NUM, &uart_config);
    uart_set_pin(GPS_UART_NUM, GPS_UART_TX_PIN, GPS_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void gps_task(void *pv)
{
    uint8_t *data = malloc(GPS_BUF_SIZE);
    char line[256];
    int pos = 0;

    while (1) {
        int len = uart_read_bytes(GPS_UART_NUM, data, GPS_BUF_SIZE - 1, pdMS_TO_TICKS(GPS_UPDATE_RATE));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = (char)data[i];
                if (c == '\n') {
                    line[pos] = '\0';
                    if (pos > 6 && line[0] == '$') {
                        parse_nmea(line);
                    }
                    pos = 0;
                } else if (pos < sizeof(line) - 1) {
                    line[pos++] = c;
                }
            }
        }
    }
}

static void publish_task(void *pv)
{
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
        if ((bits & WIFI_CONNECTED_BIT) && mqtt_connected) {
            mqtt_publish_status();
            mqtt_publish_raw();
            mqtt_publish_diag();
        }
        vTaskDelay(pdMS_TO_TICKS(PUB_UPDATE_RATE));
    }
}

static void led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        }
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        }
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
}

static void led_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    if (led_strip == NULL) {
        return;
    }

    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, red, green, blue));
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void update_status_led(void)
{
    if (!mqtt_connected) {
        led_set_rgb(32, 0, 32);   // purple / magenta
        return;
    }

    if (!gps.valid_fix || gps.hdop > 10.0) {
        led_set_rgb(32, 0, 0);      // red
        return;
    }

    if (gps.satellites < 5) {
        led_set_rgb(32, 24, 0);     // yellow
        return;
    }

    if (gps.hdop < 2.0 && gps.satellites > 10) {
        led_set_rgb(0, 32, 0);      // green
        return;
    }

    led_set_rgb(0, 0, 32);          // blue
}

static void led_task(void *pv)
{
    while (1) {
        update_status_led();
        vTaskDelay(pdMS_TO_TICKS(LED_UPDATE_RATE));
    }
}

void app_main(void)
{
    nvs_flash_init();
    led_init();
    wifi_init();
    mqtt_start();
    gps_uart_init();

    xTaskCreate(gps_task, "gps_task", 4096, NULL, 10, NULL);
    xTaskCreate(publish_task, "publish_task", 4096, NULL, 8, NULL);
    xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);
}