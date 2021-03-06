#include "esp_log.h"
#include "math.h"

#include <queue>
#include <algorithm>

#include "ow_events.h"
#include "esp_err.h"
#include "activity_detection.h"
#include "accelerometer.h"
#include "overwatcher_communicator.h"
#include "esp_timer.h"

static const char* TAG = "ad";

static TaskHandle_t sending_handle;

static const int INERTIA = CONFIG_ACTD_INERTIA; 
static const int BUFFERS_THRESHOLD = CONFIG_ACTD_BUFFERS_THRESHOLD; 
static const int ACCEL_THRESHOLD = CONFIG_ACTD_ACCEL_THRESHOLD;
static const int UPDATE_INTERVAL = CONFIG_ACTD_UPDATE_INTERVAL;


enum class machine_state{
    active, inactive, unknown
};


static int64_t last_update_time = 0;
static volatile machine_state state = machine_state::unknown;
static int active_state_cnt = 0;
static std::queue<bool> past_states;

static int compute_1d_metric(accel_buffer_dto_t& buffer_dto, int16_t mpu6050_frame_t::* channel) {

    auto n = buffer_dto.buffer_count;
    // combine accelerations along 3 different axes
    uint16_t magnitudes[n];
    for (int i = 0; i < n; i++)
        magnitudes[i] = buffer_dto.buffer[i].*channel;
    std::sort(magnitudes, magnitudes + n);

    // calculate difference between 10th and 90th percentile
    return magnitudes[int(n*0.9)] - magnitudes[int(n*0.1)];;
}

static int compute_metric(accel_buffer_dto_t& buffer_dto) {
    return compute_1d_metric(buffer_dto, &mpu6050_frame_t::x)
        + compute_1d_metric(buffer_dto, &mpu6050_frame_t::y);
        //+ compute_1d_metric(buffer_dto, &mpu6050_frame_t::z);
}

static void on_got_buffer(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    accel_buffer_dto_t* typed_event_data = (accel_buffer_dto_t*) event_data;

    int metric = compute_metric(*typed_event_data);
    
    bool instantaneous_state = metric > ACCEL_THRESHOLD;
    if (instantaneous_state){
        active_state_cnt++;
    }

    // conservatism of AD as queue of most recent states
    past_states.push(instantaneous_state);
    
    if (past_states.size() > INERTIA){
        if (past_states.front()){
            active_state_cnt--;
        }
        past_states.pop();
    
        machine_state new_state = active_state_cnt > BUFFERS_THRESHOLD ? machine_state::active : machine_state::inactive;
        // initiate sending upon status update or timeout
        if (new_state != state || esp_timer_get_time() - last_update_time > UPDATE_INTERVAL){
            state = new_state;
            
            xTaskNotifyGive(sending_handle);
            last_update_time = esp_timer_get_time();
        }
        
    }
    
    
    ESP_LOGI(TAG, "instantaneous status, is %d", instantaneous_state);
    ESP_LOGI(TAG, "active buffers count is %d", active_state_cnt);
    ESP_LOGI(TAG, "chosen metric is %d", metric);
    
}

static void sending_task_function(void* args){
    while(1){
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        send_status(state == machine_state::active);
        ESP_LOGI(TAG, "sent current calculated status, which is %d", state == machine_state::active);
    }
}


void activity_detection_init(){
    
    xTaskCreate(sending_task_function, "sending_ad_task", 8*configMINIMAL_STACK_SIZE, NULL, 5, &sending_handle);

    ESP_ERROR_CHECK(esp_event_handler_register_with(accel_event_loop, OW_EVENT, OW_EVENT_ON_ACCEL_BUFFER, &on_got_buffer, NULL));
}