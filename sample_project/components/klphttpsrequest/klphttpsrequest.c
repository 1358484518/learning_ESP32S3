#include "sdkconfig.h"
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <esp_sntp.h>
#include "esp_netif_sntp.h"

// mbedTLS 相关头文件（TLS/SSL 加密通信）
#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/error.h"
#include "mbedtls/ctr_drbg.h"   // 确定性随机数生成器
#include "mbedtls/entropy.h"    // 熵源（硬件随机数）
#include "esp_crt_bundle.h"     // ESP-IDF 内置 CA 证书包

// ======================================
// 配置宏定义
// ======================================
#define WEB_SERVER "www.boce.com"       // HTTPS 服务器域名
#define WEB_PORT "443"                   // HTTPS 标准端口
#define WEB_URL "https://www.boce.com/help/1018.html"  // 要请求的完整 URL

// WiFi 配置
#define EXAMPLE_ESP_WIFI_SSID      "klp123456"  // WiFi 名称
#define EXAMPLE_ESP_WIFI_PASS      "18902101360" // WiFi 密码
#define EXAMPLE_ESP_MAXIMUM_RETRY  5              // WiFi 重连最大次数
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH       // WPA3 兼容模式
#define EXAMPLE_H2E_IDENTIFIER ""
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK // 支持 WPA2

static const char *TAG = "klphttpsre"; // 日志标签

// HTTP GET 请求报文（HTTP/1.0 协议，请求完成后服务器主动断开）
static const char *REQUEST = "GET " WEB_URL " HTTP/1.0\r\n"
    "Host: "WEB_SERVER"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

// ======================================
// WiFi 连接相关变量和函数
// ======================================
static EventGroupHandle_t s_wifi_event_group; // FreeRTOS 事件组（用于同步 WiFi 连接状态）
#define WIFI_CONNECTED_BIT BIT0  // WiFi 连接成功标志位
#define WIFI_FAIL_BIT      BIT1  // WiFi 连接失败标志位
static int s_retry_num = 0;      // WiFi 重连计数器

/**
 * @brief WiFi 事件处理回调函数
 * 
 * 处理 WiFi 启动、断开、获取 IP 等事件
 */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // WiFi 启动完成，开始连接
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // WiFi 断开，尝试重连
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            // 重连次数用完，设置失败标志
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // 成功获取到 IP 地址
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT); // 设置连接成功标志
    }
}

/**
 * @brief 初始化 WiFi Station 模式
 * 
 * 创建事件组、初始化网络接口、注册事件回调、启动 WiFi
 */
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate(); // 创建事件组

    ESP_ERROR_CHECK(esp_netif_init());                          // 初始化网络接口
    ESP_ERROR_CHECK(esp_event_loop_create_default());             // 创建默认事件循环
    esp_netif_create_default_wifi_sta();                         // 创建默认 WiFi Station

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();         // 加载 WiFi 默认配置
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));                        // 初始化 WiFi

    // 注册 WiFi 事件回调（所有 WiFi 事件）
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    // 注册 IP 事件回调（仅获取 IP 事件）
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    // 配置 WiFi 参数
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );             // 设置为 Station 模式
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) ); // 写入 WiFi 配置
    ESP_ERROR_CHECK(esp_wifi_start() );                               // 启动 WiFi

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    // 等待 WiFi 连接成功或失败（阻塞直到事件组标志位被设置）
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

/**
 * @brief 连接 WiFi 的入口函数
 * 
 * 初始化 NVS、调用 wifi_init_sta()
 */
void wifi_connect(void)
{
    // 初始化 NVS（Non-Volatile Storage，用于保存 WiFi 配置等）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase()); // 如果 NVS 满了或版本不对，先擦除
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_init_sta(); // 初始化并连接 WiFi
}

// ======================================
// SNTP 时间同步相关函数
// ======================================
/**
 * @brief 初始化 SNTP 客户端
 * 
 * 配置 NTP 服务器（阿里云）
 */
static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com"); // 使用阿里云 NTP 服务器
    esp_netif_sntp_init(&config);
}

/**
 * @brief 等待时间同步完成
 * 
 * @return ESP_OK 同步成功，ESP_FAIL 同步失败
 */
static esp_err_t obtain_time(void)
{
    int retry = 0;
    const int retry_count = 10;
    // 等待时间同步（最多等待 10*2=20 秒）
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) != ESP_OK && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
    if (retry == retry_count) return ESP_FAIL; // 同步超时
    
    // 设置时区为东八区（中国标准时间）
    setenv("TZ", "CST-8", 1);
    tzset(); // 生效时区设置
    return ESP_OK;
}

// ======================================
// HTTPS 请求任务（核心功能）
// ======================================
/**
 * @brief HTTPS GET 请求任务
 * 
 * 完整流程：初始化 mbedTLS -> 连接服务器 -> TLS 握手 -> 发送请求 -> 读取响应 -> 清理资源 -> 循环
 */
