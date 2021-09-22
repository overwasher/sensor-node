#pragma once

void send_status(bool status);

void send_telemetry(uint8_t* data, size_t size, size_t head, size_t tail);