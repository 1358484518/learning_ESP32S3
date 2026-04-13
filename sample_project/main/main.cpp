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
static const char *TAG = "CAM_SAFE";

// 队列：用于在摄像头任务和HTTP任务之间传递数据
QueueHandle_t xFrameQueue = NULL;

/* ==============================
   📌 引脚定义
   ============================== */
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

camera_config_t camera_config;

/* ==============================
   🎥 摄像头采集任务 (独立运行，永不阻塞)
   ============================== */
void camera_task(void *pvParameters) {
    ESP_LOGI(TAG, "摄像头采集任务启动");
    
    while (1) {
        // 1. 获取一帧
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 2. 尝试把帧放入队列 (如果队列满了，说明网络慢，直接丢弃旧帧)
        if (xQueueSend(xFrameQueue, &fb, 0) != pdPASS) {
            // 队列满了，立刻释放这一帧，绝不阻塞
            esp_camera_fb_return(fb);
        }
        
        // 3. 稍微延时，避免100%占用CPU
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ==============================
   🌐 HTTP流处理 (从队列拿数据)
   ============================== */
static esp_err_t stream_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "客户端连接");
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "close");

    camera_fb_t *fb = NULL;
    
    while (1) {
        // 1. 从队列拿一帧 (等待最多1秒)
        if (xQueueReceive(xFrameQueue, &fb, pdMS_TO_TICKS(1000)) != pdPASS) {
            continue; // 没拿到，继续等
        }

        // 2. 发送HTTP头
        char header[64];
        int hlen = snprintf(header, sizeof(header), "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", fb->len);
        
        if (httpd_resp_send_chunk(req, header, hlen) != ESP_OK) {
            ESP_LOGW(TAG, "发送头失败，客户端断开");
            esp_camera_fb_return(fb); // 🔥 关键：无论如何都释放
            break;
        }

        // 3. 发送JPEG数据
        if (httpd_resp_send_chunk(req, (char*)fb->buf, fb->len) != ESP_OK) {
            ESP_LOGW(TAG, "发送数据失败，客户端断开");
            esp_camera_fb_return(fb); // 🔥 关键：无论如何都释放
            break;
        }

        // 4. 发送成功，释放缓冲
        esp_camera_fb_return(fb);
    }
    
    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "连接关闭");
    return ESP_OK;
}

/* ==============================
   🌐 首页
   ============================== */
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    const char* html = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32-CAM 监控</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { margin:0; padding:20px; background:#1a1a1a; color:#fff; font-family:Arial; text-align:center; }
        img { max-width:100%; height:auto; border:2px solid #333; border-radius:8px; }
    </style>
</head>
<body>
    <h1>📷 ESP32-CAM 监控</h1>
    <img src="/stream" autoplay playsinline>
</body>
</html>
    )HTML";
    return httpd_resp_send(req, html, strlen(html));
}

/* ==============================
   🌐 Favicon
   ============================== */
static esp_err_t favicon_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* ==============================
   🚀 启动HTTP
   ============================== */
static void start_server(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t favicon_uri = {.uri="/favicon.ico", .method=HTTP_GET, .handler=favicon_handler, .user_ctx=NULL};
        httpd_register_uri_handler(server, &favicon_uri);
        
        httpd_uri_t index_uri = {.uri="/", .method=HTTP_GET, .handler=index_handler, .user_ctx=NULL};
        httpd_register_uri_handler(server, &index_uri);
        
        httpd_uri_t stream_uri = {.uri="/stream", .method=HTTP_GET, .handler=stream_handler, .user_ctx=NULL};
        httpd_register_uri_handler(server, &stream_uri);
    }
}

/* ==============================
   📶 WiFi
   ============================== */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_STA_START) esp_wifi_connect();
    if (event_id == WIFI_EVENT_STA_DISCONNECTED) esp_wifi_connect();
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "🎉 成功! 访问: http://" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void init_wifi(void) {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    
    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

/* ==============================
   🎥 摄像头初始化
   ============================== */
static esp_err_t init_camera(void) {
    memset(&camera_config, 0, sizeof(camera_config));
    
    camera_config.pin_pwdn = PWDN_GPIO_NUM;
    camera_config.pin_reset = RESET_GPIO_NUM;
    camera_config.pin_xclk = XCLK_GPIO_NUM;
    camera_config.pin_sccb_sda = SIOD_GPIO_NUM;
    camera_config.pin_sccb_scl = SIOC_GPIO_NUM;
    camera_config.pin_d7 = D7_GPIO_NUM;
    camera_config.pin_d6 = D6_GPIO_NUM;
    camera_config.pin_d5 = D5_GPIO_NUM;
    camera_config.pin_d4 = D4_GPIO_NUM;
    camera_config.pin_d3 = D3_GPIO_NUM;
    camera_config.pin_d2 = D2_GPIO_NUM;
    camera_config.pin_d1 = D1_GPIO_NUM;
    camera_config.pin_d0 = D0_GPIO_NUM;
    camera_config.pin_vsync = VSYNC_GPIO_NUM;
    camera_config.pin_href = HREF_GPIO_NUM;
    camera_config.pin_pclk = PCLK_GPIO_NUM;
    
    camera_config.xclk_freq_hz = 20000000;
    camera_config.ledc_timer = LEDC_TIMER_0;
    camera_config.ledc_channel = LEDC_CHANNEL_0;
    camera_config.pixel_format = PIXFORMAT_JPEG;
    camera_config.frame_size = FRAMESIZE_QVGA;
    camera_config.jpeg_quality = 12;
    camera_config.fb_count = 2; // 双缓冲
    camera_config.grab_mode = CAMERA_GRAB_LATEST;
    camera_config.fb_location = CAMERA_FB_IN_DRAM;
    camera_config.sccb_i2c_port = 0;

    esp_err_t ret = esp_camera_init(&camera_config);
    if (ret != ESP_OK) return ret;
    
    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
    return ESP_OK;
}

/* ==============================
   🏁 主函数
   ============================== */
extern "C" void app_main(void) {
    // 1. 创建队列 (最多存2帧)
    xFrameQueue = xQueueCreate(2, sizeof(camera_fb_t*));
    
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    // 2. 初始化摄像头
    if (init_camera() != ESP_OK) {
        ESP_LOGE(TAG, "摄像头初始化失败");
        return;
    }

    // 3. 启动摄像头采集任务 (优先级高一点)
    xTaskCreatePinnedToCore(camera_task, "camera_task", 4096, NULL, 5, NULL, 1);

    // 4. 连接WiFi
    init_wifi();

    // 5. 启动HTTP服务器
    start_server();
}
