#include "esp_log.h"
#include "math.h"

#include "ow_events.h"
#include "esp_err.h"
#include "activity_detection.h"
#include "accelerometer.h"
#include "overwatcher_communicator.h"


static const char* TAG = "ad";

static void on_got_buffer(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    accel_buffer_dto_t* typed_event_data = event_data;
    int64_t ans = 0;
    for (int i=0; i < typed_event_data->buffer_count; i++){
        int32_t accel = typed_event_data->buffer[i].x * typed_event_data->buffer[i].x +
            typed_event_data->buffer[i].y * typed_event_data->buffer[i].y + 
            typed_event_data->buffer[i].z * typed_event_data->buffer[i].z;
        ans += sqrt(accel);
    }
    ans /= typed_event_data->buffer_count;
    ESP_LOGI(TAG, "average magnitude of acceleration is %lld mg", (long long) ans);
    bool status = ans > 1500 ? true : false;
    // send_status(ans, status);
}

void activity_detection_init(){
    ESP_ERROR_CHECK(esp_event_handler_register_with(accel_event_loop, OW_EVENT, OW_EVENT_ON_ACCEL_BUFFER, &on_got_buffer, NULL));
}