#include "sdkconfig.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// 🔥 引入 espp/rtsp
#include "espp/rtsp_server.hpp"
#include "espp/camera_format.hpp"

// ==============================
// 你提供的完美引脚定义
// ==============================
#define CAM_PIN_PWDN    GPIO_NUM_NC
#define CAM_PIN_RESET   GPIO_NUM_NC
#define CAM_PIN_XCLK    GPIO_NUM_15
#define CAM_PIN_SIOD    GPIO_NUM_4
#define CAM_PIN_SIOC    GPIO_NUM_5
#define CAM_PIN_D7      GPIO_NUM_16
#define CAM_PIN_D6      GPIO_NUM_17
#define CAM_PIN_D5      GPIO_NUM_18
#define CAM_PIN_D4      GPIO_NUM_12
#define CAM_PIN_D3      GPIO_NUM_10
#define CAM_PIN_D2      GPIO_NUM_8
#define CAM_PIN_D1      GPIO_NUM_9
#define CAM_PIN_D0      GPIO_NUM_11
#define CAM_PIN_VSYNC   GPIO_NUM_6
#define CAM_PIN_HREF    GPIO_NUM_7
#define CAM_PIN_PCLK    GPIO_NUM_13

#define WIFI_SSID     "klp123456"
#define WIFI_PASS     "18902101360"
#define RTSP_PORT     8554
static const char *TAG = "CAM_RTSP";

static QueueHandle_t g_frame_queue = NULL;
static espp::RtspServer *g_rtsp_server = NULL;

// ==============================
// 摄像头初始化
// ==============================
static void camera_init(void) {
    camera_config_t cfg = {};

    // 引脚配置
    cfg.pin_pwdn     = CAM_PIN_PWDN;
    cfg.pin_reset    = CAM_PIN_RESET;
    cfg.pin_xclk     = CAM_PIN_XCLK;
    cfg.pin_sccb_sda = CAM_PIN_SIOD;
    cfg.pin_sccb_scl = CAM_PIN_SIOC;
    cfg.pin_d7       = CAM_PIN_D7;
    cfg.pin_d6       = CAM_PIN_D6;
    cfg.pin_d5       = CAM_PIN_D5;
    cfg.pin_d4       = CAM_PIN_D4;
    cfg.pin_d3       = CAM_PIN_D3;
    cfg.pin_d2       = CAM_PIN_D2;
    cfg.pin_d1       = CAM_PIN_D1;
    cfg.pin_d0       = CAM_PIN_D0;
    cfg.pin_vsync    = CAM_PIN_VSYNC;
    cfg.pin_href     = CAM_PIN_HREF;
    cfg.pin_pclk     = CAM_PIN_PCLK;

    // 时序配置
    cfg.xclk_freq_hz = 20000000;
    cfg.pixel_format = PIXFORMAT_JPEG;

    // 分辨率配置
    cfg.frame_size = FRAMESIZE_VGA; // 640x480
    cfg.jpeg_quality = 12;
    cfg.fb_count = 2;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;

    // 初始化摄像头
    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "摄像头初始化失败: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "✅ 摄像头初始化成功");
}

// ==============================
// 摄像头采集任务
// ==============================
void cam_task(void *arg) {
    ESP_LOGI(TAG, "摄像头采集任务启动");
    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "获取帧失败");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 把帧发送给 RTSP 服务器
        if (g_rtsp_server) {
            g_rtsp_server->send_frame(fb->buf, fb->len, fb->timestamp.tv_sec * 1000 + fb->timestamp.tv_usec / 1000);
        }

        // 释放帧
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(40)); // 25fps
    }
}

// ==============================
// WiFi 初始化
// ==============================
static void wifi_init_sta(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi 连接中...");
}

// ==============================
// 主函数
// ==============================
extern "C" void app_main(void) {
    // 初始化 WiFi
    wifi_init_sta();

    // 等待 WiFi 连接（简单延时，实际项目可以用事件）
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 初始化摄像头
    camera_init();

    // 🔥 初始化 RTSP 服务器
    ESP_LOGI(TAG, "启动 RTSP 服务器...");
    g_rtsp_server = new espp::RtspServer(RTSP_PORT, espp::CameraFormat::MJPEG, 640, 480, 25);
    g_rtsp_server->start();

    ESP_LOGI(TAG, "🚀 RTSP 服务已启动: rtsp://[你的IP]:%d/stream", RTSP_PORT);

    // 启动摄像头采集任务
    xTaskCreatePinnedToCore(cam_task, "cam_task", 4096, NULL, 5, NULL, 1);
}
