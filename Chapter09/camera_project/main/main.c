#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "img_converters.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define WIFI_SSID      CONFIG_EXAMPLE_WIFI_SSID
#define WIFI_PASSWORD  CONFIG_EXAMPLE_WIFI_PASSWORD

// ============================
// Camera pin map from ESPHome
// ============================
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   15
#define CAM_PIN_SIOD   4
#define CAM_PIN_SIOC   5

// Order matches ESPHome data_pins: [D0..D7]
#define CAM_PIN_D0     11
#define CAM_PIN_D1     9
#define CAM_PIN_D2     8
#define CAM_PIN_D3     10
#define CAM_PIN_D4     12
#define CAM_PIN_D5     18
#define CAM_PIN_D6     17
#define CAM_PIN_D7     16

#define CAM_PIN_VSYNC  6
#define CAM_PIN_HREF   7
#define CAM_PIN_PCLK   13

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *TAG = "cam_test";

static httpd_handle_t s_httpd = NULL;
static sensor_t *s_sensor = NULL;
static camera_model_t s_model = CAMERA_NONE;

static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static const char index_html[] =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP32-S3 Camera Test</title>"
"<style>body{font-family:Arial,sans-serif;margin:20px;max-width:960px}"
"button,select,input{margin:4px;padding:8px} .row{margin:10px 0}"
"img{max-width:100%;height:auto;border:1px solid #ccc} code{background:#f4f4f4;padding:2px 4px}</style>"
"</head><body>"
"<h2>ESP32-S3 Camera Test</h2>"
"<p>Endpoints: <code>/capture</code>, <code>/stream</code>, <code>/status</code>, <code>/control?var=...&val=...</code></p>"
"<div class='row'><a href='/capture' target='_blank'><button>Capture JPEG</button></a>"
"<a href='/stream' target='_blank'><button>Open MJPEG stream</button></a>"
"<a href='/status' target='_blank'><button>Status JSON</button></a>"
"<a href='/reset_sensor' target='_blank'><button>Reset Sensor</button></a></div>"
"<div class='row'><img src='/stream'></div>"
"<div class='row'><label>framesize</label>"
"<select id='framesize'>"
"<option value='5'>240x176 HQVGA</option>"
"<option value='6'>320x240 QVGA</option>"
"<option value='8'>400x296 CIF</option>"
"<option value='10'>640x480 VGA</option>"
"<option value='11'>800x600 SVGA</option>"
"<option value='12' selected>1024x768 XGA</option>"
"<option value='13'>1280x720 HD</option>"
"<option value='14'>1280x1024 SXGA</option>"
"<option value='15'>1600x1200 UXGA</option>"
"<option value='18'>2048x1536 QXGA (OV3660)</option>"
"</select>"
"<button onclick=\"sendCtrl('framesize',document.getElementById('framesize').value)\">Apply</button></div>"
"<div class='row'><label>quality (smaller=better JPEG)</label>"
"<input id='quality' type='number' min='4' max='63' value='10'>"
"<button onclick=\"sendCtrl('quality',document.getElementById('quality').value)\">Apply</button></div>"
"<div class='row'><label>brightness -2..2</label><input id='brightness' type='number' min='-2' max='2' value='0'>"
"<button onclick=\"sendCtrl('brightness',document.getElementById('brightness').value)\">Apply</button></div>"
"<div class='row'><label>contrast -2..2</label><input id='contrast' type='number' min='-2' max='2' value='0'>"
"<button onclick=\"sendCtrl('contrast',document.getElementById('contrast').value)\">Apply</button></div>"
"<div class='row'><label>saturation -2..2</label><input id='saturation' type='number' min='-2' max='2' value='0'>"
"<button onclick=\"sendCtrl('saturation',document.getElementById('saturation').value)\">Apply</button></div>"
"<div class='row'><label>sharpness -2..2</label><input id='sharpness' type='number' min='-2' max='2' value='0'>"
"<button onclick=\"sendCtrl('sharpness',document.getElementById('sharpness').value)\">Apply</button></div>"
"<div class='row'><label>denoise 0..8</label><input id='denoise' type='number' min='0' max='8' value='0'>"
"<button onclick=\"sendCtrl('denoise',document.getElementById('denoise').value)\">Apply</button></div>"
"<div class='row'>"
"<button onclick=\"sendCtrl('hmirror',1)\">Mirror ON</button>"
"<button onclick=\"sendCtrl('hmirror',0)\">Mirror OFF</button>"
"<button onclick=\"sendCtrl('vflip',1)\">Flip ON</button>"
"<button onclick=\"sendCtrl('vflip',0)\">Flip OFF</button>"
"<button onclick=\"sendCtrl('colorbar',1)\">Colorbar ON</button>"
"<button onclick=\"sendCtrl('colorbar',0)\">Colorbar OFF</button>"
"</div>"
"<div class='row'>"
"<button onclick=\"sendCtrl('awb',1)\">AWB ON</button>"
"<button onclick=\"sendCtrl('awb',0)\">AWB OFF</button>"
"<button onclick=\"sendCtrl('awb_gain',1)\">AWB Gain ON</button>"
"<button onclick=\"sendCtrl('awb_gain',0)\">AWB Gain OFF</button>"
"<button onclick=\"sendCtrl('aec',1)\">AEC ON</button>"
"<button onclick=\"sendCtrl('aec',0)\">AEC OFF</button>"
"<button onclick=\"sendCtrl('agc',1)\">AGC ON</button>"
"<button onclick=\"sendCtrl('agc',0)\">AGC OFF</button>"
"</div>"
"<div class='row'><label>special_effect 0..6</label><input id='effect' type='number' min='0' max='6' value='0'>"
"<button onclick=\"sendCtrl('special_effect',document.getElementById('effect').value)\">Apply</button></div>"
"<div class='row'><label>wb_mode 0..4</label><input id='wb' type='number' min='0' max='4' value='0'>"
"<button onclick=\"sendCtrl('wb_mode',document.getElementById('wb').value)\">Apply</button></div>"
"<div class='row'><label>ae_level -2..2</label><input id='ae_level' type='number' min='-2' max='2' value='0'>"
"<button onclick=\"sendCtrl('ae_level',document.getElementById('ae_level').value)\">Apply</button></div>"
"<div class='row'><label>aec_value 0..1200</label><input id='aec_value' type='number' min='0' max='1200' value='300'>"
"<button onclick=\"sendCtrl('aec_value',document.getElementById('aec_value').value)\">Apply</button></div>"
"<div class='row'><label>agc_gain 0..30</label><input id='agc_gain' type='number' min='0' max='30' value='0'>"
"<button onclick=\"sendCtrl('agc_gain',document.getElementById('agc_gain').value)\">Apply</button></div>"
"<div class='row'><label>gainceiling 0..6</label><input id='gainceiling' type='number' min='0' max='6' value='0'>"
"<button onclick=\"sendCtrl('gainceiling',document.getElementById('gainceiling').value)\">Apply</button></div>"
"<pre id='out'></pre>"
"<script>async function sendCtrl(v,val){const r=await fetch(`/control?var=${encodeURIComponent(v)}&val=${encodeURIComponent(val)}`);document.getElementById('out').textContent=await r.text();}"
"</script></body></html>";

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Open: http://" IPSTR "/", IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static camera_config_t make_camera_config(void)
{
    camera_config_t config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_XGA,
        .jpeg_quality = 10,
        .fb_count = 2,
        .grab_mode = CAMERA_GRAB_LATEST,
        .fb_location = CAMERA_FB_IN_PSRAM,
    };
    return config;
}

