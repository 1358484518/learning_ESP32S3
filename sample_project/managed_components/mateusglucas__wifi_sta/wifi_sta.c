#include "wifi_sta.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"

#define WIFI_MAXIMUM_RETRY  8

static const char *TAG = "wifi station";
static esp_netif_t * netif_instance = NULL;
static int s_retry_num = 0;

static void sta_disconnected_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);

static void sta_got_ip_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);

esp_err_t wifi_sta_init(void){
    esp_err_t err = nvs_flash_init();
    if(err!=ESP_OK){
        ESP_LOGE(TAG, "%s: fail to init nvs flash partition", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if(err == ESP_ERR_INVALID_STATE){
        ESP_LOGI(TAG, "%s: default loop already created", esp_err_to_name(err));
    }else if(err!= ESP_OK){
        ESP_LOGE(TAG, "%s: fail to create default event loop", esp_err_to_name(err));
        return err;    
    }

    err = esp_netif_init();
    if(err!=ESP_OK){
        ESP_LOGE(TAG, "%s: fail to init netif", esp_err_to_name(err));
        return err;
    }

    netif_instance = esp_netif_create_default_wifi_sta();
    if(netif_instance == NULL){
        ESP_LOGE(TAG, "null netif instance");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if(err!=ESP_OK){
        ESP_LOGE(TAG, "%s: fail to init wifi", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT,
                                                WIFI_EVENT_STA_DISCONNECTED,
                                                &sta_disconnected_handler,
                                                NULL, NULL);   
    if(err!=ESP_OK){
        ESP_LOGE(TAG, "%s: fail to register sta disconnected handler", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT,
                                                IP_EVENT_STA_GOT_IP,
                                                &sta_got_ip_handler,
                                                NULL, NULL);
    if(err!=ESP_OK){
        ESP_LOGE(TAG, "%s: fail to sta got ip handler", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if(err!=ESP_OK){
        ESP_LOGE(TAG, "%s: fail to set wifi mode to sta", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if(err!=ESP_OK){
        ESP_LOGE(TAG, "%s: fail to start wifi", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t wifi_sta_set_config(wifi_sta_config_t *sta_config){
    wifi_config_t config = {
        .sta = *sta_config,
    };

    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

esp_err_t wifi_sta_connect(){
    return esp_wifi_connect();
}

static void sta_got_ip_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data){
    assert(event_base == IP_EVENT);
    assert(event_id == IP_EVENT_STA_GOT_IP);

    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
}

static void sta_disconnected_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data){
    assert(event_base == WIFI_EVENT);
    assert(event_id == WIFI_EVENT_STA_DISCONNECTED);

    if (s_retry_num < WIFI_MAXIMUM_RETRY) {
        ESP_LOGW(TAG, "retry to connect to the AP");
        s_retry_num++;
        esp_err_t err = esp_wifi_connect();
        if(err!=ESP_OK){
            ESP_LOGW(TAG, "failed to connect: %s", esp_err_to_name(err));
        }
    }
    ESP_LOGW(TAG, "connect to the AP fail");
}

