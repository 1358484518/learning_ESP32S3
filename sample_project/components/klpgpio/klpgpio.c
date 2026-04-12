#include <stdio.h>
#include "klpgpio.h"
/*
 * ESP32-S3 N16R8 基础GPIO输入输出示例
 * 严格避开内部Flash/PSRAM占用引脚
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

// #include <stdio.h>
#include <string.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "unity.h"
#include <mbedtls/base64.h>
// #include "esp_log.h"
#include "driver/i2c.h"
#include "esp_timer.h"
#include "esp_camera.h"
// 日志标签
static const char *TAG = "GPIO_TEST";

// ===================== 安全GPIO定义 =====================
#define GPIO_OUTPUT_PIN     GPIO_NUM_0  // 输出引脚（绝对安全）
#define GPIO_INPUT_PIN      GPIO_NUM_1  // 输入引脚（绝对安全）
// ========================================================

/**
 * @brief GPIO初始化
 */
static void gpio_example_init(void)
{
    // 1. 配置输出引脚
    gpio_config_t output_conf = {
        .pin_bit_mask = (1ULL << GPIO_OUTPUT_PIN), // 选中引脚
        .mode = GPIO_MODE_OUTPUT,                   // 输出
        .pull_up_en = GPIO_PULLUP_DISABLE,         // 关闭上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,     // 关闭下拉
        .intr_type = GPIO_INTR_DISABLE             // 关闭中断
    };
    gpio_config(&output_conf);

    // 2. 配置输入引脚（内部上拉，按键常用）
    gpio_config_t input_conf = {
        .pin_bit_mask = (1ULL << GPIO_INPUT_PIN),  // 选中引脚
        .mode = GPIO_MODE_INPUT,                   // 输入模式
        .pull_up_en = GPIO_PULLUP_ENABLE,          // 开启内部上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,     // 关闭下拉
        .intr_type = GPIO_INTR_DISABLE             // 关闭中断
    };
    gpio_config(&input_conf);

    ESP_LOGI(TAG, "GPIO初始化完成 → 输出:GPIO0  输入:GPIO1");
}

