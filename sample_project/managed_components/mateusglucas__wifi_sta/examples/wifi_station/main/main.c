#include "wifi_sta.h"

#define WIFI_SSID       "ssid"
#define WIFI_PASSWORD   "pass"

void app_main(void)
{
    wifi_sta_config_t wifi_sta_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
    };

    ESP_ERROR_CHECK(wifi_sta_init());
    ESP_ERROR_CHECK(wifi_sta_set_config(&wifi_sta_config));
    ESP_ERROR_CHECK(wifi_sta_connect());
}