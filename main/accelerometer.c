#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp32_i2c_rw/esp32_i2c_rw.h"
#include "mpu6050/mpu6050.h"
#include "mpu6050/mpu6050_registers.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "endian.h"
#include "esp_timer.h"

#include "ow_events.h"
#include "activity_detection.h"
#include "accelerometer.h"

static const char* TAG = "accel";

#define MPU6050_SDA_IO 26
#define MPU6050_SCL_IO 25
#define MPU6050_FREQ_HZ 4e5

#define MPU6050_WIP_IO 13
#define MPU6050_INT_IO 14

static uint8_t buffer[1024]; // to store telemetry from accelerometer after interrupt

static TaskHandle_t accel_handle;

// convert accelerations to milli-g (where g is gravity of Earth)
static int16_t map_to_mg(int16_t value){
    return ((int32_t) value) * 16000/(1<<15);
}

// when interrupt from accelerometer arrives, let accel_task_function() handle it
static void IRAM_ATTR mpu_isr_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(accel_handle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

static void accel_task_function(void* args){
    mpu6050_init();
    if (mpu6050_test_connection()){
        ESP_LOGI(TAG, "reached mpu6050 successfully");  
    }
    else{
        ESP_LOGE(TAG, "failed to interact with mpu6050");
        return;
    }
    mpu6050_set_rate(9); //100 Hz

    mpu6050_reset_fifo();

    mpu6050_set_full_scale_accel_range(3); //precision stuff [-16g; 16g]
    mpu6050_set_dlpf_mode(2); //low pass filter
    
    gpio_config_t io_conf = {};
    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    //bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = 1 << MPU6050_INT_IO;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = 1 << MPU6050_WIP_IO;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    gpio_set_level(MPU6050_WIP_IO, 0);

    mpu6050_set_interrupt_mode(true); // interrupt logic level mode — active-low
    mpu6050_set_interrupt_drive(true); // interrupt drive mode — open-drain
    mpu6050_set_interrupt_latch(false); // interrupt latch mode — 50us-pulse

    
    gpio_isr_handler_add(MPU6050_INT_IO, mpu_isr_handler, NULL);

    mpu6050_set_fifo_enabled(true);
    mpu6050_set_int_fifo_buffer_overflow_enabled(true);
    mpu6050_set_accel_fifo_enabled(true);
    while(1){
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        gpio_set_level(MPU6050_WIP_IO, 1);
        mpu6050_get_int_status();

        // check if interrupt was indeed caused by full fifo of the accelerometer (not some spurious interrupt)
        int actual_buffer_size = mpu6050_get_fifo_count();
        if (actual_buffer_size != sizeof(buffer)) {
            ESP_LOGE(TAG, "received interrupt and buffer size is %d, while expected budder size is %zu", actual_buffer_size, sizeof(buffer));
            continue;
        }
        mpu6050_get_fifo_bytes(buffer, sizeof(buffer));
        mpu6050_frame_t* ptr = (mpu6050_frame_t*)(buffer + sizeof(buffer) % sizeof(mpu6050_frame_t));
        size_t number_of_frames = sizeof(buffer) / sizeof(mpu6050_frame_t);
        for (int i = 0; i < number_of_frames; i++){
            ptr[i].x = map_to_mg(be16toh(ptr[i].x));
            ptr[i].y = map_to_mg(be16toh(ptr[i].y));
            ptr[i].z = map_to_mg(be16toh(ptr[i].z));
        }
        
        gpio_set_level(MPU6050_WIP_IO, 0);

        accel_buffer_dto_t accel_buffer_dto = {
            .timestamp = esp_timer_get_time(),
            .buffer = ptr,
            .buffer_count = number_of_frames
        };
        ESP_ERROR_CHECK(esp_event_post_to(accel_event_loop, OW_EVENT, OW_EVENT_ON_ACCEL_BUFFER, &accel_buffer_dto, sizeof(accel_buffer_dto), 0));
    }
}


esp_event_loop_handle_t accel_event_loop;

void accelerometer_init(){
    // configuring i2c wires:
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = MPU6050_SDA_IO,         // project-specific GPIO 
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = MPU6050_SCL_IO,         // project-specific GPIO
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = MPU6050_FREQ_HZ,  // project-specific frequency
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    
    // event loop that receives and handles event OW_EVENT_ON_ACCEL_BUFFER (when it arrives from the accelerometer)
    esp_event_loop_args_t loop_args = {
        .queue_size = CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE,
        .task_name = "ad_evt",
        .task_stack_size = 8*configMINIMAL_STACK_SIZE,
        .task_priority = ESP_TASKD_EVENT_PRIO,
        .task_core_id = 0
    };
    ESP_ERROR_CHECK(esp_event_loop_create(&loop_args, &accel_event_loop));

    xTaskCreate(accel_task_function, "accel_task", 5*configMINIMAL_STACK_SIZE, NULL, 7, &accel_handle);
}