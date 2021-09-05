#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp32_i2c_rw/esp32_i2c_rw.h"
#include "mpu6050/mpu6050.h"
#include "mpu6050/mpu6050_registers.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c.h"

static const char* TAG = "accel";

#define I2C_MASTER_SDA_IO 4
#define I2C_MASTER_SCL_IO 5
#define I2C_MASTER_FREQ_HZ 1e5


static void accel_task_function(void* args){
    mpu6050_init();
    if (mpu6050_test_connection()){
        ESP_LOGI(TAG, "reached mpu6050 successfully");  
    }
    else{
        ESP_LOGE(TAG, "failed to interact with mpu6050");
        return;
    }
    mpu6050_acceleration_t accel;
    while(1){
        mpu6050_get_acceleration(&accel);
        ESP_LOGI(TAG, "x: %d, y: %d, z: %d", (int) accel.accel_x, (int) accel.accel_y, (int) accel.accel_z);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void accelerometer_init(){
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,         // select GPIO specific to your project
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,         // select GPIO specific to your project
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,  // select frequency specific to your project
        // .clk_flags = 0,          /*!< Optional, you can use I2C_SCLK_SRC_FLAG_* flags to choose i2c source clock here. */
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    xTaskCreate(accel_task_function, "accel_task", 5*configMINIMAL_STACK_SIZE, NULL, 5, NULL);
}