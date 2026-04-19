#include <stdio.h>
#include "klplcd.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "esp_lcd_st7735.h"
#include "driver/gpio.h"

// ==================== 你的硬件引脚 ====================
#define LCD_SCLK_PIN    4
#define LCD_MOSI_PIN    5
#define LCD_CS_PIN      7
#define LCD_DC_PIN      6
#define LCD_RST_PIN     15
#define LCD_BL_PIN      16

#define LCD_H_RES       128
#define LCD_V_RES       160
#define LCD_BIT_PER_PIXEL 16
// ======================================================

static const char *TAG = "ST7735";
esp_lcd_panel_handle_t panel_handle = NULL;

// 全屏单色填充
static void lcd_fill_color(uint16_t color)
{
    uint16_t line_buf[LCD_H_RES];
    for (int i = 0; i < LCD_H_RES; i++) {
        line_buf[i] = color;
    }
    for (int y = 0; y < LCD_V_RES; y++) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LCD_H_RES, y+1, line_buf);
    }
}

void st7735_lcd_init(void)
{
    // 1. 初始化SPI总线
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_SCLK_PIN,
        .mosi_io_num = LCD_MOSI_PIN,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // 2. 初始化LCD IO
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_DC_PIN,
        .cs_gpio_num = LCD_CS_PIN,
        .pclk_hz = 30 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &io_handle));

    // 3. 初始化ST7735
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST_PIN,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7735(io_handle, &panel_cfg, &panel_handle));

    // 4. 屏幕初始化
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 2, 1));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    // ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));

    // 背光初始化
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << LCD_BL_PIN,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_BL_PIN, 1);

    ESP_LOGI(TAG, "ST7735 初始化成功!");
}

// 颜色测试（100%精准匹配你的屏幕）
void lcd_test(void)
{
    // 你想要的顺序：红色 → 蓝色 → 绿色 → 白色 → 黑色
    ESP_LOGI(TAG, "红色");
    lcd_fill_color(0xF800);   // 👈 实测显示红色
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "蓝色");
    lcd_fill_color(0x07E0);   // 👈 实测显示蓝色
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "绿色");
    lcd_fill_color(0x001F);   // 👈 实测显示绿色
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "白色");
    lcd_fill_color(0xFFFF);   // 正常
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "黑色");
    lcd_fill_color(0x0000);   // 正常
    vTaskDelay(pdMS_TO_TICKS(5000));
}

void klp_lcd(void)
{
    st7735_lcd_init();

    while (1) {
        lcd_test();
    }
}
