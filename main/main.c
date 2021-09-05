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
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_event_base.h"
#include "freertos/semphr.h"
#include "esp_sleep.h"
#include "esp_pm.h"

#include "wifi_manager.h"
#include "accelerometer.h"


static const char* TAG = "main";

esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
	switch(evt->event_id) {
		case HTTP_EVENT_ERROR:
			ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
			break;
		case HTTP_EVENT_ON_CONNECTED:
			ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
			break;
		case HTTP_EVENT_HEADER_SENT:
			ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
			break;
		case HTTP_EVENT_ON_HEADER:
			ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
			printf("%.*s", evt->data_len, (char*)evt->data);
			break;
		case HTTP_EVENT_ON_DATA:
			ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
			if (!esp_http_client_is_chunked_response(evt->client)) {
				printf("%.*s", evt->data_len, (char*)evt->data);
			}
			break;
		case HTTP_EVENT_ON_FINISH:
			ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
			break;
		case HTTP_EVENT_DISCONNECTED:
			ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
			break;
	}
	return ESP_OK;
}


void send_request(){
	esp_http_client_config_t config = {
	   .url = "http://httpbin.org/get",
	   .event_handler = _http_event_handle,
	};
	esp_http_client_handle_t client = esp_http_client_init(&config);
	esp_err_t err = esp_http_client_perform(client);

	if (err == ESP_OK) {
	   ESP_LOGI(TAG, "Status = %d, content_length = %d",
			   esp_http_client_get_status_code(client),
			   esp_http_client_get_content_length(client));
	}
	esp_http_client_cleanup(client);
	ESP_LOGI(TAG, "Sent an http request");
}

static void show_memory(void* pvParameters){
	while(1){
		ESP_LOGI(TAG, "free heap: %d", esp_get_free_heap_size());
		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}

void app_main(){
	nvs_flash_init();
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());


	esp_pm_config_esp32_t pm_conf = {
		.max_freq_mhz = 80,
		.min_freq_mhz = 10,
		.light_sleep_enable = true
	};
	pm_conf.light_sleep_enable = true;
	ESP_ERROR_CHECK(esp_pm_configure(&pm_conf) );
	ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(20*1e6) );
	wifi_init();
	accelerometer_init();
	xTaskCreate(show_memory, "show_memory", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

	while (1){
		if (start_communication() == ESP_OK){
			ESP_LOGI(TAG, "connected successfully");
			send_request();
			stop_communication();
		};
		// ESP_ERROR_CHECK(esp_light_sleep_start() );
	}

}