static esp_err_t camera_init_with_defaults(void)
{
    camera_config_t config = make_camera_config();
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: 0x%x", err);
        return err;
    }

    s_sensor = esp_camera_sensor_get();
    if (!s_sensor) {
        ESP_LOGE(TAG, "esp_camera_sensor_get failed");
        return ESP_FAIL;
    }

    camera_sensor_info_t *info = esp_camera_sensor_get_info(&s_sensor->id);
    if (info) {
        s_model = info->model;
        ESP_LOGI(TAG, "Detected sensor: %s PID=0x%04X addr=0x%02X max_framesize=%d jpeg=%d",
                 info->name, s_sensor->id.PID, s_sensor->slv_addr, info->max_size, info->support_jpeg);
    } else {
        ESP_LOGW(TAG, "Unknown sensor PID=0x%04X", s_sensor->id.PID);
    }

    s_sensor->set_quality(s_sensor, 10);
    s_sensor->set_framesize(s_sensor, FRAMESIZE_XGA);
    s_sensor->set_brightness(s_sensor, 0);
    s_sensor->set_contrast(s_sensor, 0);
    s_sensor->set_saturation(s_sensor, 0);
    if (s_sensor->set_sharpness) s_sensor->set_sharpness(s_sensor, 0);
    if (s_sensor->set_denoise) s_sensor->set_denoise(s_sensor, 0);
    s_sensor->set_whitebal(s_sensor, 1);
    s_sensor->set_awb_gain(s_sensor, 1);
    s_sensor->set_gain_ctrl(s_sensor, 1);
    s_sensor->set_exposure_ctrl(s_sensor, 1);
    s_sensor->set_aec2(s_sensor, 0);
    s_sensor->set_hmirror(s_sensor, 0);
    s_sensor->set_vflip(s_sensor, 0);
    s_sensor->set_colorbar(s_sensor, 0);

    if (s_model == CAMERA_OV3660) {
        ESP_LOGI(TAG, "OV3660 detected: use /control?var=framesize&val=18 for QXGA 2048x1536");
    } else if (s_model == CAMERA_OV2640) {
        ESP_LOGI(TAG, "OV2640 detected: max practical JPEG size is UXGA 1600x1200");
    }

    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera capture failed");
        return ESP_FAIL;
    }

    esp_err_t res = httpd_resp_set_type(req, "image/jpeg");
    if (res == ESP_OK) {
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    }
    if (res == ESP_OK) {
        if (fb->format == PIXFORMAT_JPEG) {
            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
        } else {
            uint8_t *jpg_buf = NULL;
            size_t jpg_len = 0;
            bool ok = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
            if (!ok) {
                esp_camera_fb_return(fb);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "jpeg convert failed");
                return ESP_FAIL;
            }
            res = httpd_resp_send(req, (const char *)jpg_buf, jpg_len);
            free(jpg_buf);
        }
    }

    esp_camera_fb_return(fb);
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    int64_t last_frame = esp_timer_get_time();
    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "stream capture failed");
            return ESP_FAIL;
        }

        uint8_t *jpg_buf = NULL;
        size_t jpg_len = 0;
        bool converted = false;

        if (fb->format == PIXFORMAT_JPEG) {
            jpg_buf = fb->buf;
            jpg_len = fb->len;
        } else {
            converted = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
            if (!converted) {
                esp_camera_fb_return(fb);
                ESP_LOGE(TAG, "stream jpeg convert failed");
                return ESP_FAIL;
            }
        }

        char part_buf[64];
        int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, (unsigned)jpg_len);
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_len);

        if (converted) free(jpg_buf);
        esp_camera_fb_return(fb);

        if (res != ESP_OK) {
            break;
        }

        int64_t now = esp_timer_get_time();
        int64_t frame_ms = (now - last_frame) / 1000;
        last_frame = now;
        float fps = frame_ms > 0 ? 1000.0f / (float)frame_ms : 0.0f;
        ESP_LOGI(TAG, "MJPEG frame: %lld ms (%.2f fps)", frame_ms, fps);
    }

    return res;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!s_sensor) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sensor not ready");
        return ESP_FAIL;
    }

    camera_status_t st = s_sensor->status;
    char json[1024];
    snprintf(json, sizeof(json),
             "{"
             "\"pid\":%u,"
             "\"model\":%d,"
             "\"framesize\":%d,"
             "\"quality\":%u,"
             "\"brightness\":%d,"
             "\"contrast\":%d,"
             "\"saturation\":%d,"
             "\"sharpness\":%d,"
             "\"denoise\":%u,"
             "\"special_effect\":%u,"
             "\"wb_mode\":%u,"
             "\"awb\":%u,"
             "\"awb_gain\":%u,"
             "\"aec\":%u,"
             "\"aec2\":%u,"
             "\"ae_level\":%d,"
             "\"aec_value\":%u,"
             "\"agc\":%u,"
             "\"agc_gain\":%u,"
             "\"gainceiling\":%u,"
             "\"bpc\":%u,"
             "\"wpc\":%u,"
             "\"raw_gma\":%u,"
             "\"lenc\":%u,"
             "\"hmirror\":%u,"
             "\"vflip\":%u,"
             "\"dcw\":%u,"
             "\"colorbar\":%u"
             "}",
             s_sensor->id.PID, s_model, st.framesize, st.quality, st.brightness,
             st.contrast, st.saturation, st.sharpness, st.denoise,
             st.special_effect, st.wb_mode, st.awb, st.awb_gain,
             st.aec, st.aec2, st.ae_level, st.aec_value, st.agc,
             st.agc_gain, st.gainceiling, st.bpc, st.wpc, st.raw_gma,
             st.lenc, st.hmirror, st.vflip, st.dcw, st.colorbar);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_text(httpd_req_t *req, const char *msg)
{
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t reset_sensor_handler(httpd_req_t *req)
{
    if (!s_sensor) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sensor not ready");
        return ESP_FAIL;
    }
    if (s_sensor->reset(s_sensor) != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sensor reset failed");
        return ESP_FAIL;
    }
    return send_text(req, "sensor reset ok\n");
}

