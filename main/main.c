#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include <esp_wifi_types.h>
#include "esp_err.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event_base.h"
#include "freertos/semphr.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "esp_sntp.h"

#include "wifi_manager.h"
#include "accelerometer.h"
#include "activity_detection.h"
#include "overwatcher_communicator.h"
#include "telemetry.h"


static const char* TAG = "main";


static void show_memory(void* pvParameters){
	while(1){
		ESP_LOGI(TAG, "free heap: %d", esp_get_free_heap_size());
		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    sntp_init();
}

void app_main(){
	nvs_flash_init();
    ESP_ERROR_CHECK(esp_netif_init());
	

    ESP_ERROR_CHECK(esp_event_loop_create_default());

	ESP_ERROR_CHECK( gpio_install_isr_service(0) );

	esp_pm_config_esp32_t pm_conf = {
		.max_freq_mhz = 80,
		.min_freq_mhz = 10,
		.light_sleep_enable = true
	};
	pm_conf.light_sleep_enable = true;
	ESP_ERROR_CHECK(esp_pm_configure(&pm_conf) );
	ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(20*1e6) );
	wifi_init();
	
	xTaskCreate(show_memory, "show_memory", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);
	
	if (start_communication() == ESP_OK){
			ESP_LOGI(TAG, "connected successfully");
			initialize_sntp();
			while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
				ESP_LOGI(TAG, "Waiting for system time to be set...");
				vTaskDelay(2000 / portTICK_PERIOD_MS);
			}
			accelerometer_init();
			activity_detection_init();
			telemetry_init();
	}

	/*
	while (1){
		if (start_communication() == ESP_OK){
			ESP_LOGI(TAG, "connected successfully");
			send_status();
			stop_communication();
		};
		// ESP_ERROR_CHECK(esp_light_sleep_start() );
	}*/

}
