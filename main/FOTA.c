/*
 * FOTA.c
 *
 *  Created on: Sep 16, 2021
 *      Author: HAOHV6
 */
#include "../MyLib/FOTA.h"

#include "../MyLib/ESP32_GPIO.h"
#include "../MyLib/SPIFFS_user.h"

#define FIRMWARE_VERSION	0.3
//server test
//#define UPDATE_JSON_URL		"http://202.191.56.104:5551/uploads/fota_profile.txt"

#define DIR_LINK_TEST "http://202.191.56.104:5551/uploads/fota_profile_test.txt"
#define DIR_LINK_LIVE "http://fotasimcom7070.s3.amazonaws.com/fota_profile.txt"

char rcv_buffer[200];
static EventGroupHandle_t s_wifi_event_group;
TaskHandle_t xHandle_SC = NULL;
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG_wifi = "smartconfig";

#define MAIN_TASK_PRIO     			1
#define UART_RX_TASK_PRIO      		2
#define CHECKTIMEOUT_TASK_PRIO     	3

extern bool Flag_Fota;
extern bool Flag_Fota_success;
extern bool Flag_wifi_init;
extern bool Flag_Fota_fail;
extern RTC_DATA_ATTR char DeviceID_TW_Str[50];
extern RTC_DATA_ATTR Device_Param VTAG_DeviceParameter;
extern Network_Signal VTAG_NetworkSignal;

extern TaskHandle_t main_task_handle;
extern TaskHandle_t uart_rx_task_handle;
extern TaskHandle_t check_timeout_task_handle;
extern TaskHandle_t gps_scan_task_handle;
extern TaskHandle_t gps_processing_task_handle;
extern TaskHandle_t mqtt_submessage_processing_handle;
TaskHandle_t check_update_task_handle;
TaskHandle_t mqtt_contol_ack_handle;
TaskHandle_t time_out_handle;
extern RTC_DATA_ATTR CFG VTAG_Configure;
extern char *base_path;
extern void GetDeviceTimestamp(void);
extern void main_task(void *arg);
extern void uart_rx_task(void *arg);
extern void check_timeout_task(void *arg);
extern void gps_scan_task(void *arg);
extern void gps_processing_task(void *arg);
extern void mqtt_submessage_processing_task(void *arg);
extern void  ResetAllParameters();
extern void Create_Tasks();
extern bool Flag_Fota_led;
static void mqtt_app_start_ack(void);
extern void fota_ack(void *arg);
extern CFG VTAG_Configure_temp;
extern bool Flag_wifi_got_led;
extern void JSON_Analyze(char* my_json_string, CFG* config);
static const char *TAG = "MQTT_EXAMPLE";
extern void ESP32_Clock_Config(int Freq_Max, int Freq_Min, bool LighSleep_Select);
esp_err_t advanced_ota_example_task(esp_http_client_config_t ota_client_config);
bool Flag_cancel_timeout = false;
bool step_get = false;
bool flag_get_token = true;
bool Flag_send_DOS = false;
bool Flag_send_DOSS = false;
bool Flag_mqtt_conn = false;
/*------------------------------------log_error_if_nonzero-------------------------------------------------*/
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

void forcedReset(){
  esp_task_wdt_init(1, true);
  esp_task_wdt_add(NULL);
  while(true);
}

