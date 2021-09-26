#include "esp_log.h"
#include "math.h"

#include <queue>
#include <algorithm>

#include "ow_events.h"
#include "esp_err.h"
#include "activity_detection.h"
#include "accelerometer.h"
#include "overwatcher_communicator.h"


static const char* TAG = "ad";


static const int INERTIA = 50; //number of statuses that account for conservativity
static const int BIAS_TO_ACTIVITY = 20; //number of acvive statuses among most recent ones sufficient to assert that status is 'active'
static const int ACTIVATION_RANGE = 20; //difference in accelerations between 10 and  90% that make status 'active'

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

    bool instantaneous_status = false;
    int metric = magnitudes[int(n*0.9)] - magnitudes[int(n*0.1)];
    if (metric > ACTIVATION_RANGE){
        instantaneous_status = true;
        active_state_cnt++;
    }
    past_states.push(instantaneous_status);
    
    if (past_states.back()){
        active_state_cnt--;
    }
    past_states.pop();

    bool status = active_state_cnt > BIAS_TO_ACTIVITY ? true : false;
    
    ESP_LOGI(TAG, "instantaneous status, is %d", instantaneous_status);
    ESP_LOGI(TAG, "sent current calculated status, which is %d", status);
    
    send_status(status);
}

void activity_detection_init(){
    //default state is 'inactive':
    for (int i=0; i<INERTIA-1; i++){
        past_states.push(false);
    }
    ESP_ERROR_CHECK(esp_event_handler_register_with(accel_event_loop, OW_EVENT, OW_EVENT_ON_ACCEL_BUFFER, &on_got_buffer, NULL));
}