#pragma once
#include "esp_err.h"


#ifdef __cplusplus
extern "C" {
#endif

esp_err_t start_communication(void);
void stop_communication(void);
void wifi_init(void);

#ifdef __cplusplus
}
#endif