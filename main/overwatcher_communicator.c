#include "esp_log.h"
#include "esp_event_base.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include <sys/time.h>
#include <string.h>

#include "overwatcher_communicator.h"
#include "credentials.h"

// #define BASEURL "http://192.168.43.18:5000/"
#define BASEURL "https://overwatcher.ow.dcnick3.me/"

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

void send_status(bool status){
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

	esp_http_client_cleanup(client);
}


typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t local_timestamp;
	uint64_t real_timestamp;
	uint32_t update_rate_nominator;
	uint32_t update_rate_denominator;
	char app_version[32];
	char compile_time[16];
	char compile_date[16];
	char idf_version[32];
	uint8_t app_elf_sha256[32];
} __attribute__ ((packed)) telemetry_parcel_header_t;

static telemetry_parcel_header_t fill_parcel_header(void){
	struct timeval tv;
	gettimeofday(&tv, NULL);

	const esp_app_desc_t* app_desc = esp_ota_get_app_description();
	telemetry_parcel_header_t telemetry_parcel_header = {
		.magic = 0x4c54574f,
		.version = 1,
		.local_timestamp = esp_timer_get_time(),
		.real_timestamp = (int64_t) tv.tv_sec*1000000L + tv.tv_usec,
		.update_rate_nominator = 1690, //100Hz, but every 170th value is lost
		.update_rate_denominator = 17
	};
	memcpy(telemetry_parcel_header.app_version, app_desc->version, sizeof(app_desc->version));
	memcpy(telemetry_parcel_header.compile_time, app_desc->time, sizeof(app_desc->time));
	memcpy(telemetry_parcel_header.compile_date, app_desc->date, sizeof(app_desc->date));
	memcpy(telemetry_parcel_header.idf_version, app_desc->idf_ver, sizeof(app_desc->idf_ver));
	memcpy(telemetry_parcel_header.app_elf_sha256, app_desc->app_elf_sha256, sizeof(app_desc->app_elf_sha256));
	return telemetry_parcel_header;
}

void send_telemetry(uint8_t* data, size_t size, size_t head, size_t tail){
	telemetry_parcel_header_t telemetry_parcel_header = fill_parcel_header();
	ESP_LOGI(TAG, "size of parcel header is %zu", sizeof(telemetry_parcel_header));
	ESP_LOGI(TAG, "stub for send_telemetry with pointer to data %p, size %zu, head %zu and tail %zu", data, size, head, tail);
	esp_http_client_config_t config = {
	   .url = BASEURL "sensor/v1/telemetry",
	   .event_handler = _http_event_handle,
       .cert_pem = overwatcher_ow_dcnick3_me_pem_start,
       .method = HTTP_METHOD_POST,
	   .buffer_size_tx = 1024,
	};
	esp_http_client_handle_t client = esp_http_client_init(&config);
	
    esp_http_client_set_header(client, "Authorization", AUTH_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");

	size_t parcel_len = (tail - head + size) % size + sizeof(telemetry_parcel_header);
	if (esp_http_client_open(client, parcel_len) != ESP_OK){
		ESP_LOGE(TAG, "Failed to connect to server");
		esp_http_client_cleanup(client);
		return;
	}
	
	if (esp_http_client_write(client, (char*) &telemetry_parcel_header, sizeof(telemetry_parcel_header)) != sizeof(telemetry_parcel_header)){
		ESP_LOGE(TAG, "Writing header failed");
		goto finally;
	}

	// head-to-tail, or
	// head-to-end and beginning-to-tail
	if (tail > head 
		? (esp_http_client_write(client, (char*) data + head, tail-head) != tail-head)
		: (esp_http_client_write(client, (char*) data + head, size-head) != size-head
			|| esp_http_client_write(client, (char*) data, tail) != tail))
	{
		ESP_LOGE(TAG, "Writing measurements failed");
		goto finally;
	}
	int content_length = esp_http_client_fetch_headers(client);
	if (content_length == -1){
		ESP_LOGE(TAG, "esp_http_client_fetch_headers() failed");
		goto finally;
	}
	char output_buffer[256] = {0};
	int data_read = esp_http_client_read_response(client, output_buffer, 256);
	if (data_read >= 0) {
		ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
			esp_http_client_get_status_code(client),
			esp_http_client_get_content_length(client));
		ESP_LOGI(TAG, "%s", output_buffer);
	} else {
		ESP_LOGE(TAG, "Failed to read response");
	}



finally:
	esp_http_client_close(client);
	esp_http_client_cleanup(client);
}