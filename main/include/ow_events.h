#pragma once

#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(OW_EVENT);
enum {
    OW_EVENT_ON_ACCEL_BUFFER,
};