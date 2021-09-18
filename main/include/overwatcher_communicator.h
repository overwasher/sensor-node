#pragma once

void send_raw_update(int64_t value, bool status);

void send_telemetry(uint8_t* data, size_t size, size_t head, size_t tail);