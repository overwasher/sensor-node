#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "telemetry.h"
#include "accelerometer.h"
#include "ow_events.h"
#include "overwatcher_communicator.h"

#define PARTITION_TYPE 64
#define PARTITION_SUBTYPE 0
#define PARTITION_LABEL "storage"

#define RESERVED_SPACE 30 // number of buffers for which space is left before sending task starts.
#define BUFFER_ALIGNMENT 1024 // buffers are aligned to 1024 bytes

static const char* TAG = "tm";
static TaskHandle_t sending_handle;

volatile static size_t head;
volatile static size_t tail;
static esp_partition_t * storage_info;
// static int64_t * timestamps;
static int32_t NUMBER_OF_BUFFERS;

static void on_got_buffer(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    accel_buffer_dto_t* typed_event_data = event_data;
    esp_partition_write(storage_info, tail, typed_event_data->buffer, typed_event_data -> buffer_count * sizeof(*typed_event_data -> buffer));

    size_t local_tail = tail;
    size_t local_head = head;
    local_tail += BUFFER_ALIGNMENT;
    if (local_tail >= storage_info->size){
        local_tail = 0;
    }
    if (local_tail == local_head){
        ESP_LOGE(TAG, "got buffer and flash memory is full");
    }
    tail = local_tail;
    ESP_LOGI(TAG, "saved buffer and increased tail to %zu", local_tail);
    int32_t buffers_count = ((local_tail + storage_info->size - local_head) % storage_info->size) / BUFFER_ALIGNMENT;
    ESP_LOGI(TAG, "current buffers count %u", buffers_count);
    if (buffers_count == NUMBER_OF_BUFFERS - RESERVED_SPACE){
        xTaskNotifyGive(sending_handle);
    }
}


static void sending_task_function(void* args){
    // todo loop : wait (ulTaskNotifyTake) and send
    while(1){
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        spi_flash_mmap_handle_t storage_handle;
        void * data;
        esp_partition_mmap(storage_info, 0, storage_info->size, SPI_FLASH_MMAP_DATA, &data, &storage_handle);
        size_t local_tail = tail;
        send_telemetry(data, storage_info->size, head, local_tail);
        ESP_LOGI(TAG, "sent buffers and changes head from %zu to %zu", head, local_tail);
        head = local_tail;
        spi_flash_munmap(storage_handle);
    }
}

void telemetry_init(void){
    esp_partition_iterator_t storage_iter = esp_partition_find(PARTITION_TYPE, PARTITION_SUBTYPE, PARTITION_LABEL);
    // записывать timestampы просто в памяти, но потом
    if (storage_iter == NULL){
        ESP_LOGE(TAG, "did not find partition table for storing buffers");
        abort();
    }
    storage_info = esp_partition_get(storage_iter);
    esp_partition_iterator_release(storage_iter);
    esp_partition_erase_range(storage_info, 0, storage_info -> size);
    ESP_LOGI(TAG, "Initialized erased storage, size is %zu", storage_info -> size);
    
    NUMBER_OF_BUFFERS = storage_info -> size / BUFFER_ALIGNMENT;
    head = (esp_random() % NUMBER_OF_BUFFERS) * BUFFER_ALIGNMENT;
    // tail = head;
    tail = (head - 35*BUFFER_ALIGNMENT + storage_info -> size) % storage_info -> size;

    xTaskCreate(sending_task_function, "sending_task", 5*configMINIMAL_STACK_SIZE, NULL, 5, &sending_handle);

    ESP_ERROR_CHECK(esp_event_handler_register_with(accel_event_loop, OW_EVENT, OW_EVENT_ON_ACCEL_BUFFER, &on_got_buffer, NULL));
}