static void event_handler(void *arg, esp_event_base_t event_base,int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		xTaskCreate(smartconfig_task, "smartconfig_example_task", 4096,	NULL, 3, &xHandle_SC);
	}
	else if (event_base == WIFI_EVENT	&& event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		Flag_Fota_fail = true;
		ESP_LOGI(TAG_wifi, "WiFi disconnect\r\n");
//		esp_wifi_connect();
		xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
	}
	else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
		xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
	}
	else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE)
	{
		ESP_LOGI(TAG_wifi, "Scan done");
	}
	else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL)
	{
		ESP_LOGI(TAG_wifi, "Found channel");
	}
	else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
	{
		ESP_LOGI(TAG_wifi, "Got SSID and password");
		Flag_cancel_timeout = true;
		smartconfig_event_got_ssid_pswd_t *evt	=	(smartconfig_event_got_ssid_pswd_t*) event_data;
		wifi_config_t wifi_config;
		uint8_t ssid[33] = { 0 };
		uint8_t password[65] = { 0 };
		uint8_t rvd_data[33] = { 0 };

		bzero(&wifi_config, sizeof(wifi_config_t));
		memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
		memcpy(wifi_config.sta.password, evt->password,	sizeof(wifi_config.sta.password));
		wifi_config.sta.bssid_set = evt->bssid_set;
		if (wifi_config.sta.bssid_set == true)
		{
			memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
		}

		memcpy(ssid, evt->ssid, sizeof(evt->ssid));
		memcpy(password, evt->password, sizeof(evt->password));
		ESP_LOGI(TAG_wifi, "SSID:%s", ssid);
		ESP_LOGI(TAG_wifi, "PASSWORD:%s", password);
		if (evt->type == SC_TYPE_ESPTOUCH_V2)
		{
			ESP_ERROR_CHECK(esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)));
			ESP_LOGI(TAG_wifi, "RVD_DATA:");
			for (int i = 0; i < 33; i++)
			{
				printf("%02x ", rvd_data[i]);
			}
			printf("\n");
		}

		(esp_wifi_disconnect());
		(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
		esp_wifi_connect();
	}
	else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE)
	{
		xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
	}
}
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
	switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            break;
        case HTTP_EVENT_ON_CONNECTED:
            break;
        case HTTP_EVENT_HEADER_SENT:
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_ON_DATA:
			strncpy(rcv_buffer+strlen(rcv_buffer), (char*)evt->data, evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            break;
        case HTTP_EVENT_DISCONNECTED:
            break;
    }
    return ESP_OK;
}
/*------------------------------------MQTT hanlder-------------------------------------------------*/
static void mqtt_event_handler_ack(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
    	ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED\r\n");
		memset(MQTT_ID_Topic, 0, sizeof(MQTT_ID_Topic));
		sprintf(MQTT_ID_Topic, "messages/%s/control", DeviceID_TW_Str);
		msg_id = esp_mqtt_client_subscribe(client, MQTT_ID_Topic, 1);
		ESP_LOGI(TAG, "subcribe to topic: %s\r\n", MQTT_ID_Topic);
		Flag_mqtt_conn = true;
		break;

    case MQTT_EVENT_DISCONNECTED:
    	Flag_mqtt_conn = false;
    	ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED, RECONNECT NOW");
    	esp_mqtt_client_reconnect(client);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
    	ESP_LOGI(TAG, "sent publish successful\r\n");
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA\r\n");
//        ESP_LOGI(TAG, "Data sub: %s\r\n", event->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_contol_ack(void *arg)
{
	int err = 0;
	esp_mqtt_client_config_t mqtt_cfg = {
	#if SERVER_TEST
	        .uri = "mqtt://203.113.138.18:4445",
	#elif INNOWAY_TEST
			.uri = "mqtt://116.101.122.190:1883",
			.username = "vtag",
	        .password = "abMthkHU3UOZ7T5eICcGrVvjPbya17ER",
	#elif INNOWAY_LIVE
			.uri = "mqtt://vttmqtt.innoway.vn:1883",
	#else
			.uri = "mqtt://171.244.133.251:1883",
	#endif
	};

	esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
	/* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
	esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler_ack, NULL);
	esp_mqtt_client_start(client);
	while(1)
	{
		if(Flag_mqtt_conn == true && Flag_send_DOS == true)
		{

			memset(MQTT_ID_Topic, 0, sizeof(MQTT_ID_Topic));
			sprintf(MQTT_ID_Topic, "messages/%s/devconf", DeviceID_TW_Str);
			MQTT_DevConf_FOTA_Convert(Mqtt_TX_Str, VTAG_NetworkSignal.RSRP, VTAG_Configure.CC, "DOS", VTAG_NetworkSignal.RSRQ, VTAG_Configure.Period, VTAG_Configure.Mode, 0, VTAG_Vesion, Network_Type_Str, VTAG_Configure.Network);
			err = esp_mqtt_client_publish(client, MQTT_ID_Topic, Mqtt_TX_Str, strlen(Mqtt_TX_Str), 1, 0);
			if(err != -1) Flag_send_DOS = false;
			vTaskDelay(1000/portTICK_PERIOD_MS);
		}
		else if(Flag_mqtt_conn == true &&Flag_send_DOSS == true)
		{
			memset(MQTT_ID_Topic, 0, sizeof(MQTT_ID_Topic));
			sprintf(MQTT_ID_Topic, "messages/%s/devconf", DeviceID_TW_Str);
			MQTT_DevConf_FOTA_Convert(Mqtt_TX_Str, VTAG_NetworkSignal.RSRP, VTAG_Configure.CC, "DOSS", VTAG_NetworkSignal.RSRQ, VTAG_Configure.Period, VTAG_Configure.Mode, 0, VTAG_Version_next, Network_Type_Str, VTAG_Configure.Network);
			err = esp_mqtt_client_publish(client, MQTT_ID_Topic, Mqtt_TX_Str, strlen(Mqtt_TX_Str), 1, 0);
			if(err != -1)
			{
				Flag_Fota_led = Flag_wifi_got_led = false;
//				Flag_Fota = false;
				gpio_set_level(LED_1, 1);
				vTaskDelay(2000/portTICK_PERIOD_MS);
				gpio_set_level(LED_1, 0);
				esp_mqtt_client_destroy(client);
				vTaskDelay(1000/portTICK_PERIOD_MS);
				esp_restart();
//				forcedReset();
			}
			vTaskDelay(1000/portTICK_PERIOD_MS);
		}
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}

static void check_timeout(void *arg)
{
	uint16_t start_timeout = 120;
	while(1)
	{
		start_timeout--;
		//ESP_LOGE(TAG, "Time remaining: %d", start_timeout);
		vTaskDelay(1000/portTICK_PERIOD_MS);
		if(start_timeout == 0 && Flag_cancel_timeout == false)
		{
			esp_restart();
		}
		else if(start_timeout == 0)
		{
			vTaskDelete(NULL);
		}
	}
}

void smartconfig_task(void *parm)
{
	printf("smartconfig_example_task\r\n");
	EventBits_t uxBits;
	ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
	smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
	while (1)
	{
		uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
		if (uxBits & CONNECTED_BIT)
		{
			ESP_LOGI(TAG_wifi, "WiFi Connected to ap");
		}
		if (uxBits & ESPTOUCH_DONE_BIT)
		{
			Flag_wifi_got_led = true;
			ESP_LOGI(TAG_wifi, "smartconfig over");
			esp_smartconfig_stop();
			xTaskCreatePinnedToCore(mqtt_contol_ack, "mqtt_contol_ack", 4096*2, NULL, CHECKTIMEOUT_TASK_PRIO, &mqtt_contol_ack_handle, tskNO_AFFINITY);
			xTaskCreatePinnedToCore(check_update_task, "check_update_task",1024*8, NULL, CHECKTIMEOUT_TASK_PRIO, &check_update_task_handle , tskNO_AFFINITY);
			vTaskDelete(NULL);
		}
	}
}
void init_OTA_component()
{
	Flag_wifi_init = false;
	esp_netif_init();
	esp_event_loop_create_default();
	esp_wifi_set_ps(WIFI_PS_NONE);
//	esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
//	assert(sta_netif);
	esp_netif_create_default_wifi_sta();
	Flag_wifi_init = true;
}

void check_update_task(void *pvParameter)
{
	char URL[200] = {0};
#if SERVER_TEST
	sprintf(URL, "http://171.244.133.226:8080/Thingworx/Things/%s/Services/OTA", DeviceID_TW_Str);
#elif INNOWAY_TEST
	sprintf(URL, DIR_LINK_TEST);
	printf("api get fota profile: %s\r\n", URL);
#elif INNOWAY_LIVE
	sprintf(URL, "http://171.244.133.231:8080/Thingworx/Things/%s/Services/OTA", DeviceID_TW_Str);
	printf("api get fota profile: %s\r\n", URL);
#endif
	printf("Looking for a new firmware...\n");
	while(1) {
			memset(rcv_buffer, 0, 200);
			// configure the esp_http_client
			esp_http_client_config_t config = {
#if INNOWAY_LIVE
					.method = HTTP_METHOD_POST,
#elif INNOWAY_TEST
					.method = HTTP_METHOD_GET,
#endif
					.url = URL,
					.event_handler = _http_event_handler,
					.buffer_size = 200
			};
			esp_http_client_handle_t client = esp_http_client_init(&config);
			esp_http_client_set_post_field(client, "{}", strlen("{}"));
			esp_http_client_set_header(client, "appKey", "5403be97-566e-4f98-b6e2-ef20573432df");
			esp_http_client_set_header(client, "Accept", "application/json");
			esp_http_client_set_header(client, "Content-Type", "application/json");
			// downloading the json file

			esp_err_t err = esp_http_client_perform(client);
			if(err == ESP_OK)
			{
				// parse the json file
				ESP_LOGI(TAG_wifi, "rcv_buffer:%s", rcv_buffer);
				cJSON *json = cJSON_Parse(rcv_buffer);
				if(json == NULL)
				{
					esp_http_client_cleanup(client);
					printf("downloaded file is not a valid json, aborting...\n");
					goto AWS;
				}
				else {
					cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "app");
					for(int i = 0; i < strlen(version->valuestring); i++)
					{
						if(version->valuestring[i] == '.' && version->valuestring[i+1] == 'b' && version->valuestring[i+2] == 'i' && version->valuestring[i+3] == 'n')
						{
							version->valuestring[i] = 0;
							break;
						}
					}
					strcpy(VTAG_Version_next, version->valuestring);
					cJSON *file = cJSON_GetObjectItemCaseSensitive(json, "host");

					// check the version
					if((!cJSON_IsNumber(version)) && 0) printf("unable to read new version, aborting...\n");
					else
					{
						double new_version = version->valuedouble;
						if(new_version >= FIRMWARE_VERSION || 1)
						{
							if(cJSON_IsString(file) && (file->valuestring != NULL))
							{
								printf("downloading and installing new firmware (%s)...\n", file->valuestring);
								Flag_send_DOS = true;
								esp_http_client_config_t ota_client_config = {
									.url = file->valuestring,
									.cert_pem = NULL,
									.skip_cert_common_name_check = true
								};
								esp_err_t ret = advanced_ota_example_task(ota_client_config);
								if (ret == ESP_OK)
								{
									esp_http_client_cleanup(client);
									ResetAllParameters();
									Flag_Fota_success = true;
									Flag_send_DOSS = true;
									vTaskDelete(NULL);
								}
								else
								{
									printf("OTA failed...\n");
									esp_http_client_cleanup(client);
									ResetAllParameters();
									Flag_Fota_fail = true;
									vTaskDelete(NULL);
								}
							}
							else
							{
								printf("unable to read the new file name, aborting...\n");
								esp_http_client_cleanup(client);
								ResetAllParameters();
								Flag_Fota_fail = true;
								vTaskDelete(NULL);
							}
						}
						else
						{
							printf("current firmware version (%.1f) is greater or equal to the available one (%.1f), nothing to do...\n", FIRMWARE_VERSION, new_version);
							esp_http_client_cleanup(client);
							ResetAllParameters();
							Flag_Fota_fail = true;
							vTaskDelete(NULL);
						}
					}
				}
			}
			else
			{
				AWS:
				printf("unable to download the json file, change to aws cloud\n");
				memset(rcv_buffer, 0, 200);
				esp_http_client_config_t config = {
						.method = HTTP_METHOD_GET,
						.url = DIR_LINK_LIVE,
						.event_handler = _http_event_handler,
						.buffer_size = 200
				};
				esp_http_client_handle_t client_aws = esp_http_client_init(&config);
				esp_http_client_set_method(client_aws, HTTP_METHOD_GET);
				esp_http_client_set_header(client_aws, "Accept", "application/json");
				esp_http_client_set_header(client_aws, "Content-Type", "application/json");
				// downloading the json file
				esp_err_t err = esp_http_client_perform(client_aws);

				if(err == ESP_OK)
				{
					// parse the json file
					ESP_LOGI(TAG_wifi, "rcv_buffer:%s", rcv_buffer);
					cJSON *json = cJSON_Parse(rcv_buffer);
					if(json == NULL) printf("downloaded file is not a valid json, aborting...\n");
					else {
						cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "app");
						for(int i = 0; i < strlen(version->valuestring); i++)
						{
							if(version->valuestring[i] == '.' && version->valuestring[i+1] == 'b' && version->valuestring[i+2] == 'i' && version->valuestring[i+3] == 'n')
							{
								version->valuestring[i] = 0;
								break;
							}
						}
						strcpy(VTAG_Version_next, version->valuestring);
						cJSON *file = cJSON_GetObjectItemCaseSensitive(json, "host");

						// check the version
						if((!cJSON_IsNumber(version)) && 0) printf("unable to read new version, aborting...\n");
						else
						{
							double new_version = version->valuedouble;
							if(new_version >= FIRMWARE_VERSION || 1)
							{
								if(cJSON_IsString(file) && (file->valuestring != NULL))
								{
									printf("downloading and installing new firmware (%s)...\n", file->valuestring);
									Flag_send_DOS = true;
									esp_http_client_config_t ota_client_config = {
										.url = file->valuestring,
										.cert_pem = NULL,
										.skip_cert_common_name_check = true
									};
									esp_err_t ret = advanced_ota_example_task(ota_client_config);
									if (ret == ESP_OK)
									{
										esp_http_client_cleanup(client_aws);
										ResetAllParameters();
										Flag_Fota_success = true;
										Flag_send_DOSS = true;
										vTaskDelete(NULL);
									}
									else
									{
										printf("OTA failed...\n");
										esp_http_client_cleanup(client_aws);
										ResetAllParameters();
										Flag_Fota_fail = true;
										vTaskDelete(NULL);
									}
								}
								else
								{
									printf("unable to read the new file name, aborting...\n");
									esp_http_client_cleanup(client_aws);
									ResetAllParameters();
									Flag_Fota_fail = true;
									vTaskDelete(NULL);
								}
							}
							else
							{
								printf("current firmware version (%.1f) is greater or equal to the available one (%.1f), nothing to do...\n", FIRMWARE_VERSION, new_version);
								esp_http_client_cleanup(client_aws);
								ResetAllParameters();
								Flag_Fota_fail = true;
								vTaskDelete(NULL);
							}
						}
					}
				}
				else
				{
					printf("unable to download the json file, aborting...\n");
					esp_http_client_cleanup(client_aws);
					ResetAllParameters();
					Flag_Fota_fail = true;
					vTaskDelete(NULL);
				}
			}
	    }
}
static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }
    return ESP_OK;
}
esp_err_t advanced_ota_example_task(esp_http_client_config_t ota_client_config)
{
    ESP_LOGI(TAG, "Starting Advanced OTA example");
    int total_size_ota = 0;
    int threshold_percent = 0;
    esp_err_t ota_finish_err = ESP_OK;
    esp_http_client_config_t config = {
        .url = ota_client_config.url,
		.skip_cert_common_name_check = true,
		.cert_pem = NULL,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        return ESP_FAIL;
    }
    total_size_ota = esp_https_ota_get_image_size(https_ota_handle);
    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_read_img_desc failed");
        goto ota_end;
    }
    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image header verification failed");
        goto ota_end;
    }

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
//        ESP_LOGW(TAG, "Image bytes read: %d", esp_https_ota_get_image_len_read(https_ota_handle));
        if(esp_https_ota_get_image_len_read(https_ota_handle) * 100/ total_size_ota >= threshold_percent)
		{
			threshold_percent = threshold_percent + 1;
			ESP_LOGI(TAG, "Dowload: %d%%", esp_https_ota_get_image_len_read(https_ota_handle) * 100/ total_size_ota);
		}
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(TAG, "Complete data was not received.");
    } else {
        ota_finish_err = esp_https_ota_finish(https_ota_handle);
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
            ESP_LOGI(TAG, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            return ESP_OK;
        } else {
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            }
            ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
            return ESP_FAIL;
        }
    }

ota_end:
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed");
    return ESP_FAIL;
}
void ini_wifi()
{
	xTaskCreatePinnedToCore(check_timeout, "check_timeout", 4096, NULL, MAIN_TASK_PRIO, &time_out_handle, tskNO_AFFINITY);
	//ESP_ERROR_CHECK(esp_netif_init());
	s_wifi_event_group = xEventGroupCreate();
	xTaskCreatePinnedToCore(fota_ack, "fota_ack", 4096*2, NULL, CHECKTIMEOUT_TASK_PRIO, NULL, tskNO_AFFINITY);
	xTaskCreatePinnedToCore(check_timeout_task, "check_timeout", 4096*2, NULL, CHECKTIMEOUT_TASK_PRIO, &check_timeout_task_handle, tskNO_AFFINITY);
	xTaskCreatePinnedToCore(mqtt_submessage_processing_task, "mqtt submessage processing", 4096*2, NULL, CHECKTIMEOUT_TASK_PRIO, &mqtt_submessage_processing_handle, tskNO_AFFINITY);
	//ESP_ERROR_CHECK(esp_event_loop_create_default());

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	(esp_wifi_init(&cfg));

	(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
	(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

	(esp_wifi_set_mode(WIFI_MODE_STA));
	//ESP_ERROR_CHECK(esp_wifi_start());
}
