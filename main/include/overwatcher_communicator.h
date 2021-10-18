#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void send_status(bool status);

void send_telemetry(const uint8_t* data, size_t size, size_t head, size_t tail);

#ifdef __cplusplus
}
#endif