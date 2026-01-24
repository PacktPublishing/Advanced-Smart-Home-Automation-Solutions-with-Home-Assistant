/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "nvs.h"
#include "shell.h"
#include "wifi.h"
#include "sample_credentials.h"

/* BMP280 + I2C */
#include "bmp280.h"
#include "driver/i2c.h"

/* Golioth */
#include <golioth/client.h>

#include "fw_update.h"

/* ================================
 * Compile-time publish mode
 * ================================ */
// 1 = LightDB Stream
// 0 = LightDB State
#define COMPILE_STREAM 0

#if COMPILE_STREAM
#include <golioth/stream.h>
#define TAG "golioth_stream"
#define FW_VERSION "2.1"
#else
#include <golioth/lightdb_state.h>
#define TAG "golioth_state"
#define FW_VERSION "2.3"
#endif

static const char *_current_version = FW_VERSION;

/* ================================
 * I2C Master Configuration
 * ================================ */
#define I2C_MASTER_NUM      I2C_NUM_0
#define I2C_MASTER_SDA_IO  21
#define I2C_MASTER_SCL_IO  22
#define I2C_MASTER_FREQ_HZ 100000

/* ================================
 * Globals
 * ================================ */
static SemaphoreHandle_t _connected_sem = NULL;
static bmp280_t bmp280;

/* ================================
 * I2C Init
 * ================================ */
static void i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(
        I2C_MASTER_NUM,
        conf.mode,
        0,
        0,
        0
    ));
}

/* ================================
 * Golioth Client Event Callback
 * ================================ */
static void on_client_event(struct golioth_client *client,
                            enum golioth_client_event event,
                            void *arg)
{
    if (event == GOLIOTH_CLIENT_EVENT_CONNECTED) {
        xSemaphoreGive(_connected_sem);
        GLTH_LOGI(TAG, "Golioth connected");
    } else {
        GLTH_LOGW(TAG, "Golioth disconnected");
    }
}

/* ================================
 * BMP280 Init
 * ================================ */
static void bmp280_init_sensor(void)
{
    i2c_master_init();

    bmp280_init_desc(&bmp280, BMP280_I2C_ADDRESS_0, I2C_MASTER_NUM);

    if (bmp280_init(&bmp280) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BMP280");
    } else {
        ESP_LOGI(TAG, "BMP280 initialized");
    }
}

/* ================================
 * CBOR Encoder (shared)
 * ================================ */
static size_t encode_env_cbor(uint8_t *buf,
                              size_t buf_size,
                              float temperature_c,
                              float pressure_hpa)
{
    ZCBOR_STATE_E(zse, 1, buf, buf_size, 1);

    zcbor_map_start_encode(zse, 2);

    zcbor_tstr_put_lit(zse, "temperature_c");
    zcbor_float32_put(zse, temperature_c);

    zcbor_tstr_put_lit(zse, "pressure_hpa");
    zcbor_float32_put(zse, pressure_hpa);

    zcbor_map_end_encode(zse, 2);

    return (size_t)(zse->payload - buf);
}

/* ================================
 * Publish (Stream or State)
 * ================================ */
static void env_publish(struct golioth_client *client,
                        float temperature_c,
                        float pressure_hpa)
{
    uint8_t buf[64];
    size_t len = encode_env_cbor(
        buf,
        sizeof(buf),
        temperature_c,
        pressure_hpa
    );

#if COMPILE_STREAM
    golioth_stream_set(
        client,
        "data",
        GOLIOTH_CONTENT_TYPE_CBOR,
        buf,
        len,
        NULL,
        NULL
    );

    GLTH_LOGI(TAG,
              "Streamed env: T=%.2f C, P=%.2f hPa",
              temperature_c,
              pressure_hpa);
#else
    golioth_lightdb_set(
        client,
        "data",
        GOLIOTH_CONTENT_TYPE_CBOR,
        buf,
        len,
        NULL,
        NULL
    );

    GLTH_LOGI(TAG,
              "State updated: T=%.2f C, P=%.2f hPa",
              temperature_c,
              pressure_hpa);
#endif
}

/* ================================
 * app_main
 * ================================ */
void app_main(void)
{
    GLTH_LOGI(TAG, "Start BMP280 + Golioth example");

    nvs_init();
    shell_start();

    if (!nvs_credentials_are_set()) {
        ESP_LOGW(TAG,
                 "Credentials not set. Use shell to configure WiFi and Golioth.");
        while (!nvs_credentials_are_set()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    wifi_init(nvs_read_wifi_ssid(), nvs_read_wifi_password());
    wifi_wait_for_connected();

    bmp280_init_sensor();

    const struct golioth_client_config *config =
        golioth_sample_credentials_get();

    struct golioth_client *client = golioth_client_create(config);
    assert(client);

    _connected_sem = xSemaphoreCreateBinary();

    golioth_client_register_event_callback(client, on_client_event, NULL);
    golioth_client_start(client);

    GLTH_LOGI(TAG, "Waiting for Golioth connection...");
    xSemaphoreTake(_connected_sem, portMAX_DELAY);
    
    golioth_fw_update_init(client, _current_version);

    while (true) {
        float temperature;
        float pressure;

        if (bmp280_read_float(&bmp280, &temperature, &pressure) == ESP_OK) {
            env_publish(client, temperature, pressure / 100.0f);
        }

        vTaskDelay(pdMS_TO_TICKS(300000)); // 5 minutes
    }
}
