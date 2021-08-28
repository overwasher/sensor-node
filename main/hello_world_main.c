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


#define ESP_WIFI_SSID      "nirvana"
#define ESP_WIFI_PASS      "nevermind"

const char* TAG = "main";

void esp_sleep_lock(void);
void esp_sleep_unlock(void);


static xSemaphoreHandle xSemaphore = NULL;



static wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
static wifi_config_t sta_config = {
		.sta = {
			.ssid = ESP_WIFI_SSID,
			.password = ESP_WIFI_PASS,
			.bssid_set = false
		}
	};

static gpio_config_t io_conf;

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


void wifi_connect(){
	ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
	ESP_ERROR_CHECK( esp_wifi_start() );
	ESP_ERROR_CHECK( esp_wifi_connect() );
	ESP_LOGI(TAG, "Connected to wi-fi");
}


void wifi_disconnect(){
	xSemaphoreTake(xSemaphore, portMAX_DELAY);
	esp_wifi_stop();
	ESP_LOGI(TAG, "Disconnected from wi-fi");
	esp_sleep_unlock();
}


void send_request(){
	xSemaphoreTake(xSemaphore, portMAX_DELAY);

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
	xSemaphoreGive(xSemaphore);
}

void on_got_ip(){
	ESP_LOGI(TAG, "Got IP address");
	xSemaphoreGive(xSemaphore);
}

void wakeup_cb(){
	while (1){
		gpio_set_level(2, 0);
		vTaskDelay(pdMS_TO_TICKS(500));
		gpio_set_level(2, 1);
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}

static void show_memory(void* pvParameters){
	while(1){
		ESP_LOGI(TAG, "free heap: %d", esp_get_free_heap_size());
		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}

void app_main()
{	nvs_flash_init();
	tcpip_adapter_init();

	ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));

	ESP_ERROR_CHECK( esp_wifi_init(&init_config) );
	ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );

	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = 1<<2;
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	gpio_config(&io_conf);


	esp_pm_config_esp8266_t pm_conf;
	pm_conf.light_sleep_enable = true;
	ESP_ERROR_CHECK(esp_pm_configure(&pm_conf) );
	ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(20*1e6) );


	xSemaphore = xSemaphoreCreateBinary();

	xTaskCreate(show_memory, "show_memory", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

	while (1){
		if (xSemaphore != NULL){
				esp_sleep_lock();
			    wifi_connect();
			    gpio_set_level(2, 1);
			    send_request();
			    wifi_disconnect();
				gpio_set_level(2, 0);
				ESP_ERROR_CHECK(esp_light_sleep_start() );
			}
	}

}
