#include "esp_wifi.h"
#include <esp_wifi_types.h>
#include "esp_err.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "wifi_manager.h"
#include "credentials.h"

static const char* TAG = "wifi";

static esp_pm_lock_handle_t pm_lock_handle;
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define CONNECT_TO_AP_MAXIMUM_RETRY 5

static int s_retry_num = 0; //counts failed attempts to start communication

static int comm_request_cnt = 0; //counts communication requests from different tasks
static SemaphoreHandle_t comm_request_cnt_lock; //lock for comm_request_cnt, as it is a critical region


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONNECT_TO_AP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t start_communication_impl(){
    s_retry_num = 0;
    esp_err_t res = ESP_ERR_WIFI_NOT_CONNECT;
    esp_pm_lock_acquire(pm_lock_handle); //indicates not to sleep while there is communication via wifi
    s_wifi_event_group = xEventGroupCreate();
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));


	wifi_config_t sta_config = {
		.sta = {
			.ssid = ESP_WIFI_SSID,
			.password = ESP_WIFI_PASS,
			.bssid_set = false,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
		}
	};
	ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
	ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_LOGI(TAG, "wifi_init_sta finished.");

     /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    
     /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s",
                 ESP_WIFI_SSID);
        res = ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s",
                 ESP_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);

    if (res != ESP_OK){
        stop_communication();
    }
    return res;
}

static void stop_communication_impl(void){
	esp_wifi_stop();
    esp_pm_lock_release(pm_lock_handle); //after communication is stopped, esp can enter light sleep mode
	ESP_LOGI(TAG, "Communication stopped");
}

esp_err_t start_communication(){
    esp_err_t result;
    xSemaphoreTake(comm_request_cnt_lock, portMAX_DELAY);
    if (comm_request_cnt == 0){
        result = start_communication_impl();
        if (result == ESP_OK) comm_request_cnt++;
    }
    else{
        result = ESP_OK;
        comm_request_cnt++;
    }
    xSemaphoreGive(comm_request_cnt_lock);
    return result;
}


void stop_communication(void){
    xSemaphoreTake(comm_request_cnt_lock, portMAX_DELAY);
    if (comm_request_cnt == 1){
        stop_communication_impl();
    }
    comm_request_cnt--;
    xSemaphoreGive(comm_request_cnt_lock);
}


void wifi_init(void){
    ESP_ERROR_CHECK( esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "communication", &pm_lock_handle) );
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK( esp_wifi_init(&init_config) );
	ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    
    comm_request_cnt_lock = xSemaphoreCreateMutex();
}