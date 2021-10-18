#pragma once
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

extern esp_event_loop_handle_t accel_event_loop;

typedef struct{
    int16_t x, y, z;
} mpu6050_frame_t;


typedef struct{
    int64_t timestamp;
    mpu6050_frame_t* buffer;
    size_t buffer_count;
} accel_buffer_dto_t;


void accelerometer_init(void);


#ifdef __cplusplus
}
#endif