static esp_err_t control_handler(httpd_req_t *req)
{
    if (!s_sensor) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sensor not ready");
        return ESP_FAIL;
    }

    char query[128];
    char var[32];
    char valstr[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "var", var, sizeof(var)) != ESP_OK ||
        httpd_query_key_value(query, "val", valstr, sizeof(valstr)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing var/val");
        return ESP_FAIL;
    }

    int val = atoi(valstr);
    int rc = -1;

    if (!strcmp(var, "framesize")) rc = s_sensor->set_framesize(s_sensor, (framesize_t)val);
    else if (!strcmp(var, "quality")) rc = s_sensor->set_quality(s_sensor, val);
    else if (!strcmp(var, "brightness")) rc = s_sensor->set_brightness(s_sensor, val);
    else if (!strcmp(var, "contrast")) rc = s_sensor->set_contrast(s_sensor, val);
    else if (!strcmp(var, "saturation")) rc = s_sensor->set_saturation(s_sensor, val);
    else if (!strcmp(var, "sharpness") && s_sensor->set_sharpness) rc = s_sensor->set_sharpness(s_sensor, val);
    else if (!strcmp(var, "denoise") && s_sensor->set_denoise) rc = s_sensor->set_denoise(s_sensor, val);
    else if (!strcmp(var, "hmirror")) rc = s_sensor->set_hmirror(s_sensor, val);
    else if (!strcmp(var, "vflip")) rc = s_sensor->set_vflip(s_sensor, val);
    else if (!strcmp(var, "colorbar")) rc = s_sensor->set_colorbar(s_sensor, val);
    else if (!strcmp(var, "awb")) rc = s_sensor->set_whitebal(s_sensor, val);
    else if (!strcmp(var, "awb_gain")) rc = s_sensor->set_awb_gain(s_sensor, val);
    else if (!strcmp(var, "wb_mode")) rc = s_sensor->set_wb_mode(s_sensor, val);
    else if (!strcmp(var, "aec")) rc = s_sensor->set_exposure_ctrl(s_sensor, val);
    else if (!strcmp(var, "aec2")) rc = s_sensor->set_aec2(s_sensor, val);
    else if (!strcmp(var, "aec_value")) rc = s_sensor->set_aec_value(s_sensor, val);
    else if (!strcmp(var, "ae_level")) rc = s_sensor->set_ae_level(s_sensor, val);
    else if (!strcmp(var, "agc")) rc = s_sensor->set_gain_ctrl(s_sensor, val);
    else if (!strcmp(var, "agc_gain")) rc = s_sensor->set_agc_gain(s_sensor, val);
    else if (!strcmp(var, "gainceiling")) rc = s_sensor->set_gainceiling(s_sensor, (gainceiling_t)val);
    else if (!strcmp(var, "special_effect")) rc = s_sensor->set_special_effect(s_sensor, val);
    else if (!strcmp(var, "dcw")) rc = s_sensor->set_dcw(s_sensor, val);
    else if (!strcmp(var, "bpc")) rc = s_sensor->set_bpc(s_sensor, val);
    else if (!strcmp(var, "wpc")) rc = s_sensor->set_wpc(s_sensor, val);
    else if (!strcmp(var, "raw_gma")) rc = s_sensor->set_raw_gma(s_sensor, val);
    else if (!strcmp(var, "lenc")) rc = s_sensor->set_lenc(s_sensor, val);

    if (rc != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "control failed: var=%s val=%d rc=%d\n", var, val, rc);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
        return ESP_FAIL;
    }

    char ok[128];
    snprintf(ok, sizeof(ok), "ok: %s=%d\n", var, val);
    return send_text(req, ok);
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 16;
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return NULL;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };
    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL
    };
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };
    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
    };
    httpd_uri_t control_uri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = control_handler,
        .user_ctx = NULL
    };
    httpd_uri_t reset_uri = {
        .uri = "/reset_sensor",
        .method = HTTP_GET,
        .handler = reset_sensor_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &capture_uri);
    httpd_register_uri_handler(server, &stream_uri);
    httpd_register_uri_handler(server, &status_uri);
    httpd_register_uri_handler(server, &control_uri);
    httpd_register_uri_handler(server, &reset_uri);

    return server;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());
    wifi_init_sta();
    ESP_ERROR_CHECK(camera_init_with_defaults());
    s_httpd = start_webserver();
    if (!s_httpd) {
        ESP_LOGE(TAG, "webserver failed to start");
    }
}
