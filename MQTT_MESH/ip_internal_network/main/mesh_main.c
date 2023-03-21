/* Mesh Internal Communication Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "nvs_flash.h"
#include "mesh_netif.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#include "esp_spiffs.h"
#include "st7789.h"
#include "fontx.h"
#include "pngle.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_vfs.h"

/*******************************************************
 *                LCD
 *******************************************************/

	static uint16_t color = GREEN;
	uint8_t ascii[50];
    uint8_t buffer[FontxGlyphBufSize];
	uint8_t fontWidth;
	uint8_t fontHeight;

static TFT_t dev;

static const char *TAG1 = "ST7789";

static void SPIFFS_Directory(char * path) {
	DIR* dir = opendir(path);
	assert(dir != NULL);
	while (true) {
		struct dirent*pe = readdir(dir);
		if (!pe) break;
		ESP_LOGI(__FUNCTION__,"d_name=%s d_ino=%d d_type=%x", pe->d_name,pe->d_ino, pe->d_type);
	}
	closedir(dir);
}

    FontxFile fx16G[2];

/*******************************************************
 *                Macros
 *******************************************************/
#define BUTTON_PIN 0

#define EXAMPLE_BUTTON_GPIO     0

// commands for internal mesh communication:
// <CMD> <PAYLOAD>, where CMD is one character, payload is variable dep. on command
#define CMD_KEYPRESSED 0x55
// CMD_KEYPRESSED: payload is always 6 bytes identifying address of node sending keypress event
#define CMD_ROUTE_TABLE 0x56
// CMD_KEYPRESSED: payload is a multiple of 6 listing addresses in a routing table
/*******************************************************
 *                Constants
 *******************************************************/
static const char *MESH_TAG = "mesh_main";
static const uint8_t MESH_ID[6] = { 0x77, 0x77, 0x77, 0x77, 0x77, 0x76};

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static bool is_running = true;
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;
static esp_ip4_addr_t s_current_ip;
static mesh_addr_t s_route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
static int s_route_table_size = 0;
static SemaphoreHandle_t s_route_table_lock = NULL;
static uint8_t s_mesh_tx_payload[CONFIG_MESH_ROUTE_TABLE_SIZE*6+1];
static char mqtt_dataL[] = "0";
static bool button_pressed = false;
static char* string;
static char* str;
static char* last_dot;
static char* charger = "1";
static char* token;


/*******************************************************
 *                Function Declarations
 *******************************************************/
// interaction with public mqtt broker
void mqtt_app_start(void);
void mqtt_app_publish(char* topic, char *publish_string);

