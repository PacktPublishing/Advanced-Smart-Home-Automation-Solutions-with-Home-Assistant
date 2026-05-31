# ESP32-S3 Camera Test Example

This ESP-IDF example is configured for your ESP32-S3 camera board and works with OV2640 and OV3660 using the following mapping:

- SDA: GPIO4
- SCL: GPIO5
- XCLK: GPIO15 @ 20 MHz
- D0..D7: GPIO11, GPIO9, GPIO8, GPIO10, GPIO12, GPIO18, GPIO17, GPIO16
- VSYNC: GPIO6
- HREF: GPIO7
- PCLK: GPIO13

Default runtime configuration:

- Resolution: 1024x768 (XGA)
- JPEG quality: 10
- Frame buffers: 2
- Frame buffer location: PSRAM

## Files

- `idf_component.yml`
- `main/CMakeLists.txt`
- `main/Kconfig.projbuild`
- `main/main.c`

## Build

- source ~/.espressif/v6.0/esp-idf/export.sh     

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

In `menuconfig`, set:

- PSRAM enabled

## Web endpoints

- `/` control page
- `/capture` one JPEG frame
- `/stream` MJPEG stream
- `/status` JSON state
- `/control?var=...&val=...` runtime tuning
- `/reset_sensor` sensor reset