void klpgpio(void)
{
    // 初始化GPIO
    gpio_example_init();

    bool output_level = 0;

    // 主循环
    while (1) {
        // 1. 翻转输出电平
        output_level = !output_level;
        gpio_set_level(GPIO_OUTPUT_PIN, output_level);

        // 2. 读取输入电平
        int input_level = gpio_get_level(GPIO_INPUT_PIN);

        // 3. 打印状态
        ESP_LOGI(TAG, "输出电平: %d | 输入电平: %d", output_level, input_level);
        gpio_dump_io_configuration(stdout,SOC_GPIO_VALID_GPIO_MASK);
        // 延时500ms
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

#ifdef CONFIG_IDF_TARGET_ESP32
#define BOARD_WROVER_KIT 1
#elif defined CONFIG_IDF_TARGET_ESP32S2
#define BOARD_CAMERA_MODEL_ESP32S2 1
#elif defined CONFIG_IDF_TARGET_ESP32S3
#define BOARD_CAMERA_MODEL_ESP32_S3_EYE 1
#endif

#define portTICK_RATE_MS              portTICK_PERIOD_MS

// WROVER-KIT PIN Map
#if BOARD_WROVER_KIT

#define PWDN_GPIO_NUM -1  //power down is not used
#define RESET_GPIO_NUM -1 //software reset will be performed
#define XCLK_GPIO_NUM 21
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 19
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 5
#define Y2_GPIO_NUM 4
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// ESP32Cam (AiThinker) PIN Map
#elif BOARD_ESP32CAM_AITHINKER

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1 //software reset will be performed
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#elif BOARD_CAMERA_MODEL_ESP32S2

#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1

#define VSYNC_GPIO_NUM    21
#define HREF_GPIO_NUM     38
#define PCLK_GPIO_NUM     11
#define XCLK_GPIO_NUM     40

#define SIOD_GPIO_NUM     17
#define SIOC_GPIO_NUM     18

#define Y9_GPIO_NUM       39
#define Y8_GPIO_NUM       41
#define Y7_GPIO_NUM       42
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       3
#define Y4_GPIO_NUM       14
#define Y3_GPIO_NUM       37
#define Y2_GPIO_NUM       13

#elif BOARD_CAMERA_MODEL_ESP32_S3_EYE

#define PWDN_GPIO_NUM     43
#define RESET_GPIO_NUM    44

#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13
#define XCLK_GPIO_NUM     15

#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       11
#define Y4_GPIO_NUM       10
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       8

#endif

#define I2C_MASTER_SCL_IO           4      /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           5      /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              0      /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ          100000 /*!< I2C master clock frequency */

// static const char *TAG = "test camera";

typedef void (*decode_func_t)(uint8_t *jpegbuffer, uint32_t size, uint8_t *outbuffer);

static esp_err_t init_camera(uint32_t xclk_freq_hz, pixformat_t pixel_format, 
    framesize_t frame_size, uint8_t fb_count, int sccb_sda_gpio_num, int sccb_port)
{
    framesize_t size_bak = frame_size;
    if (PIXFORMAT_JPEG == pixel_format && FRAMESIZE_SVGA > frame_size) {
        frame_size = FRAMESIZE_HD;
    }
    camera_config_t camera_config = {
        .pin_pwdn = PWDN_GPIO_NUM,
        .pin_reset = RESET_GPIO_NUM,
        .pin_xclk = XCLK_GPIO_NUM,
        .pin_sccb_sda = sccb_sda_gpio_num, // If pin_sccb_sda is -1, sccb will use the already initialized i2c port specified by `sccb_i2c_port`.
        .pin_sccb_scl = SIOC_GPIO_NUM,
        .sccb_i2c_port = sccb_port,

        .pin_d7 = Y9_GPIO_NUM,
        .pin_d6 = Y8_GPIO_NUM,
        .pin_d5 = Y7_GPIO_NUM,
        .pin_d4 = Y6_GPIO_NUM,
        .pin_d3 = Y5_GPIO_NUM,
        .pin_d2 = Y4_GPIO_NUM,
        .pin_d1 = Y3_GPIO_NUM,
        .pin_d0 = Y2_GPIO_NUM,
        .pin_vsync = VSYNC_GPIO_NUM,
        .pin_href = HREF_GPIO_NUM,
        .pin_pclk = PCLK_GPIO_NUM,

        .xclk_freq_hz = xclk_freq_hz,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = pixel_format, //YUV422,GRAYSCALE,RGB565,JPEG
        .frame_size = frame_size,    //QQVGA-UXGAQQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

        .jpeg_quality = 12, //0-63, for OV series camera sensors, lower number means higher quality
        .fb_count = fb_count,       //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY
    };

    //initialize the camera
    esp_err_t ret = esp_camera_init(&camera_config);

    if (ESP_OK == ret && PIXFORMAT_JPEG == pixel_format && FRAMESIZE_SVGA > size_bak) {
        sensor_t *s = esp_camera_sensor_get();
        s->set_framesize(s, size_bak);
    }

    return ret;
}

static bool camera_test_fps(uint16_t times, float *fps, uint32_t *size)
{
    *fps = 0.0f;
    *size = 0;
    uint32_t s = 0;
    uint32_t num = 0;
    uint64_t total_time = esp_timer_get_time();
    for (size_t i = 0; i < times; i++) {
        camera_fb_t *pic = esp_camera_fb_get();
        if (NULL == pic) {
            ESP_LOGW(TAG, "fb get failed");
            return 0;
        } else {
            s += pic->len;
            num++;
        }
        esp_camera_fb_return(pic);
    }
    total_time = esp_timer_get_time() - total_time;
    if (num) {
        *fps = num * 1000000.0f / total_time ;
        *size = s / num;
    }
    return 1;
}

static const char *get_cam_format_name(pixformat_t pixel_format)
{
    switch (pixel_format) {
    case PIXFORMAT_JPEG: return "JPEG";
    case PIXFORMAT_RGB565: return "RGB565";
    case PIXFORMAT_RGB888: return "RGB888";
    case PIXFORMAT_YUV422: return "YUV422";
    default:
        break;
    }
    return "UNKNOWN";
}

static void printf_img_base64(const camera_fb_t *pic)
{
    uint8_t *outbuffer = NULL;
    size_t outsize = 0;
    if (PIXFORMAT_JPEG != pic->format) {
        fmt2jpg(pic->buf, pic->width * pic->height * 2, pic->width, pic->height, pic->format, 50, &outbuffer, &outsize);
    } else {
        outbuffer = pic->buf;
        outsize = pic->len;
    }

    uint8_t *base64_buf = calloc(1, outsize * 4);
    if (NULL != base64_buf) {
        size_t out_len = 0;
        mbedtls_base64_encode(base64_buf, outsize * 4, &out_len, outbuffer, outsize);
        printf("%s\n", base64_buf);
        free(base64_buf);
        if (PIXFORMAT_JPEG != pic->format) {
            free(outbuffer);
        }
    } else {
        ESP_LOGE(TAG, "malloc for base64 buffer failed");
    }
}
