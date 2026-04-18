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
#define EXAMPLE_ESP_WIFI_SSID      "klp123456"
#define EXAMPLE_ESP_WIFI_PASS      "18902101360"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5//重连接次数

//WPA3 加密 才需要选，普通家用路由器都是 WPA2，完全不用管，不用 WPA3 → 直接选 BOTH 就行
// 第一部分 WPA3：选兼容模式
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER ""
// 第二部分 认证阈值：选 WPA2 （家用最常用）
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
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

// 网页 HTML 代码（已转义，直接用于 ESP32 程序）
const char ESP32_HTML_PAGE[] = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>ESP32 HTTP 测试</title>
</head>
<body>
    <h1>ESP32 HTTP Server 测试</h1>

    <button onclick="getHello()">测试 /hello</button>
    <p id="hello_result"></p>
    <hr>

    发送内容到 /echo：<br>
    <input type="text" id="echo_msg" value="你好ESP32">
    <button onclick="postEcho()">发送</button>
    <p id="echo_result"></p>
    <hr>

    发送数字到 /adder 累加：<br>
    <input type="number" id="add_num" value="5">
    <button onclick="postAdder()">累加</button>
    <p id="adder_result"></p>

<script>
const ip = "192.168.1.9";
const port = "80";
const base = `http://${ip}:${port}`;

// 测试 /hello
async function getHello() {
    let res = await fetch(base + "/hello");//发送到hello接口
    let txt = await res.text();
    document.getElementById("hello_result").innerText = "返回：" + txt;
}

// 测试 /echo
async function postEcho() {
    let msg = document.getElementById("echo_msg").value;
    let res = await fetch(base + "/echo", {
        method: "POST",
        body: msg
    });
    let txt = await res.text();
    document.getElementById("echo_result").innerText = "返回：" + txt;
}

// 测试 /adder
async function postAdder() {
    let num = document.getElementById("add_num").value;
    let res = await fetch(base + "/adder", {
        method: "POST",
        body: num
    });
    let txt = await res.text();
    document.getElementById("adder_result").innerText = "当前总和：" + txt;
}
</script>

</body>
</html>
)HTML";

static esp_err_t klp_get_handler(httpd_req_t *req)
{

    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_send(req, ESP32_HTML_PAGE, HTTPD_RESP_USE_STRLEN);
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

void wifi_init_sta(void)
{
    static httpd_handle_t server = NULL;
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    //注册其他事件监听
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, 
        IP_EVENT_STA_GOT_IP, 
        &connect_handler, 
        &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
         WIFI_EVENT_STA_DISCONNECTED, 
         &disconnect_handler, 
         &server));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
#ifdef CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
            .disable_wpa3_compatible_mode = 0,
#endif
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}


void wifi_connect(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        /* If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
         * and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. */
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
}


void klp_http_server(void)
{
    
    wifi_connect();//wifi连接
    /* Start the server for the first time */
    // server = start_tests();
    while (1)
    {
        /* code */
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
}
