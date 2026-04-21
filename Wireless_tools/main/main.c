#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "klpapsta.h"
#include "klphttpserver.h"
void app_main(void)
{
    klp_wifi_ap_sta();
    klp_http_server();
    while (1){

        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI("main", "Hello World!");
        
    }
 
}