/*******************************************************
 *                Function Definitions
 *******************************************************/

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ MESH RECIEVE ROUTING INFO
void static recv_cb(mesh_addr_t *from, mesh_data_t *data)
{
    if (data->data[0] == CMD_ROUTE_TABLE) {
        int size =  data->size - 1;
        if (s_route_table_lock == NULL || size%6 != 0) {
            ESP_LOGE(MESH_TAG, "Error in receiving raw mesh data: Unexpected size");
            return;
        }
        xSemaphoreTake(s_route_table_lock, portMAX_DELAY);
        s_route_table_size = size / 6;
        for (int i=0; i < s_route_table_size; ++i) {
            ESP_LOGI(MESH_TAG, "Received Routing table [%d] "
                    MACSTR, i, MAC2STR(data->data + 6*i + 1));
        }
        memcpy(&s_route_table, data->data + 1, size);
        xSemaphoreGive(s_route_table_lock);
    } else if (data->data[0] == CMD_KEYPRESSED) {
        if (data->size != 7) {
            ESP_LOGE(MESH_TAG, "Error in receiving raw mesh data: Unexpected size");
            return;
        }
        ESP_LOGW(MESH_TAG, "Keypressed detected on node: "
                MACSTR, MAC2STR(data->data + 1));
    } else {
        ESP_LOGE(MESH_TAG, "Error in receiving raw mesh data: Unknown command");
    }
}
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ SET CHARGING ON MQTT SUBSCRIBE EVENT
void change_charging_data(char *mqtt_data){
    ESP_LOGI(MESH_TAG, "%s, %s", mqtt_dataL, mqtt_data);
    strcpy(mqtt_dataL, mqtt_data);
    int count = 0;
    int max_count = 0;
    wifi_ap_record_t ap;
    esp_wifi_sta_get_ap_info(&ap);

    asprintf(&string, IPSTR "/RSSI", IP2STR(&s_current_ip));
    asprintf(&str, "%d" ,ap.rssi);
    mqtt_app_publish(string, str);

    asprintf(&string, IPSTR "/LAYER", IP2STR(&s_current_ip));
    asprintf(&str, "%d", mesh_layer);
    mqtt_app_publish(string, str);

    asprintf(&string, IPSTR "/RTABLE", IP2STR(&s_current_ip));
    asprintf(&str, "%d", esp_mesh_get_routing_table_size());
    mqtt_app_publish(string, str);

    asprintf(&string, IPSTR "/MESHMODE", IP2STR(&s_current_ip));
    asprintf(&str, "%s", (esp_mesh_is_root()) ? "ROOT" : "NODE");
    mqtt_app_publish(string, str);
    asprintf(&string, "%s - " IPSTR, str, IP2STR(&s_current_ip));
    mqtt_app_publish("/WHOISHERE", string);

    asprintf(&string, "" IPSTR, IP2STR(&s_current_ip));
    if(!esp_mesh_is_root()){
        ESP_LOGI(MESH_TAG, "CHARGING DATA = %s", mqtt_data);
        //VERY NICE LOGIC
        last_dot = strrchr(string, '.');
        if (last_dot != NULL) {
            charger = last_dot + 1; // get symbol after last dot
        }
        ESP_LOGI(MESH_TAG, "CHARGER = %s", charger);
    }
    for (int i = 0; mqtt_data[i] != '\0'; i++) {
        if (mqtt_data[i] == ',') {
            max_count++;
        }
    }
        ESP_LOGI(MESH_TAG, "max_count = %d", max_count);
        //EVALUATE CHARGING DATA
        token = strtok(mqtt_data, ","); // get first token
        while (token != NULL) {
            ESP_LOGI(MESH_TAG, "count = %d", count);
            ESP_LOGI(MESH_TAG, "token = %s", token);
            if(strchr(token, *charger) != NULL){
                if(count == 0 && max_count == 0){ // I AM FIRST
                    mqtt_app_publish(string, "A");
                    asprintf(&str, "A");
                    break;
                }else if(count >= 0){// I AM HERE WITH SOMEBODY
                    mqtt_app_publish(string, "B");
                    asprintf(&str, "B");
                    break;
                }
            }else{
                if(count == max_count){// I AM NOT ON THE LIST
                    mqtt_app_publish(string, "C");
                    asprintf(&str, "C");
                    break;
                }
                count++;
                token = strtok(NULL, ","); // get next token
            }
        }
            lcdFillScreen(&dev, BLACK);
        	sprintf((char *)ascii, "layer: %d, rtableSize:%d", mesh_layer, esp_mesh_get_routing_table_size());
	        lcdSetFontDirection(&dev, 1);
	        lcdDrawString(&dev, fx16G, CONFIG_WIDTH-(fontHeight*1), 0, ascii, color);
            sprintf((char *)ascii, "%s | " IPSTR, (esp_mesh_is_root()) ? "ROOT" : "NODE", IP2STR(&s_current_ip));
	        lcdDrawString(&dev, fx16G, CONFIG_WIDTH-(fontHeight*2), 0, ascii, color);
            sprintf((char *)ascii, "RSSI: %d dBm", ap.rssi);
	        lcdDrawString(&dev, fx16G, CONFIG_WIDTH-(fontHeight*3), 0, ascii, color);
            sprintf((char *)ascii, "mqtt_data = %s", mqtt_data);
	        lcdDrawString(&dev, fx16G, CONFIG_WIDTH-(fontHeight*4), 0, ascii, color);
            sprintf((char *)ascii, "CHARGE MODE = %s", str);
	        lcdDrawString(&dev, fx16G, CONFIG_WIDTH-(fontHeight*5), 0, ascii, color);
}
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ INITIAL MQTT LAUNCH AND INFINITE WHILE WITH MESH ROUTING AND BUTTON FUNCTION
void esp_mesh_mqtt_task(void *arg)
{
    wifi_ap_record_t ap;
    esp_wifi_sta_get_ap_info(&ap);

            lcdFillScreen(&dev, BLACK);
        	sprintf((char *)ascii, "layer: %d, rtableSize:%d", mesh_layer, esp_mesh_get_routing_table_size());
	        lcdSetFontDirection(&dev, 1);
	        lcdDrawString(&dev, fx16G, CONFIG_WIDTH-(fontHeight*1), 0, ascii, color);
            sprintf((char *)ascii, "%s | " IPSTR, (esp_mesh_is_root()) ? "ROOT" : "NODE", IP2STR(&s_current_ip));
	        lcdDrawString(&dev, fx16G, CONFIG_WIDTH-(fontHeight*2), 0, ascii, color);
            sprintf((char *)ascii, "RSSI: %d dBm", ap.rssi);
	        lcdDrawString(&dev, fx16G, CONFIG_WIDTH-(fontHeight*3), 0, ascii, color);
            sprintf((char *)ascii, "mqtt_data = INITIALIZING");
	        lcdDrawString(&dev, fx16G, CONFIG_WIDTH-(fontHeight*4), 0, ascii, color);
            sprintf((char *)ascii, "CHARGE MODE = C");
	        lcdDrawString(&dev, fx16G, CONFIG_WIDTH-(fontHeight*5), 0, ascii, color);

    mqtt_app_start();

    asprintf(&string, "" IPSTR, IP2STR(&s_current_ip));
    mqtt_app_publish(string, "C");

    asprintf(&string, IPSTR "/RSSI", IP2STR(&s_current_ip));
    asprintf(&str, "%d" ,ap.rssi);
    mqtt_app_publish(string, str);

    asprintf(&string, IPSTR "/LAYER", IP2STR(&s_current_ip));
    asprintf(&str, "%d", mesh_layer);
    mqtt_app_publish(string, str);

    asprintf(&string, IPSTR "/RTABLE", IP2STR(&s_current_ip));
    asprintf(&str, "%d", esp_mesh_get_routing_table_size());
    mqtt_app_publish(string, str);

    asprintf(&string, IPSTR "/MESHMODE", IP2STR(&s_current_ip));
    asprintf(&str, "%s", (esp_mesh_is_root()) ? "ROOT" : "NODE");
    mqtt_app_publish(string, str);
    asprintf(&string, "%s - " IPSTR, str, IP2STR(&s_current_ip));
    mqtt_app_publish("/WHOISHERE", string);
    
    is_running = true;
    mesh_data_t data;
    esp_err_t err;
    while (is_running) {
            if(button_pressed == true){
                if(!esp_mesh_is_root()){
                    asprintf(&string, "" IPSTR, IP2STR(&s_current_ip));
                    ESP_LOGI(MESH_TAG, "CHARGING DATA = %s", mqtt_dataL);
                    //VERY NICE LOGIC
                    last_dot = strrchr(string, '.');
                    if (last_dot != NULL) {
                        charger = last_dot + 1; // get symbol after last dot
                    }
                }
                ESP_LOGI(MESH_TAG, "CHARGER = %s", charger);
                if(strstr(mqtt_dataL, charger) != NULL){
                    ESP_LOGI(MESH_TAG, "TRIED PUBLISH 1 - 0, %s", mqtt_dataL);
                    asprintf(&string, "%s - 0", charger);
                    mqtt_app_publish("chargeQ", string);
                }else{
                    ESP_LOGI(MESH_TAG, "TRIED PUBLISH 1 - 1 , %s", mqtt_dataL);
                    asprintf(&string, "%s - %s", charger, charger);
                    mqtt_app_publish("chargeQ", string);
                }
                button_pressed = false;
            }
            esp_mesh_get_routing_table((mesh_addr_t *) &s_route_table,
                                       CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &s_route_table_size);
            data.size = s_route_table_size * 6 + 1;
            data.proto = MESH_PROTO_BIN;
            data.tos = MESH_TOS_P2P;
            s_mesh_tx_payload[0] = CMD_ROUTE_TABLE;
            memcpy(s_mesh_tx_payload + 1, s_route_table, s_route_table_size*6);
            data.data = s_mesh_tx_payload;
            for (int i = 1; i < s_route_table_size; i++) {
                err = esp_mesh_send(&s_route_table[i], &data, MESH_DATA_P2P, NULL, 0);
                ESP_LOGI(MESH_TAG, "Sending routing table to [%d] "
                        MACSTR ": sent with err code: %d", i, MAC2STR(s_route_table[i].addr), err);
            }
        vTaskDelay(2 * 1000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------- LAUNCH MQTT TASK AND SET FONT FOR LCD, CREATE SEMAPHORE FOR MESH AND MQTT ROUTING
esp_err_t esp_mesh_comm_mqtt_task_start(void)
{
    InitFontx(fx16G,"/spiffs/ILGH16XB.FNT",""); // 8x16Dot Gothic
	GetFontx(fx16G, 0, buffer, &fontWidth, &fontHeight);

    s_route_table_lock = xSemaphoreCreateMutex();
    
    static bool is_comm_mqtt_task_started = false;


    if (!is_comm_mqtt_task_started) {
        xTaskCreate(esp_mesh_mqtt_task, "mqtt task", 3072, NULL, 5, NULL);
        is_comm_mqtt_task_started = true;
    }
    return ESP_OK;
}
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ MESH HANDLER
void mesh_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    mesh_addr_t id = {0,};
    static uint8_t last_layer = 0;

    switch (event_id) {
    case MESH_EVENT_STARTED: {
        esp_mesh_get_id(&id);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_STOPPED: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_CHILD_CONNECTED: {
        mesh_event_child_connected_t *child_connected = (mesh_event_child_connected_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR"",
                 child_connected->aid,
                 MAC2STR(child_connected->mac));
    }
    break;
    case MESH_EVENT_CHILD_DISCONNECTED: {
        mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
                 child_disconnected->aid,
                 MAC2STR(child_disconnected->mac));
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_ADD: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new);
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new);
    }
    break;
    case MESH_EVENT_NO_PARENT_FOUND: {
        mesh_event_no_parent_found_t *no_parent = (mesh_event_no_parent_found_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                 no_parent->scan_times);
    }
    /* TODO handler for the failure */
    break;
    case MESH_EVENT_PARENT_CONNECTED: {
        mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
        esp_mesh_get_id(&id);
        mesh_layer = connected->self_layer;
        memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR"",
                 last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr));
        last_layer = mesh_layer;
        mesh_netifs_start(esp_mesh_is_root());
    }
    break;
    case MESH_EVENT_PARENT_DISCONNECTED: {
        mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 disconnected->reason);
        mesh_layer = esp_mesh_get_layer();
        mesh_netifs_stop();
    }
    break;
    case MESH_EVENT_LAYER_CHANGE: {
        mesh_event_layer_change_t *layer_change = (mesh_event_layer_change_t *)event_data;
        mesh_layer = layer_change->new_layer;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                 last_layer, mesh_layer,
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "");
        last_layer = mesh_layer;
    }
    break;
    case MESH_EVENT_ROOT_ADDRESS: {
        mesh_event_root_address_t *root_addr = (mesh_event_root_address_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:"MACSTR"",
                 MAC2STR(root_addr->addr));
    }
    break;
    case MESH_EVENT_VOTE_STARTED: {
        mesh_event_vote_started_t *vote_started = (mesh_event_vote_started_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:"MACSTR"",
                 vote_started->attempts,
                 vote_started->reason,
                 MAC2STR(vote_started->rc_addr.addr));
    }
    break;
    case MESH_EVENT_VOTE_STOPPED: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
        break;
    }
    case MESH_EVENT_ROOT_SWITCH_REQ: {
        mesh_event_root_switch_req_t *switch_req = (mesh_event_root_switch_req_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:"MACSTR"",
                 switch_req->reason,
                 MAC2STR( switch_req->rc_addr.addr));
    }
    break;
    case MESH_EVENT_ROOT_SWITCH_ACK: {
        /* new root */
        mesh_layer = esp_mesh_get_layer();
        esp_mesh_get_parent_bssid(&mesh_parent_addr);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:"MACSTR"", mesh_layer, MAC2STR(mesh_parent_addr.addr));
    }
    break;
    case MESH_EVENT_TODS_STATE: {
        mesh_event_toDS_state_t *toDs_state = (mesh_event_toDS_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
    }
    break;
    case MESH_EVENT_ROOT_FIXED: {
        mesh_event_root_fixed_t *root_fixed = (mesh_event_root_fixed_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                 root_fixed->is_fixed ? "fixed" : "not fixed");
    }
    break;
    case MESH_EVENT_ROOT_ASKED_YIELD: {
        mesh_event_root_conflict_t *root_conflict = (mesh_event_root_conflict_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_ASKED_YIELD>"MACSTR", rssi:%d, capacity:%d",
                 MAC2STR(root_conflict->addr),
                 root_conflict->rssi,
                 root_conflict->capacity);
    }
    break;
    case MESH_EVENT_CHANNEL_SWITCH: {
        mesh_event_channel_switch_t *channel_switch = (mesh_event_channel_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", channel_switch->channel);
    }
    break;
    case MESH_EVENT_SCAN_DONE: {
        mesh_event_scan_done_t *scan_done = (mesh_event_scan_done_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                 scan_done->number);
    }
    break;
    case MESH_EVENT_NETWORK_STATE: {
        mesh_event_network_state_t *network_state = (mesh_event_network_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                 network_state->is_rootless);
    }
    break;
    case MESH_EVENT_STOP_RECONNECTION: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
    }
    break;
    case MESH_EVENT_FIND_NETWORK: {
        mesh_event_find_network_t *find_network = (mesh_event_find_network_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:"MACSTR"",
                 find_network->channel, MAC2STR(find_network->router_bssid));
    }
    break;
    case MESH_EVENT_ROUTER_SWITCH: {
        mesh_event_router_switch_t *router_switch = (mesh_event_router_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, "MACSTR"",
                 router_switch->ssid, router_switch->channel, MAC2STR(router_switch->bssid));
    }
    break;
    default:
        ESP_LOGI(MESH_TAG, "unknown id:%d", event_id);
        break;
    }
}
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

