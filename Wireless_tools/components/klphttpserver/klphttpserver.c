#include <stdio.h>
#include "klphttpserver.h"
#include <esp_http_server.h>
#include "sdkconfig.h"
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>            // struct addrinfo
#include <arpa/inet.h>
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
// #include "lwip/err.h"
// #include "lwip/sockets.h"
// #include "lwip/sys.h"
// #include <lwip/netdb.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"  
// #if defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
// #include "addr_from_stdin.h"
// #endif

// #if defined(CONFIG_EXAMPLE_IPV4)
#define HOST_IP_ADDR "192.168.1.5"//CONFIG_EXAMPLE_IPV4_ADDR
// #elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
// #define HOST_IP_ADDR ""
// #endif

// #define PORT 6000//CONFIG_EXAMPLE_PORT

static const char *TAG = "klptcpserver";
// static const char *payload = "Message from ESP32 ";

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
// #define EXAMPLE_ESP_WIFI_SSID      "klp123456"
// #define EXAMPLE_ESP_WIFI_PASS      "18902101360"
// #define EXAMPLE_ESP_MAXIMUM_RETRY  5//重连接次数

//WPA3 加密 才需要选，普通家用路由器都是 WPA2，完全不用管，不用 WPA3 → 直接选 BOTH 就行
// 第一部分 WPA3：选兼容模式
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER ""
// 第二部分 认证阈值：选 WPA2 （家用最常用）
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK

/* FreeRTOS event group to signal when we are connected*/
// static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// static int s_retry_num = 0;


static int pre_start_mem, post_stop_mem;

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

/********************* Basic Handlers Start *******************/