static void https_get_task(void *pvParameters)
{
    char buf[512];                    // 数据收发缓冲区（512字节）
    int ret, flags, len;              // 返回值、证书验证标志、数据长度

    // mbedTLS 上下文结构体（"容器"，存放 TLS 连接的所有状态和配置）
    mbedtls_ssl_context ssl;          // SSL 上下文（核心：保存 TLS 连接状态）
    mbedtls_ssl_config conf;          // SSL 配置（加密套件、验证模式等）
    mbedtls_net_context server_fd;    // 网络上下文（socket 连接句柄）
    mbedtls_ctr_drbg_context ctr_drbg;// 确定性随机数生成器（DRBG）
    mbedtls_entropy_context entropy;  // 熵源（从 ESP32 硬件 RNG 获取随机数）

    // ======================================
    // 1. 初始化所有 mbedTLS 上下文（必须先初始化，否则有未定义行为）
    // ======================================
    mbedtls_ssl_init(&ssl);           // 初始化 SSL 上下文
    mbedtls_ssl_config_init(&conf);   // 初始化 SSL 配置
    mbedtls_ctr_drbg_init(&ctr_drbg); // 初始化随机数生成器
    mbedtls_entropy_init(&entropy);   // 初始化熵源

    // ======================================
    // 2. 给随机数生成器播种（解决 "No RNG was provided" 错误）
    // ======================================
    ESP_LOGI(TAG, "Seeding the random number generator...");
    // 参数说明：
    // &ctr_drbg: 随机数生成器上下文
    // mbedtls_entropy_func: 熵源函数（从 ESP32 硬件 RNG 读取随机数）
    // &entropy: 熵源上下文
    // NULL, 0: 额外的自定义种子（不需要，传 NULL）
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned -0x%x", -ret);
        abort(); // 播种失败，直接终止程序
    }

    // ======================================
    // 3. 绑定 ESP-IDF 内置 CA 证书包（验证服务器身份）
    // ======================================
    ESP_LOGI(TAG, "Attaching the certificate bundle...");
    ret = esp_crt_bundle_attach(&conf); // 把系统内置的 CA 证书绑定到 SSL 配置
    if(ret < 0) {
        ESP_LOGE(TAG, "esp_crt_bundle_attach returned -0x%x", -ret);
        abort();
    }

    // ======================================
    // 4. 设置 TLS 主机名（防止证书域名不匹配）
    // ======================================
    ESP_LOGI(TAG, "Setting hostname for TLS session...");
    // 检查服务器证书里的域名是否和 WEB_SERVER 一致
    if((ret = mbedtls_ssl_set_hostname(&ssl, WEB_SERVER)) != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_set_hostname returned -0x%x", -ret);
        abort();
    }

    // ======================================
    // 5. 加载 SSL 默认配置
    // ======================================
    ESP_LOGI(TAG, "Setting up the SSL/TLS structure...");
    // 参数说明：
    // &conf: SSL 配置
    // MBEDTLS_SSL_IS_CLIENT: 角色是客户端
    // MBEDTLS_SSL_TRANSPORT_STREAM: 传输层是 TCP 流
    // MBEDTLS_SSL_PRESET_DEFAULT: 使用默认安全配置
    if((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_config_defaults returned %d", ret);
        goto exit; // 加载失败，跳转到清理资源
    }

    // ======================================
    // 6. 配置证书验证模式和绑定 RNG
    // ======================================
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED); // 强制验证服务器证书（验证失败直接断开）
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg); // 把 RNG 绑定到 SSL 配置

#ifdef CONFIG_MBEDTLS_DEBUG
    mbedtls_esp_enable_debug_log(&conf, CONFIG_MBEDTLS_DEBUG_LEVEL); // 如果开启了 mbedTLS 调试，启用日志
