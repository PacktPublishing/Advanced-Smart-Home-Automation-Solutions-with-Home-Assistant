#pragma once

#include "driver/i2c.h"
#include "esp_err.h"

#define BMP280_I2C_ADDRESS_0 0x76
#define BMP280_I2C_ADDRESS_1 0x77

typedef struct {
    i2c_port_t i2c_port;
    uint8_t i2c_addr;

    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;

    int32_t t_fine;
} bmp280_t;

void bmp280_init_desc(bmp280_t *dev, uint8_t addr, i2c_port_t port);
esp_err_t bmp280_init(bmp280_t *dev);
esp_err_t bmp280_read_float(bmp280_t *dev, float *temperature, float *pressure);
