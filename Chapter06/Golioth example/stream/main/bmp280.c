#include "bmp280.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BMP280_REG_ID        0xD0
#define BMP280_REG_RESET    0xE0
#define BMP280_REG_CTRL     0xF4
#define BMP280_REG_CONFIG   0xF5
#define BMP280_REG_DATA     0xF7
#define BMP280_REG_CALIB    0x88

static esp_err_t i2c_read(bmp280_t *dev, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(
        dev->i2c_port,
        dev->i2c_addr,
        &reg,
        1,
        data,
        len,
        pdMS_TO_TICKS(100)
    );
}

static esp_err_t i2c_write(bmp280_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(
        dev->i2c_port,
        dev->i2c_addr,
        data,
        sizeof(data),
        pdMS_TO_TICKS(100)
    );
}

void bmp280_init_desc(bmp280_t *dev, uint8_t addr, i2c_port_t port)
{
    dev->i2c_addr = addr;
    dev->i2c_port = port;
}

esp_err_t bmp280_init(bmp280_t *dev)
{
    uint8_t calib[24];
    ESP_ERROR_CHECK(i2c_read(dev, BMP280_REG_CALIB, calib, sizeof(calib)));

    dev->dig_T1 = calib[1] << 8 | calib[0];
    dev->dig_T2 = calib[3] << 8 | calib[2];
    dev->dig_T3 = calib[5] << 8 | calib[4];
    dev->dig_P1 = calib[7] << 8 | calib[6];
    dev->dig_P2 = calib[9] << 8 | calib[8];
    dev->dig_P3 = calib[11] << 8 | calib[10];
    dev->dig_P4 = calib[13] << 8 | calib[12];
    dev->dig_P5 = calib[15] << 8 | calib[14];
    dev->dig_P6 = calib[17] << 8 | calib[16];
    dev->dig_P7 = calib[19] << 8 | calib[18];
    dev->dig_P8 = calib[21] << 8 | calib[20];
    dev->dig_P9 = calib[23] << 8 | calib[22];

    // Normal mode, temp & pressure oversampling x1
    ESP_ERROR_CHECK(i2c_write(dev, BMP280_REG_CTRL, 0x27));
    ESP_ERROR_CHECK(i2c_write(dev, BMP280_REG_CONFIG, 0xA0));

    return ESP_OK;
}

esp_err_t bmp280_read_float(bmp280_t *dev, float *temperature, float *pressure)
{
    uint8_t data[6];
    ESP_ERROR_CHECK(i2c_read(dev, BMP280_REG_DATA, data, 6));

    int32_t adc_P = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
    int32_t adc_T = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);

    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dev->dig_T1 << 1))) *
                   ((int32_t)dev->dig_T2)) >> 11;

    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dev->dig_T1)) *
                     ((adc_T >> 4) - ((int32_t)dev->dig_T1))) >> 12) *
                     ((int32_t)dev->dig_T3)) >> 14;

    dev->t_fine = var1 + var2;
    *temperature = (dev->t_fine * 5 + 128) >> 8;
    *temperature /= 100.0f;

    int64_t press_var1, press_var2, press_raw;

    press_var1 = ((int64_t)dev->t_fine) - 128000;
    press_var2 = press_var1 * press_var1 * dev->dig_P6;
    press_var2 = press_var2 + ((press_var1 * dev->dig_P5) << 17);
    press_var2 = press_var2 + (((int64_t)dev->dig_P4) << 35);
    press_var1 = ((press_var1 * press_var1 * dev->dig_P3) >> 8) +
                 ((press_var1 * dev->dig_P2) << 12);
    press_var1 = (((((int64_t)1) << 47) + press_var1) *
                  (int64_t)((uint16_t)dev->dig_P1)) >> 33;
    if (press_var1 == 0)
        return ESP_FAIL;

    press_raw = 1048576 - adc_P;
    press_raw = (((press_raw << 31) - press_var2) * 3125) / press_var1;
    press_var1 = (dev->dig_P9 * (press_raw >> 13) * (press_raw >> 13)) >> 25;
    press_var2 = (dev->dig_P8 * press_raw) >> 19;
    press_raw = ((press_raw + press_var1 + press_var2) >> 8) +
            (((int64_t)dev->dig_P7) << 4);

    /* Output pressure in Pascals */
    *pressure = press_raw / 256.0f;
    return ESP_OK;
}
