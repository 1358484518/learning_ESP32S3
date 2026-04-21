#include <stdio.h>
#include "klpcamera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static TaskHandle_t klp_camera_task_Handle;


/**
 * @brief 发送“请挂起”的通知给摄像头任务（在其他任务中调用）
 */
static void klp_camera_task_suspend_signal(void) {
    if (klp_camera_task_Handle != NULL) {
        xTaskNotifyGive(klp_camera_task_Handle);
    }
}

/**
 * @brief 检查并挂起摄像头任务（必须在摄像头任务自身的循环中调用）
 */
void klp_camera_task_check_and_suspend(void) {
    // 1. 非阻塞检查通知（pdTRUE：收到后清零）
    uint32_t ulNotificationValue = ulTaskNotifyTake(pdTRUE, 0);

    // 2. 判断是否收到通知（返回值 > 0 表示收到）
    if (ulNotificationValue > 0) {
        // 3. 挂起自己（用 NULL 明确表示挂起当前任务）
        ESP_LOGI("CAMERA", "Camera task suspending...");
        vTaskSuspend(NULL);
        // 被 vTaskResume() 唤醒后会继续执行
        ESP_LOGI("CAMERA", "Camera task resumed");
    }
}
static void klp_camera_task(void *arg)
{
    while (1)
    {

        vTaskDelay(pdMS_TO_TICKS(1000));
        klp_camera_task_suspend_signal();
    }
}

void klp_camera(void)
{
    BaseType_t klp_camera_handle = xTaskCreate(klp_camera_task, 
        "klp_camera_task", 4096, NULL, 5, &klp_camera_task_Handle);
}
