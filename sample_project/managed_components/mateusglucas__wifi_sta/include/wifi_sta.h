#pragma once

#include "esp_err.h"
#include "esp_wifi.h"

/**
 * @brief Initialize Wi-Fi station
 * @return ESP_OK on success, otherwise ESP_ERR code
 */
esp_err_t wifi_sta_init(void);

/**
 * @brief Set Wi-Fi station cofigurations
 * @param sta_config Wi-Fi station configurations
 * @return ESP_OK on success, otherwise ESP_ERR code
 */
esp_err_t wifi_sta_set_config(wifi_sta_config_t *sta_config);

/**
 * @brief Connect to Wi-Fi
 * @return ESP_OK on success, otherwise ESP_ERR code
 */
esp_err_t wifi_sta_connect(void);