static esp_err_t hello_get_handler(httpd_req_t *req)
{
#define STR "Hello World html!"
    ESP_LOGI(TAG, "Free Stack for server task: '%d'", uxTaskGetStackHighWaterMark(NULL));
    httpd_resp_send(req, STR, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
#undef STR
}

/* This handler is intended to check what happens in case of empty values of headers.
 * Here `Header2` is an empty header and `Header1` and `Header3` will have `Value1`
 * and `Value3` in them. */
static esp_err_t test_header_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    int buf_len;
    char *buf;

    buf_len = httpd_req_get_hdr_value_len(req, "Header1");
    if (buf_len > 0) {
        buf = malloc(++buf_len);
        if (!buf) {
            ESP_LOGE(TAG, "Failed to allocate memory of %d bytes!", buf_len);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
            return ESP_ERR_NO_MEM;
        }
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Header1", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Header1 content: %s", buf);
            if (strcmp("Value1", buf) != 0) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wrong value of Header1 received");
                free(buf);
                return ESP_ERR_INVALID_ARG;
            } else {
                ESP_LOGI(TAG, "Expected value and received value matched for Header1");
            }
        } else {
            ESP_LOGE(TAG, "Error in getting value of Header1");
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Error in getting value of Header1");
            free(buf);
            return ESP_FAIL;
        }
        free(buf);
    } else {
        ESP_LOGE(TAG, "Header1 not found");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Header1 not found");
        return ESP_ERR_NOT_FOUND;
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Header3");
    if (buf_len > 0) {
        buf = malloc(++buf_len);
        if (!buf) {
            ESP_LOGE(TAG, "Failed to allocate memory of %d bytes!", buf_len);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
            return ESP_ERR_NO_MEM;
        }
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Header3", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Header3 content: %s", buf);
            if (strcmp("Value3", buf) != 0) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wrong value of Header3 received");
                free(buf);
                return ESP_ERR_INVALID_ARG;
            } else {
                ESP_LOGI(TAG, "Expected value and received value matched for Header3");
            }
        } else {
            ESP_LOGE(TAG, "Error in getting value of Header3");
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Error in getting value of Header3");
            free(buf);
            return ESP_FAIL;
        }
        free(buf);
    } else {
        ESP_LOGE(TAG, "Header3 not found");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Header3 not found");
        return ESP_ERR_NOT_FOUND;
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Header2");
    buf = malloc(++buf_len);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate memory of %d bytes!", buf_len);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    if (httpd_req_get_hdr_value_str(req, "Header2", buf, buf_len) == ESP_OK) {
        ESP_LOGI(TAG, "Header2 content: %s", buf);
        httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    } else {
        ESP_LOGE(TAG, "Header2 not found");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Header2 not found");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t hello_type_get_handler(httpd_req_t *req)
{
#define STR "Hello World!"
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_send(req, STR, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
#undef STR
}

static esp_err_t hello_status_get_handler(httpd_req_t *req)
{
#define STR "Hello World!"
    httpd_resp_set_status(req, HTTPD_500);
    httpd_resp_send(req, STR, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
#undef STR
}

static esp_err_t echo_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/echo handler read content length %d", req->content_len);

    char*  buf = malloc(req->content_len + 1);
    size_t off = 0;
    int    ret;

    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate memory of %d bytes!", req->content_len + 1);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    while (off < req->content_len) {
        /* Read data received in the request */
        ret = httpd_req_recv(req, buf + off, req->content_len - off);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            free (buf);
            return ESP_FAIL;
        }
        off += ret;
        ESP_LOGI(TAG, "/echo handler recv length %d", ret);
    }
    buf[off] = '\0';

    if (req->content_len < 128) {
        ESP_LOGI(TAG, "/echo handler read %s", buf);
    }

    /* Search for Custom header field */
    char*  req_hdr = 0;
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Custom");
    if (hdr_len) {
        /* Read Custom header value */
        req_hdr = malloc(hdr_len + 1);
        if (!req_hdr) {
            ESP_LOGE(TAG, "Failed to allocate memory of %d bytes!", hdr_len + 1);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        httpd_req_get_hdr_value_str(req, "Custom", req_hdr, hdr_len + 1);

        /* Set as additional header for response packet */
        httpd_resp_set_hdr(req, "Custom", req_hdr);
    }
    httpd_resp_send(req, buf, req->content_len);
    free (req_hdr);
    free (buf);
    return ESP_OK;
}

static void adder_free_func(void *ctx)
{
    ESP_LOGI(TAG, "Custom Free Context function called");
    free(ctx);
}

/* Create a context, keep incrementing value in the context, by whatever was
 * received. Return the result
 */
static esp_err_t adder_post_handler(httpd_req_t *req)
{
    char buf[10];
    char outbuf[50];
    int  ret;

    /* Read data received in the request */
    ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    int val = atoi(buf);
    ESP_LOGI(TAG, "/adder handler read %d", val);

    if (! req->sess_ctx) {
        ESP_LOGI(TAG, "/adder allocating new session");
        req->sess_ctx = malloc(sizeof(int));
        ESP_RETURN_ON_FALSE(req->sess_ctx, ESP_ERR_NO_MEM, TAG, "Failed to allocate sess_ctx");
        req->free_ctx = adder_free_func;
        *(int *)req->sess_ctx = 0;
    }
    int *adder = (int *)req->sess_ctx;
    *adder += val;

    snprintf(outbuf, sizeof(outbuf),"%d", *adder);
    httpd_resp_send(req, outbuf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t leftover_data_post_handler(httpd_req_t *req)
{
    /* Only echo the first 10 bytes of the request, leaving the rest of the
     * request data as is.
     */
    char buf[11];
    int  ret;

    /* Read data received in the request */
    ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    ESP_LOGI(TAG, "leftover data handler read %s", buf);
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void generate_async_resp(void *arg)
{
    char buf[250];
    struct async_resp_arg *resp_arg = (struct async_resp_arg *)arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
#define HTTPD_HDR_STR      "HTTP/1.1 200 OK\r\n"                   \
                           "Content-Type: text/html\r\n"           \
                           "Content-Length: %d\r\n"
#define STR "Hello Double World!"

    ESP_LOGI(TAG, "Executing queued work fd : %d", fd);

    snprintf(buf, sizeof(buf), HTTPD_HDR_STR,
         strlen(STR));
    httpd_socket_send(hd, fd, buf, strlen(buf), 0);
    /* Space for sending additional headers based on set_header */
    httpd_socket_send(hd, fd, "\r\n", strlen("\r\n"), 0);
    httpd_socket_send(hd, fd, STR, strlen(STR), 0);
#undef STR
    free(arg);
}

static esp_err_t async_get_handler(httpd_req_t *req)
{
#define STR "Hello World!"
    httpd_resp_send(req, STR, HTTPD_RESP_USE_STRLEN);
    /* Also register a HTTPD Work which sends the same data on the same
     * socket again
     */
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    ESP_RETURN_ON_FALSE(resp_arg, ESP_ERR_NO_MEM, TAG, "Failed to allocate resp_arg");
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    if (resp_arg->fd < 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Queuing work fd : %d", resp_arg->fd);
    httpd_queue_work(req->handle, generate_async_resp, resp_arg);
    return ESP_OK;
#undef STR
}
// 网页模板（动态插入当前WiFi信息 | 密码明文显示）
const char HTML_TEMPLATE[] = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>ESP32 WiFi配置</title>
    <style>
        body{font-family:Arial;margin:30px;}
        .wifi-box{margin:20px 0;padding:15px;border:1px solid #ccc;}
        input{margin:5px 0;padding:8px;width:250px;}
        button{padding:8px 16px;background:#007bff;color:#fff;border:none;cursor:pointer;}
        .info{color:green;font-size:16px;}
    </style>
</head>
<body>
    <h1>ESP32 WiFi 配置工具</h1>

    <div class="wifi-box">
        <h3>当前已配置WiFi</h3>
        <p class="info">WiFi名称：%s</p>
        <p class="info">WiFi密码：%s</p>
    </div>

    <div class="wifi-box">
        <h3>修改WiFi配置</h3>
        <input type="text" id="ssid" placeholder="WiFi名称"><br>
        <!-- 明文密码输入框 -->
        <input type="text" id="password" placeholder="WiFi密码"><br>
        <button onclick="saveWifi()">提交并重启</button>
        <p id="tip" style="color:red;"></p>
    </div>

    <hr>
    <button onclick="getHello()">测试 /hello</button>
    <p id="hello_result"></p>

<script>
const base = window.location.origin;
async function getHello(){fetch(base+"/hello").then(r=>r.text()).then(t=>document.getElementById("hello_result").innerText="返回："+t);}
async function saveWifi(){
    let ssid=document.getElementById("ssid").value.trim();
    let pwd=document.getElementById("password").value.trim();
    if(!ssid){document.getElementById("tip").innerText="请输入WiFi名称！";return;}
    document.getElementById("tip").innerText="保存中...";
    fetch("/wifi_config",{
        method:"POST",
        headers:{"Content-Type":"application/x-www-form-urlencoded"},
        body:`ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(pwd)}`
    }).then(r=>r.text()).then(t=>document.getElementById("tip").innerText=t);
}
</script>
</body>
</html>
)HTML";



// 保存WiFi信息到NVS
static esp_err_t save_wifi_to_nvs(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    // 保存SSID和密码
    nvs_set_str(nvs_handle, "sta_ssid", ssid);
    nvs_set_str(nvs_handle, "sta_pwd", password);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return ESP_OK;
}
// 从NVS读取当前WiFi配置
static void get_wifi_from_nvs(char *ssid, size_t ssid_len, char *pwd, size_t pwd_len)
{
    // 默认值
    strncpy(ssid, "未配置", ssid_len);
    strncpy(pwd, "未配置", pwd_len);

    nvs_handle_t nvs_handle;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_str(nvs_handle, "sta_ssid", ssid, &ssid_len);
        nvs_get_str(nvs_handle, "sta_pwd", pwd, &pwd_len);
        nvs_close(nvs_handle);
    }
}

// 延时重启任务（保证HTTP响应发送完成后再重启）
static void restart_task(void *pvParameters)
{
    // 延时 100ms，确保网页响应发送到浏览器
    vTaskDelay(pdMS_TO_TICKS(1000));
    // 重启整个芯片（硬件级重启，100%生效）
    // 函数 esp_restart() 用于执行芯片的软件复位。调用此函数时，程序停止执行，两个 CPU 均复位，应用程序由引导加载程序加载并重启。
    // 函数 esp_register_shutdown_handler() 用于注册复位前会自动调用的例程（复位过程由 esp_restart() 函数触发），这与 atexit POSIX 函数的功能类似。
    esp_restart();
    // 重启后不会执行到这里
    vTaskDelete(NULL);
}

static esp_err_t wifi_config_post_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "接收数据失败");
        return ESP_FAIL;
    }

    char ssid[32] = {0};
    char password[64] = {0};

    // 解析表单数据 ssid=xxx&password=yyy
    char *ssid_start = strstr(buf, "ssid=");
    char *pwd_start = strstr(buf, "password=");
    if (ssid_start) {
        ssid_start += 5;
        char *end = strchr(ssid_start, '&');
        if (!end) end = buf + strlen(buf);
        strncpy(ssid, ssid_start, end - ssid_start);
    }
    if (pwd_start) {
        pwd_start += 9;
        strncpy(password, pwd_start, sizeof(password)-1);
    }

    ESP_LOGI(TAG, "收到新WiFi配置：SSID=%s, PASSWORD=%s", ssid, password);

    // 保存到NVS
    if (save_wifi_to_nvs(ssid, password) == ESP_OK) {
        httpd_resp_send(req, "配置保存成功，设备正在重启...", HTTPD_RESP_USE_STRLEN);
        
        // 🔥 核心修复：创建任务延时重启，不再直接调用
        xTaskCreate(restart_task, "restart_task", 1024, NULL, 1, NULL);
    } else {
        httpd_resp_send(req, "保存失败", HTTPD_RESP_USE_STRLEN);
    }

    return ESP_OK;
}

static esp_err_t klp_get_handler(httpd_req_t *req)
{
    // 读取当前保存的WiFi信息
    char curr_ssid[32] = {0};
    char curr_pwd[64] = {0};
    get_wifi_from_nvs(curr_ssid, sizeof(curr_ssid), curr_pwd, sizeof(curr_pwd));

    // 动态分配足够大的内存（4096字节，适配网页大小）
    const size_t buf_size = 4096;
    char *html_buf = (char *)malloc(buf_size);
    // 内存分配失败处理
    if (html_buf == NULL) {
        ESP_LOGE(TAG, "HTML buffer malloc failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory Allocate Failed");
        return ESP_ERR_NO_MEM;
    }

    // 动态生成网页内容
    snprintf(html_buf, buf_size, HTML_TEMPLATE, curr_ssid, curr_pwd);

    // 发送HTTP响应
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_buf, HTTPD_RESP_USE_STRLEN);

    // 🔥 关键：释放动态分配的内存，防止内存泄漏
    free(html_buf);
    // 释放后置空，养成好习惯
    html_buf = NULL;

    return ESP_OK;
}

static const httpd_uri_t basic_handlers[] = {
    { .uri      = "/",
      .method   = HTTP_GET,
      .handler  = klp_get_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/hello/type_html",
      .method   = HTTP_GET,
      .handler  = hello_type_get_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/test_header",
      .method   = HTTP_GET,
      .handler  = test_header_get_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/hello",
      .method   = HTTP_GET,
      .handler  = hello_get_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/hello/status_500",
      .method   = HTTP_GET,
      .handler  = hello_status_get_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/echo",
      .method   = HTTP_POST,
      .handler  = echo_post_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/echo",
      .method   = HTTP_PUT,
      .handler  = echo_post_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/leftover_data",
      .method   = HTTP_POST,
      .handler  = leftover_data_post_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/adder",
      .method   = HTTP_POST,
      .handler  = adder_post_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/async_data",
      .method   = HTTP_GET,
      .handler  = async_get_handler,
      .user_ctx = NULL,
    },
     // ===================== 新增WiFi配置接口 =====================
    { .uri      = "/wifi_config",
      .method   = HTTP_POST,
      .handler  = wifi_config_post_handler,
      .user_ctx = NULL,
    }
};

static const int basic_handlers_no = sizeof(basic_handlers)/sizeof(httpd_uri_t);
//注册uri请求接口
static void register_basic_handlers(httpd_handle_t hd)
{
    int i;
    ESP_LOGI(TAG, "Registering basic handlers");
    ESP_LOGI(TAG, "No of handlers = %d", basic_handlers_no);
    for (i = 0; i < basic_handlers_no; i++) {
        if (httpd_register_uri_handler(hd, &basic_handlers[i]) != ESP_OK) {
            ESP_LOGW(TAG, "register uri failed for %d", i);
            return;
        }
    }
    ESP_LOGI(TAG, "Success");
}
//测试http服务器
static httpd_handle_t test_httpd_start(void)
{
    pre_start_mem = esp_get_free_heap_size();
    httpd_handle_t hd;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* Modify this setting to match the number of test URI handlers */
    config.max_uri_handlers  = basic_handlers_no;
    config.server_port = 80;

    /* This check should be a part of http_server */
    config.max_open_sockets = (CONFIG_LWIP_MAX_SOCKETS - 3);
    //创建http服务器，打印相关参数
    if (httpd_start(&hd, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Started HTTP server on port: '%d'", config.server_port);
        ESP_LOGI(TAG, "Max URI handlers: '%d'", config.max_uri_handlers);
        ESP_LOGI(TAG, "Max Open Sessions: '%d'", config.max_open_sockets);
        ESP_LOGI(TAG, "Max Header Length: '%d'", CONFIG_HTTPD_MAX_REQ_HDR_LEN);
        ESP_LOGI(TAG, "Max URI Length: '%d'", CONFIG_HTTPD_MAX_URI_LEN);
        ESP_LOGI(TAG, "Max Stack Size: '%d'", config.stack_size);
        return hd;
    }
    return NULL;
}

static void test_httpd_stop(httpd_handle_t hd)
{
    httpd_stop(hd);
    post_stop_mem = esp_get_free_heap_size();
    ESP_LOGI(TAG, "HTTPD Stop: Current free memory: %d", post_stop_mem);
}

httpd_handle_t start_tests(void)
{
    httpd_handle_t hd = test_httpd_start();
    if (hd) {
        register_basic_handlers(hd);
    }
    return hd;
}

void stop_tests(httpd_handle_t hd)
{
    ESP_LOGI(TAG, "Stopping httpd");
    test_httpd_stop(hd);
}
static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_tests(*server);
        *server = NULL;
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_tests();
    }
}


void klp_http_server(void)
{
    static httpd_handle_t server = NULL;
        //注册其他事件监听
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, 
        IP_EVENT_STA_GOT_IP, 
        &connect_handler, 
        &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
         WIFI_EVENT_STA_DISCONNECTED, 
         &disconnect_handler, 
         &server));
             // ===================== 【新增：AP 模式事件监听】 =====================
    // AP 模式：有设备连接分配IP → 启动HTTP
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &connect_handler, &server));
    // AP 模式：AP停止 → 关闭HTTP
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STOP, &disconnect_handler, &server));
    // ==================================================================


}
