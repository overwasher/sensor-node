#pragma once

typedef struct{
    int16_t x, y, z;
} mpu6050_frame_t;


typedef struct{
    int64_t timestamp;
    mpu6050_frame_t* buffer;
    size_t buffer_count;
} accel_buffer_dto_t;


void accelerometer_init(void);