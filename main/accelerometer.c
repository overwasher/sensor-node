#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp32_i2c_rw/esp32_i2c_rw.h"
#include "mpu6050/mpu6050.h"
#include "mpu6050/mpu6050_registers.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "endian.h"

static const char* TAG = "accel";

#define MPU6050_SDA_IO 4
#define MPU6050_SCL_IO 5
#define MPU6050_FREQ_HZ 4e5

#define MPU6050_WIP_IO 13
#define MPU6050_INT_IO 14

typedef struct{
    int16_t x, y, z;
} mpu6050_frame_t;

static uint8_t buffer[1024];

static TaskHandle_t accel_handle;

static int16_t map_to_mg(int16_t value){
    return ((int32_t) value) * 16000/(1<<15);
}
 
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

    mpu6050_acceleration_t accel;
    while(1){
        ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(10000));
        // TODO: if timeout then crash
        // TODO: check buffer size
        gpio_set_level(MPU6050_WIP_IO, 1);
        mpu6050_get_int_status();
        mpu6050_get_fifo_bytes(buffer, sizeof(buffer));
        mpu6050_frame_t* ptr = (mpu6050_frame_t*)(buffer + sizeof(buffer) % sizeof(mpu6050_frame_t));
        size_t number_of_frames = sizeof(buffer) / sizeof(mpu6050_frame_t);
        for (int i = 0; i < number_of_frames; i++){
            ptr[i].x = map_to_mg(be16toh(ptr[i].x));
            ptr[i].y = map_to_mg(be16toh(ptr[i].y));
            ptr[i].z = map_to_mg(be16toh(ptr[i].z));
            // ESP_LOGI(TAG, "x: %d, y: %d, z: %d", ptr[i].x, ptr[i].y, ptr[i].z);
        }
        
        gpio_set_level(MPU6050_WIP_IO, 0);
        // mpu6050_get_acceleration(&accel);
        // ESP_LOGI(TAG, "x: %d, y: %d, z: %d", map_to_mg(accel.accel_x), map_to_mg(accel.accel_y), map_to_mg(accel.accel_z));
        // vTaskDelay( pdMS_TO_TICKS(20) );
    }
}

void accelerometer_init(){
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = MPU6050_SDA_IO,         // select GPIO specific to your project
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = MPU6050_SCL_IO,         // select GPIO specific to your project
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = MPU6050_FREQ_HZ,  // select frequency specific to your project
        // .clk_flags = 0,          /*!< Optional, you can use I2C_SCLK_SRC_FLAG_* flags to choose i2c source clock here. */
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    xTaskCreate(accel_task_function, "accel_task", 5*configMINIMAL_STACK_SIZE, NULL, 7, &accel_handle);
}