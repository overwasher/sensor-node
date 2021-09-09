#include "esp_log.h"
#include "esp_event_base.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include <string.h>

#include "overwatcher_communicator.h"

#define BASEURL "http://192.168.43.18:3000/"
// #define BASEURL = "https://overwatcher.ow.dcnick3.me/";

extern const char overwatcher_ow_dcnick3_me_pem_start[] asm("_binary_overwatcher_ow_dcnick3_me_pem_start");
extern const char overwatcher_ow_dcnick3_me_pem_end[]   asm("_binary_overwatcher_ow_dcnick3_me_pem_end");

static const char* TAG = "comm";



static esp_err_t _http_event_handle(esp_http_client_event_t *evt)
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
			// ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
			break;
		case HTTP_EVENT_ON_DATA:
			ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
			if (!esp_http_client_is_chunked_response(evt->client)) {
				ESP_LOGI(TAG, "%s%.*s", "Server responded with ", evt->data_len, (char*)evt->data);
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

void send_raw_update(int64_t value, bool status){
	esp_http_client_config_t config = {
	   .url = BASEURL "sensor/v1/update",
	   .event_handler = _http_event_handle,
       .cert_pem = overwatcher_ow_dcnick3_me_pem_start,
       .method = HTTP_METHOD_POST,
	};
	esp_http_client_handle_t client = esp_http_client_init(&config);
    
    esp_http_client_set_header(client, "Authorization", "Bearer /innopolis/dorms/1/levels/3/washers/2");
    esp_http_client_set_header(client, "Content-Type", "application/json");

    char json_status[30];
    sniprintf(json_status, sizeof(json_status), "{\"state\":\"%s\"}", status?"active":"inactive");

    esp_http_client_set_post_field(client, json_status, strlen(json_status));
	esp_err_t err = esp_http_client_perform(client);
    ESP_LOGI(TAG, "sent %s", json_status);

    if (err == ESP_OK) {
	   ESP_LOGI(TAG, "Status = %d, content_length = %d",
			   esp_http_client_get_status_code(client),
			   esp_http_client_get_content_length(client));
	}

    esp_http_client_set_url(client, BASEURL"sensor/v1/update_raw");
    char json_avg_accel[30];
    sniprintf(json_avg_accel, sizeof(json_status), "{\"averageAcceleration\":%lld}", (long long) value);

    esp_http_client_set_post_field(client, json_avg_accel, strlen(json_avg_accel));
    err = esp_http_client_perform(client);
    ESP_LOGI(TAG, "sent %s", json_avg_accel);



	if (err == ESP_OK) {
	   ESP_LOGI(TAG, "Status = %d, content_length = %d",
			   esp_http_client_get_status_code(client),
			   esp_http_client_get_content_length(client));
	}
	esp_http_client_cleanup(client);
}