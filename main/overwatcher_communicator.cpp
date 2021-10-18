#include "esp_log.h"
#include "esp_event_base.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include <sys/time.h>
#include <string.h>
#include <memory>

#include "overwatcher_communicator.h"
#include "credentials.h"
#include "wifi_manager.h"

#define BASEURL CONFIG_BASEURL

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

struct esp_http_client_deleter {
	void operator()(struct esp_http_client* ptr) const	
	{
		ESP_ERROR_CHECK(esp_http_client_cleanup(ptr));
	}
};

using esp_http_client_holder = std::unique_ptr<struct esp_http_client, esp_http_client_deleter>;


class esp_http_client_wrap : public esp_http_client_holder
{
private:
	static struct esp_http_client* make_http_client(esp_http_client_method_t method, const char* url) {
		esp_http_client_config_t config;
		memset(&config, 0, sizeof(config));
		config.url = url;
		config.cert_pem = overwatcher_ow_dcnick3_me_pem_start;
		config.method = method;
		config.event_handler = _http_event_handle;
		config.buffer_size_tx = 1024;

		auto res = esp_http_client_init(&config);
		assert(res != nullptr);
		return res;
	}

public:
	esp_http_client_wrap(esp_http_client_method_t method, const char* url)
		: esp_http_client_holder(make_http_client(method, url))
	{
    ESP_ERROR_CHECK(esp_http_client_set_header(get(), "Authorization", AUTH_TOKEN));
    ESP_ERROR_CHECK(esp_http_client_set_header(get(), "Content-Type", "application/json"));
	}

	void set_post_data(const char* data, std::size_t size) {
    ESP_ERROR_CHECK(esp_http_client_set_post_field(get(), data, size));
	}

	void set_content_type(const char* content_type) {
    ESP_ERROR_CHECK(esp_http_client_set_header(get(), "Content-Type", content_type));
	}

	esp_err_t perform() {
		return esp_http_client_perform(get());
	}

	int status_code() {
		return esp_http_client_get_status_code(get());
	}

	esp_err_t open(size_t write_len) {
		return esp_http_client_open(get(), write_len);
	}

	int write(const char* buffer, int len) {
		return esp_http_client_write(get(), buffer, len);
	}

	bool write_chk(const char* buffer, int len) {
		return write(buffer, len) == len;
	}

	int fetch_headers() {
		return esp_http_client_fetch_headers(get());
	}

	int read_response(char* buffer, int len) {
		return esp_http_client_read_response(get(), buffer, len);
	}
};

class communication_holder {
	esp_err_t status = ESP_OK;
public:
	communication_holder() { status = start_communication(); }
	~communication_holder() { if (status == ESP_OK) stop_communication(); }
	communication_holder(communication_holder const&) = delete;
	communication_holder(communication_holder&&) = delete;
	communication_holder& operator=(communication_holder const&) = delete;
	communication_holder& operator=(communication_holder&&) = delete;
	operator bool() {
		return status == ESP_OK;
	}
};

void send_status(bool status){
	communication_holder comm;
	if (!comm){
		ESP_LOGE(TAG, "could not start communication, therefore did not send status");
		return;
	}

	esp_http_client_wrap client(HTTP_METHOD_POST, BASEURL "sensor/v1/update");

    char json_status[30];
    sniprintf(json_status, sizeof(json_status), "{\"state\":\"%s\"}", status?"active":"inactive");

	client.set_post_data(json_status, strlen(json_status));

	esp_err_t err = client.perform();

	if (err == ESP_OK) {
		ESP_LOGI(TAG, "sent %s", json_status);
		ESP_LOGI(TAG, "Status = %d", client.status_code());
	}
	else{
		ESP_LOGE(TAG, "client_perform() failed -> did not send status");
	}
}


typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t local_timestamp;
	uint64_t real_timestamp;
	uint32_t update_rate_nominator;
	uint32_t update_rate_denominator;
} __attribute__ ((packed)) telemetry_parcel_header_t;

static telemetry_parcel_header_t fill_parcel_header(void){
	struct timeval tv;
	gettimeofday(&tv, NULL);

	const esp_app_desc_t* app_desc = esp_ota_get_app_description();
	telemetry_parcel_header_t telemetry_parcel_header;
	telemetry_parcel_header.magic = 0x4c54574f;
	telemetry_parcel_header.version = 2;
	telemetry_parcel_header.local_timestamp = esp_timer_get_time();
	telemetry_parcel_header.real_timestamp = (int64_t) tv.tv_sec*1000000L + tv.tv_usec;
	telemetry_parcel_header.update_rate_nominator = 1690; //100Hz, but every 170th value is lost
	telemetry_parcel_header.update_rate_denominator = 17;

	return telemetry_parcel_header;
}

void send_telemetry(const uint8_t* data, size_t size, size_t head, size_t tail){
	
	telemetry_parcel_header_t telemetry_parcel_header = fill_parcel_header(); //fill header with time before attempting to start communication

	communication_holder comm;
	if (!comm){
		ESP_LOGE(TAG, "could not start communication, therefore did not send status");
		return;
	}

	ESP_LOGI(TAG, "size of parcel header is %zu", sizeof(telemetry_parcel_header));
	ESP_LOGI(TAG, "called send_telemetry with pointer to data %p, size %zu, head %zu and tail %zu", data, size, head, tail);
	

	esp_http_client_wrap client(HTTP_METHOD_POST, BASEURL "sensor/v1/telemetry");

	client.set_content_type("application/octet-stream");

	size_t parcel_len = (tail - head + size) % size + sizeof(telemetry_parcel_header);
	if (client.open(parcel_len) != ESP_OK){
		ESP_LOGE(TAG, "Failed to connect to server");
		return;
	}
	
	if (!client.write_chk((char*) &telemetry_parcel_header, sizeof(telemetry_parcel_header))){
		ESP_LOGE(TAG, "Writing header failed");
		return;
	}

	// data array is thought of as a queue: moving head and tail are operation analogous to writing and reading queue
	// there are 2 scenarios, either borders of array are included into parcel or not
	// , which, in terms of head and tail is either tail is greater that head or not
	// so we write either from head to tail, or from head to end and from beginning to tail
	if (tail > head) {
		if (!client.write_chk((char*) data + head, tail-head)) {
			ESP_LOGE(TAG, "Writing measurements failed");
			return;
		}
	} else {
		if (!client.write_chk((char*) data + head, size-head)) {
			ESP_LOGE(TAG, "Writing measurements failed");
			return;
		}
		if (!client.write_chk((char*) data, tail)) {
			ESP_LOGE(TAG, "Writing measurements failed");
			return;
		}
	}
	
	int content_length = client.fetch_headers();
	if (content_length == -1){
		ESP_LOGE(TAG, "esp_http_client_fetch_headers() failed");
		return;
	}

	char output_buffer[256] = {0};
	int data_read = client.read_response(output_buffer, 255);
	if (data_read >= 0) {
		ESP_LOGI(TAG, "HTTP GET Status = %d", client.status_code());
		ESP_LOGI(TAG, "%s", output_buffer);
	} else {
		ESP_LOGE(TAG, "Failed to read response");
	}

}