#endif

    // ======================================
    // 7. 最终 SSL 初始化（把配置和上下文绑定）
    // ======================================
    ESP_LOGI(TAG, "Calling mbedtls_ssl_setup...");
    if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_setup returned -0x%x", -ret);
        char error_buf[200];
        memset(error_buf, 0, sizeof(error_buf));
        mbedtls_strerror(ret, error_buf, sizeof(error_buf)); // 把错误码翻译成人类可读的文字
        ESP_LOGE(TAG,"%s\n", error_buf);
        goto exit;
    }
    ESP_LOGI(TAG, "mbedtls_ssl_setup successful!");

    // ======================================
    // 8. 主循环：重复发送 HTTPS 请求
    // ======================================
    while(1) {
        // 8.1 初始化网络上下文并连接服务器
        mbedtls_net_init(&server_fd);
        ESP_LOGI(TAG, "Connecting to %s:%s...", WEB_SERVER, WEB_PORT);
        if ((ret = mbedtls_net_connect(&server_fd, WEB_SERVER, WEB_PORT, MBEDTLS_NET_PROTO_TCP)) != 0) {
            ESP_LOGE(TAG, "mbedtls_net_connect returned -%x", -ret);
            goto exit;
        }
        ESP_LOGI(TAG, "Connected.");

        // 8.2 把 SSL 上下文和网络连接绑定（SSL 读写数据会通过这个 socket）
        mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

        // 8.3 执行 TLS 握手（最关键的一步，协商加密套件和密钥）
        ESP_LOGI(TAG, "Performing the SSL/TLS handshake...");
        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            // 如果是 "等待读/写" 错误，继续重试（非阻塞 socket 的正常行为）
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                ESP_LOGE(TAG, "mbedtls_ssl_handshake returned -0x%x", -ret);
                goto exit;
            }
        }
        ESP_LOGI(TAG, "TLS handshake successful!");

        // 8.4 验证服务器证书
        ESP_LOGI(TAG, "Verifying peer X.509 certificate...");
        if ((flags = mbedtls_ssl_get_verify_result(&ssl)) != 0) {
            // flags 不为 0 表示证书验证失败（比如证书过期、域名不匹配）
            ESP_LOGW(TAG, "Failed to verify peer certificate! (flags: 0x%x)", flags);
        } else {
            ESP_LOGI(TAG, "Certificate verified.");
        }

        // 8.5 打印协商好的加密套件
        ESP_LOGI(TAG, "Cipher suite is %s", mbedtls_ssl_get_ciphersuite(&ssl));

        // 8.6 发送 HTTP GET 请求
        ESP_LOGI(TAG, "Writing HTTP request...");
        size_t written_bytes = 0;
        do {
            // 发送数据（可能一次发不完，循环直到发完）
            ret = mbedtls_ssl_write(&ssl, (const unsigned char *)REQUEST + written_bytes, strlen(REQUEST) - written_bytes);
            if (ret >= 0) {
                ESP_LOGI(TAG, "%d bytes written", ret);
                written_bytes += ret;
            } else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ) {
                // 真正的错误，跳出
                ESP_LOGE(TAG, "mbedtls_ssl_write returned -0x%x", -ret);
                goto exit;
            }
        } while(written_bytes < strlen(REQUEST));

        // 8.7 读取 HTTP 响应
        ESP_LOGI(TAG, "Reading HTTP response...");
        do {
            len = sizeof(buf) - 1;
            memset(buf, 0, sizeof(buf));
            ret = mbedtls_ssl_read(&ssl, (unsigned char *)buf, len);

            // 如果是 "等待读/写" 错误，继续重试
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }

            // 服务器发送关闭通知
            if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                ret = 0;
                break;
            }

            // 读取出错
            if (ret < 0) {
                ESP_LOGE(TAG, "mbedtls_ssl_read returned -0x%x", -ret);
                break;
            }

            // 连接关闭
            if (ret == 0) {
                ESP_LOGI(TAG, "connection closed");
                break;
            }

            // 成功读取到数据，打印到串口
            len = ret;
            ESP_LOGD(TAG, "%d bytes read", len);
            for (int i = 0; i < len; i++) {
                putchar(buf[i]);
            }
        } while(1);

        // 8.8 发送 TLS 关闭通知
        mbedtls_ssl_close_notify(&ssl);

    exit:
        // ======================================
        // 9. 清理资源（每次请求完或出错时）
        // ======================================
        mbedtls_ssl_session_reset(&ssl); // 重置 SSL 会话
        mbedtls_net_free(&server_fd);    // 关闭并释放 socket

        if (ret != 0) {
            memset(buf, 0, sizeof(buf));
            mbedtls_strerror(ret, buf, 100);
            ESP_LOGE(TAG, "Last error was: -0x%x - %s", -ret, buf);
        }

        putchar('\n');
        static int request_count;
        ESP_LOGI(TAG, "Completed %d requests", ++request_count);
        ESP_LOGI(TAG, "Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

        // 倒计时 10 秒后重新开始
        for (int countdown = 10; countdown >= 0; countdown--) {
            ESP_LOGI(TAG, "%d...", countdown);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(TAG, "Starting again!");
    }
}

// ======================================
// 主入口函数
// ======================================
void klp_https_request(void)
{
    wifi_connect();       // 连接 WiFi
    initialize_sntp();    // 初始化 SNTP
    obtain_time();        // 等待时间同步

    // 创建 HTTPS 请求任务（栈大小 16384 字节，优先级 5）
    xTaskCreate(&https_get_task, "https_get_task", 16384, NULL, 5, NULL);
    
    // 主任务死循环（不做任何事，HTTPS 请求在上面的任务里执行）
    while(1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
