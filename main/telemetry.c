#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "string.h"

#include "telemetry.h"
#include "accelerometer.h"
#include "ow_events.h"
#include "overwatcher_communicator.h"


#define TELEMETRY_USE_FLASH

#ifdef TELEMETRY_USE_FLASH
#define PARTITION_TYPE 64
#define PARTITION_SUBTYPE 0
#define PARTITION_LABEL "storage"
#endif

#define RESERVED_SPACE 30 // number of buffers for which space is left before sending task starts.
#define BUFFER_ALIGNMENT 1024 // buffers are aligned to 1024 bytes

static const char* TAG = "tm";
static TaskHandle_t sending_handle;

volatile static size_t head;
volatile static size_t tail;
#ifdef TELEMETRY_USE_FLASH
static const esp_partition_t * storage_info;
#endif
// static int64_t * timestamps;
static int32_t NUMBER_OF_BUFFERS;

static uint8_t check_buffer[1024];

static esp_err_t parcel_write(size_t dst_offset, const void *src, size_t size){
    #ifdef TELEMETRY_USE_FLASH
    ESP_ERROR_CHECK(esp_partition_write(storage_info, dst_offset, src, size));
    ESP_ERROR_CHECK(esp_partition_read(storage_info, dst_offset, check_buffer, size));
    
    if (memcmp(src, check_buffer, size)){
        ESP_LOGE(TAG, "read verification failed! ((((((((. at offset = %zu", dst_offset);
    }
    #else
    #endif
    return ESP_OK;
}


static esp_err_t parcel_mmap(size_t offset, size_t size, const void** out_ptr, uint32_t* out_handle){
    #ifdef TELEMETRY_USE_FLASH
    ESP_ERROR_CHECK(esp_partition_mmap(storage_info, offset, size, SPI_FLASH_MMAP_DATA, out_ptr, out_handle));
    #else
    #endif
    return ESP_OK;
}

static esp_err_t parcel_munmap(uint32_t handle){
    #ifdef TELEMETRY_USE_FLASH
    spi_flash_munmap(handle);
    #else
    #endif
    return ESP_OK;
}

static esp_err_t parcel_erase_range(size_t offset, size_t size){
    #ifdef TELEMETRY_USE_FLASH
    ESP_ERROR_CHECK(esp_partition_erase_range(storage_info, offset, size));
    #else
    #endif
    return ESP_OK;
}

static size_t parcel_size(){
    #ifdef TELEMETRY_USE_FLASH
    return storage_info->size;
    #else
    #endif
    return ESP_OK;
}


static void on_got_buffer(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    accel_buffer_dto_t* typed_event_data = event_data;
    
    size_t local_tail = tail;
    size_t local_head = head;

    int32_t buf_size = typed_event_data -> buffer_count * sizeof(*typed_event_data -> buffer);

    ESP_ERROR_CHECK(parcel_write(local_tail, typed_event_data->buffer, buf_size));


    local_tail += BUFFER_ALIGNMENT;
    if (local_tail >= parcel_size()){
        local_tail = 0;
    }
    if (local_tail == local_head){
        ESP_LOGE(TAG, "got buffer and memory is full");
    }
    tail = local_tail;
    ESP_LOGI(TAG, "saved buffer and increased tail to %zu", local_tail);
    int32_t buffers_count = ((local_tail + parcel_size() - local_head) % parcel_size()) / BUFFER_ALIGNMENT;
    ESP_LOGI(TAG, "current buffers count %u", buffers_count);
    if (buffers_count == NUMBER_OF_BUFFERS - RESERVED_SPACE){
        xTaskNotifyGive(sending_handle);
    }
}


static void sending_task_function(void* args){
    while(1){
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        uint32_t parcel_handle;
        const void * data;
        parcel_mmap(0, parcel_size(), &data, &parcel_handle);
        size_t local_tail = tail/4096*4096;
        send_telemetry(data, parcel_size(), head, local_tail);
        ESP_LOGI(TAG, "sent buffers and changes head from %zu to %zu", head, local_tail);
        
        //erasing flash:
        if (local_tail > head){
            ESP_LOGI(TAG, "at erase: head is %zu, tail is %zu", head, local_tail);
            parcel_erase_range(head, local_tail-head);
        }
        else{
            ESP_LOGI(TAG, "at erase: head is %zu, tail is %zu", head, local_tail);
            parcel_erase_range(head, parcel_size()-head);
            parcel_erase_range(0, local_tail);
        }

        head = local_tail;
        parcel_munmap(parcel_handle);
    }
}

void telemetry_init(void){
    #ifdef TELEMETRY_USE_FLASH
    esp_partition_iterator_t storage_iter = esp_partition_find(PARTITION_TYPE, PARTITION_SUBTYPE, PARTITION_LABEL);
    // записывать timestampы просто в памяти, но потом
    if (storage_iter == NULL){
        ESP_LOGE(TAG, "did not find partition table for storing buffers");
        abort();
    }
    storage_info = esp_partition_get(storage_iter);
    esp_partition_iterator_release(storage_iter);
    ESP_ERROR_CHECK(esp_partition_erase_range(storage_info, 0, storage_info -> size));
    ESP_LOGI(TAG, "Initialized erased storage, size is %zu", storage_info -> size);
    
    NUMBER_OF_BUFFERS = storage_info -> size / BUFFER_ALIGNMENT;
    head = (esp_random() % NUMBER_OF_BUFFERS)/4*4 * BUFFER_ALIGNMENT; //head should be aligned 4096 (for erase_range()), buffer alignment is 1024, so...
    tail = head;
    // for test speed up purposes
    // tail = (head - 35*BUFFER_ALIGNMENT + storage_info -> size) % storage_info -> size;
    #else
    
    #endif

    xTaskCreate(sending_task_function, "sending_task", 8*configMINIMAL_STACK_SIZE, NULL, 5, &sending_handle);

    ESP_ERROR_CHECK(esp_event_handler_register_with(accel_event_loop, OW_EVENT, OW_EVENT_ON_ACCEL_BUFFER, &on_got_buffer, NULL));
}