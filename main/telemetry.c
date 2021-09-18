#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"


#include "telemetry.h"
#include "accelerometer.h"
#include "ow_events.h"

#define PARTITION_TYPE 64
#define PARTITION_SUBTYPE 0
#define PARTITION_LABEL "storage"

static const char* TAG = "tm";
static TaskHandle_t sending_handle;

static void on_got_buffer(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    accel_buffer_dto_t* typed_event_data = event_data;
    // save to flash
    
    // if |head - tail| < ?? xTaskNotifyGive()
    // xTaskNotifyGive()
}


static void sending_task_function(void* args){
    // todo loop : wait (ulTaskNotifyTake) and send
    while(1){
        ulTaskNotifyTake(0, portMAX_DELAY);
    }
}

void telemetry_init(void){
    esp_partition_iterator_t storage_iter = esp_partition_find(PARTITION_TYPE, PARTITION_SUBTYPE, PARTITION_LABEL);
    if (storage_iter == NULL){
        ESP_LOGE(TAG, "did not find partition table for storing buffers");
        abort();
    }
    esp_partition_t * storage_info = esp_partition_get(storage_iter);
    esp_partition_iterator_release(storage_iter);
    esp_partition_erase_range(storage_info, 0, storage_info -> size);
    ESP_LOGI(TAG, "Initialized erased storage, size is %zu", storage_info -> size);
    
    xTaskCreate(sending_task_function, "sending_task", 5*configMINIMAL_STACK_SIZE, NULL, 5, &sending_handle);

    ESP_ERROR_CHECK(esp_event_handler_register_with(accel_event_loop, OW_EVENT, OW_EVENT_ON_ACCEL_BUFFER, &on_got_buffer, NULL));
}