#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_random.h"
#include "esp_netif_sntp.h"

/* 日志标签 */
static const char *TAG = "klphttpclient";

/* ==============================================
   【可自行修改】配置：访问任意HTTPS网站
   ============================================== */
#define WEB_SERVER        "www.baidu.com"    // 目标网站域名
#define WEB_URL           "https://www.baidu.com" // 目标URL
#define WEB_PORT          "443"

// HTTP GET 请求报文
static const char HTTPS_REQUEST[] = "GET " WEB_URL " HTTP/1.1\r\n"
                             "Host: "WEB_SERVER"\r\n"
                             "User-Agent: esp-idf/1.0 esp32\r\n"
                             "Connection: close\r\n"
                             "\r\n";

/* ==============================================
   WiFi 配置（你的WiFi账号密码）
   ============================================== */
#define EXAMPLE_ESP_WIFI_SSID      "klp123456"
#define EXAMPLE_ESP_WIFI_PASS      "18902101360"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;

/* ==============================================
   WiFi 事件处理 + 连接函数
   ============================================== */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", EXAMPLE_ESP_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID:%s", EXAMPLE_ESP_WIFI_SSID);
    }
}

void wifi_connect(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_init_sta();
}

/* ==============================================
   SNTP 时间同步（证书验证必须要时间）
   ============================================== */
static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    esp_netif_sntp_init(&config);
}

static esp_err_t obtain_time(void)
{
    int retry = 0;
    const int retry_count = 10;
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) != ESP_OK && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time... (%d/%d)", retry, retry_count);
    }
    if (retry == retry_count) return ESP_FAIL;
    setenv("TZ", "CST-8", 1);
    tzset();
    return ESP_OK;
}

/* ==============================================
   核心：HTTPS GET 请求（支持任意网站）
   🔥 关键修改：使用系统证书包，无固定证书限制
   ============================================== */
static void https_get_request(void)
{
    char buf[512];
    int ret, len;

    // 🔥 核心配置：启用系统全局证书包，支持所有正规HTTPS网站
    esp_tls_cfg_t cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        ESP_LOGE(TAG, "Failed to allocate esp_tls handle!");
        return;
    }

    // 建立HTTPS连接
    if (esp_tls_conn_http_new_sync(WEB_URL, &cfg, tls) == 1) {
        ESP_LOGI(TAG, "HTTPS 连接成功！");
    } else {
        ESP_LOGE(TAG, "HTTPS 连接失败！");
        goto cleanup;
    }

    // 发送HTTP请求
    size_t written_bytes = 0;
    do {
        ret = esp_tls_conn_write(tls, HTTPS_REQUEST + written_bytes, strlen(HTTPS_REQUEST) - written_bytes);
        if (ret >= 0) {
            ESP_LOGI(TAG, "%d bytes written", ret);
            written_bytes += ret;
        } else if (ret != ESP_TLS_ERR_SSL_WANT_READ && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "esp_tls_conn_write error");
            goto cleanup;
        }
    } while (written_bytes < strlen(HTTPS_REQUEST));

    // 读取响应
    ESP_LOGI(TAG, "Reading HTTP response...");
    do {
        len = sizeof(buf) - 1;
        memset(buf, 0x00, sizeof(buf));
        ret = esp_tls_conn_read(tls, (char *)buf, len);

        if (ret == ESP_TLS_ERR_SSL_WANT_WRITE || ret == ESP_TLS_ERR_SSL_WANT_READ) continue;
        if (ret < 0) { ESP_LOGE(TAG, "read error"); break; }
        if (ret == 0) { ESP_LOGI(TAG, "connection closed"); break; }

        // 打印服务器返回的数据
        for (int i = 0; i < ret; i++) putchar(buf[i]);
        putchar('\n');
    } while (1);

cleanup:
    esp_tls_conn_destroy(tls);
    // 倒计时后重试
    for (int countdown = 10; countdown >= 0; countdown--) {
        ESP_LOGI(TAG, "%d...", countdown);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/* ==============================================
   HTTPS 任务
   ============================================== */
static void https_request_task(void *pvparameters)
{
    ESP_LOGI(TAG, "Start HTTPS Client");
    while(1){
        https_get_request(); // 循环请求
        ESP_LOGI(TAG, "Minimum free heap size: %" PRIu32 " bytes", esp_get_minimum_free_heap_size());
    }
}

/* ==============================================
   主函数
   ============================================== */
void klpesptlshttpsclient(void)
{
    wifi_connect();       // 连接WiFi
    initialize_sntp();    // 初始化时间同步
    obtain_time();        // 同步时间（必须！）

    // 创建HTTPS任务
    xTaskCreate(&https_request_task, "https_get_task", 8192, NULL, 5, NULL);
}
