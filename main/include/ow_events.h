#pragma once

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(OW_EVENT);
enum {
    OW_EVENT_ON_ACCEL_BUFFER,
};

#ifdef __cplusplus
}
#endif