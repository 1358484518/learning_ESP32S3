#include "sdkconfig.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

#define WIFI_SSID     "klp123456"
#define WIFI_PASS     "18902101360"
static const char *TAG = "CAM_FINAL";

// 全局帧队列（解耦核心）
static QueueHandle_t g_frame_queue = NULL;

// ==============================
// 你的正确引脚（已验证）
// ==============================
#define PWDN_GPIO_NUM    GPIO_NUM_NC
#define RESET_GPIO_NUM   GPIO_NUM_NC
#define XCLK_GPIO_NUM    GPIO_NUM_15
#define SIOD_GPIO_NUM    GPIO_NUM_4
#define SIOC_GPIO_NUM    GPIO_NUM_5
#define D7_GPIO_NUM      GPIO_NUM_16
#define D6_GPIO_NUM      GPIO_NUM_17
#define D5_GPIO_NUM      GPIO_NUM_18
#define D4_GPIO_NUM      GPIO_NUM_12
#define D3_GPIO_NUM      GPIO_NUM_10
#define D2_GPIO_NUM      GPIO_NUM_8
#define D1_GPIO_NUM      GPIO_NUM_9
#define D0_GPIO_NUM      GPIO_NUM_11
#define VSYNC_GPIO_NUM   GPIO_NUM_6
#define HREF_GPIO_NUM    GPIO_NUM_7
#define PCLK_GPIO_NUM    GPIO_NUM_13

// ==============================
// 摄像头采集任务（绝对安全）
// ==============================
void cam_task(void *arg) {
    camera_fb_t *fb = NULL;

    while (1) {
        fb = esp_camera_fb_get();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 入队失败 = 队列满 → 必须释放！
        if (xQueueSend(g_frame_queue, &fb, 0) != pdPASS) {
            esp_camera_fb_return(fb);
        }

        // 入队成功 → 不释放！交给HTTP
    }
}

// ==============================
// HTTP 流处理（绝对安全）
// ==============================
static esp_err_t stream_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    httpd_resp_set_hdr(req, "Connection", "close");

    camera_fb_t *fb = NULL;

    while (1) {
        // 拿不到帧就继续等
        if (xQueueReceive(g_frame_queue, &fb, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        // 发送头
        char header[64];
        int hlen = snprintf(header, sizeof(header),
            "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", fb->len);

        if (httpd_resp_send_chunk(req, header, hlen) != ESP_OK) {
            esp_camera_fb_return(fb);  // 🔥 必放！
            break;
        }

        // 发送数据
        if (httpd_resp_send_chunk(req, (char *)fb->buf, fb->len) != ESP_OK) {
            esp_camera_fb_return(fb);  // 🔥 必放！
            break;
        }

        // 发送完成 → 释放
        esp_camera_fb_return(fb);
    }

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ==============================
// HTTP 首页
// ==============================
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    const char *html = R"HTML(
<html>
<head>
<meta charset="utf-8">
</head>
<body style="background:#000; text-align:center; margin:0; padding:10px;">
<h2 style="color:#fff;">ESP32-S3 摄像头</h2>
<img src="/stream" style="width:100%; max-width:640px;">
</body>
</html>
    )HTML";
    return httpd_resp_send(req, html, strlen(html));
}

// ==============================
// 启动 HTTP
// ==============================
static void start_http_server(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_index = {"/", HTTP_GET, index_handler, NULL};
        httpd_uri_t uri_stream = {"/stream", HTTP_GET, stream_handler, NULL};
        httpd_register_uri_handler(server, &uri_index);
        httpd_register_uri_handler(server, &uri_stream);
    }
}

// ==============================
// WiFi 事件回调
// ==============================
static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ip = (ip_event_got_ip_t*)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip->ip_info.ip));
    }
}

// ==============================
// WiFi 初始化
// ==============================
static void wifi_init_sta(void) {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    wifi_config_t wifi_cfg = {};
    strcpy((char*)wifi_cfg.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_cfg.sta.password, WIFI_PASS);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();
}

// ==============================
// 摄像头初始化
// ==============================
static void camera_init(void) {
    camera_config_t cfg = {};

    cfg.pin_pwdn     = PWDN_GPIO_NUM;
    cfg.pin_reset    = RESET_GPIO_NUM;
    cfg.pin_xclk     = XCLK_GPIO_NUM;
    cfg.pin_sccb_sda = SIOD_GPIO_NUM;
    cfg.pin_sccb_scl = SIOC_GPIO_NUM;
    cfg.pin_d7       = D7_GPIO_NUM;
    cfg.pin_d6       = D6_GPIO_NUM;
    cfg.pin_d5       = D5_GPIO_NUM;
    cfg.pin_d4       = D4_GPIO_NUM;
    cfg.pin_d3       = D3_GPIO_NUM;
    cfg.pin_d2       = D2_GPIO_NUM;
    cfg.pin_d1       = D1_GPIO_NUM;
    cfg.pin_d0       = D0_GPIO_NUM;
    cfg.pin_vsync    = VSYNC_GPIO_NUM;
    cfg.pin_href     = HREF_GPIO_NUM;
    cfg.pin_pclk     = PCLK_GPIO_NUM;

    cfg.xclk_freq_hz  = 10000000;
    cfg.pixel_format  = PIXFORMAT_JPEG;
    cfg.frame_size    = FRAMESIZE_VGA;
    cfg.jpeg_quality  = 18;
    cfg.fb_count      = 1;
    cfg.fb_location   = CAMERA_FB_IN_DRAM;

    esp_camera_init(&cfg);
}

// ==============================
// 主函数（完整安全）
// ==============================
extern "C" void app_main(void) {
    // 基础系统初始化
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    // 创建队列（深度=1，最稳定）
    g_frame_queue = xQueueCreate(1, sizeof(camera_fb_t*));

    // 摄像头初始化 + 独立任务
    camera_init();
    xTaskCreatePinnedToCore(cam_task, "cam_task", 4096, NULL, 5, NULL, 1);

    // 网络初始化
    wifi_init_sta();
    start_http_server();
}
