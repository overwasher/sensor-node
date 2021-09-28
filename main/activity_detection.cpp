#include "esp_log.h"
#include "math.h"

#include <queue>
#include <algorithm>

extern "C" {
#include "ow_events.h"
#include "esp_err.h"
#include "activity_detection.h"
#include "accelerometer.h"
#include "overwatcher_communicator.h"
#include "esp_timer.h"
}

static const char* TAG = "ad";

static TaskHandle_t sending_handle;

static const int INERTIA = 50; //number of statuses that account for conservativity
static const int BUFFERS_THRESHOLD = 20; //number of acvive statuses among most recent ones sufficient to assert that status is 'active'
static const int ACCEL_THRESHOLD = 20; //difference in accelerations between 10 and  90% that make status 'active'
static const int UPDATE_INTERVAL = 3e7; //in microseconds


enum class machine_state{
    active, inactive, unknown
};


static int64_t last_update_time = 0;
static volatile machine_state state = machine_state::unknown;
static int active_state_cnt = 0;
static std::queue<bool> past_states;

static void on_got_buffer(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    accel_buffer_dto_t* typed_event_data = (accel_buffer_dto_t*) event_data;
    
    int n = typed_event_data->buffer_count;
    int magnitudes[n];
    for (int i=0; i < n; i++){
        magnitudes[i] = sqrt(typed_event_data->buffer[i].x * typed_event_data->buffer[i].x +
            typed_event_data->buffer[i].y * typed_event_data->buffer[i].y + 
            typed_event_data->buffer[i].z * typed_event_data->buffer[i].z);
    }

    std::sort(magnitudes, magnitudes + n);

    int metric = magnitudes[int(n*0.9)] - magnitudes[int(n*0.1)];
    
    bool instantaneous_state = metric > ACCEL_THRESHOLD;
    if (instantaneous_state){
        active_state_cnt++;
    }

    past_states.push(instantaneous_state);
    
    if (past_states.size() > INERTIA){
        if (past_states.front()){
            active_state_cnt--;
        }
        past_states.pop();
    
        machine_state new_state = active_state_cnt > BUFFERS_THRESHOLD ? machine_state::active : machine_state::inactive;

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