//--------------------------------------------------- IP HANDLER
void ip_event_handler(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));
    s_current_ip.addr = event->ip_info.ip.addr;

    esp_mesh_comm_mqtt_task_start();
}

//--------------------------------------------------- BUTTON HANDLER
void IRAM_ATTR button_isr_handler(void* arg)
{
    button_pressed = true;
}
//---------------------------------------------------
void app_main(void)
{
    // SPIFFS ------------------------------------------------------------------------
        //Initialize SPIFFS
	esp_vfs_spiffs_conf_t conf = {
		.base_path = "/spiffs",
		.partition_label = NULL,
		.max_files = 12,
		.format_if_mount_failed =true
	};

	// Use settings defined above toinitialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is anall-in-one convenience function.
	esp_err_t ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG1, "Failed to mount or format filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG1, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG1, "Failed to initialize SPIFFS (%s)",esp_err_to_name(ret));
		}
		return;
	}

	size_t total = 0, used = 0;
	ret = esp_spiffs_info(NULL, &total,&used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG1,"Failed to get SPIFFS partition information (%s)",esp_err_to_name(ret));
	} else {
		ESP_LOGI(TAG1,"Partition size: total: %d, used: %d", total, used);
	}

	SPIFFS_Directory("/spiffs/");
    //----------------------------------------------------------------------------

    //--------------------------------------------------- MESH INIT
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_DEBUG);
    ESP_ERROR_CHECK(nvs_flash_init());
    /*  tcpip initialization */
    ESP_ERROR_CHECK(esp_netif_init());
    /*  event initialization */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /*  crete network interfaces for mesh (only station instance saved for further manipulation, soft AP instance ignored */
    ESP_ERROR_CHECK(mesh_netifs_init(recv_cb));

    /*  wifi initialization */
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    /* router */
    cfg.channel = CONFIG_MESH_CHANNEL;
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, CONFIG_MESH_ROUTER_PASSWD,
           strlen(CONFIG_MESH_ROUTER_PASSWD));
    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    cfg.mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
           strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    /* mesh start */
    ESP_ERROR_CHECK(esp_mesh_start());
    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%d, %s\n",  esp_get_free_heap_size(),
             esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed");
    //---------------------------------------------------
    
    //--------------------------------------------------- LCD AND SPIFFS INIT
    spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_CS_GPIO, CONFIG_DC_GPIO, CONFIG_RESET_GPIO, CONFIG_BL_GPIO);
	lcdInit(&dev, CONFIG_WIDTH, CONFIG_HEIGHT, CONFIG_OFFSETX, CONFIG_OFFSETY);
    lcdFillScreen(&dev, BLACK);
    //---------------------------------------------------

    //--------------------------------------------------- BUTTON INIT
    gpio_config_t io_conf;
    //configure button pin as input
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BUTTON_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_POSEDGE);
    gpio_install_isr_service(0);
    //register ISR for button interrupt
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);
    //---------------------------------------------------
}
