/* MQTT Example using plain mbedTLS sockets
 *
 * Adapted from the ssl_client1 example in mbedtls.
 *
 * Original Copyright (C) 2006-2016, ARM Limited, All Rights Reserved, Apache 2.0 License.
 * Additions Copyright (C) Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD, Apache 2.0 License.
 * Additions Copyright (C) Copyright 2017 pcbreflux, Apache 2.0 License.
 * Additions Copyright (C) Copyright 2017 iotcebu, Apache 2.0 License.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string.h>
#include <stdlib.h>

#include "sdkconfig.h"

#include "MQTTClient.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_deep_sleep.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

#include "DHT22.h"

/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
 */

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

/* Constants that aren't configurable in menuconfig */
#define MQTT_SERVER CONFIG_MQTT_BROKER
#define MQTT_USER CONFIG_MQTT_USER
#define MQTT_PASS CONFIG_MQTT_PASSWORD
#define MQTT_PORT CONFIG_MQTT_WEBSOCKET_PORT
#define MQTT_TOPIC "iotcebu/testuser/pwm/#"
#define MQTT_CLIENTID CONFIG_MQTT_CLIENT_ID
#define MQTT_WEBSOCKET 1  // 0=no 1=yes
#define MQTT_BUF_SIZE 512

#define DHT22_IO  16 

xSemaphoreHandle print_mux;

static unsigned char mqtt_sendBuf[MQTT_BUF_SIZE];
static unsigned char mqtt_readBuf[MQTT_BUF_SIZE];

static const char *TAG = "MQTTS";

/* Variable holding number of times ESP32 restarted since first boot.
* It is placed into RTC memory using RTC_DATA_ATTR and
* maintains its value when ESP32 wakes from deep sleep.
*/
RTC_DATA_ATTR static int boot_count = 0;


/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t wifi_event_group;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            /* This is a workaround as ESP32 WiFi libs don't currently
               auto-reassociate. */
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}


static void mqtt_task(void *pvParameters)
{
    int ret;
    float temperature = 0.0;
    float humidity = 0.0;

    Network network;

    // Turn the led indicator on (to signal we are connected and alive)
    gpio_pad_select_gpio(CONFIG_LED_INDICATOR);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(CONFIG_LED_INDICATOR, GPIO_MODE_OUTPUT);
    // Turn it on
    gpio_set_level(CONFIG_LED_INDICATOR, 1);


    setDHTgpio(DHT22_IO);
    ESP_LOGD(TAG,"Wait for WiFi ...");
    /* Wait for the callback to set the CONNECTED_BIT in the
       event group.
     */
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
            false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to AP");

    ESP_LOGI(TAG, "Start MQTT Task ...");

    MQTTClient client;
    NetworkInit(&network);
    network.websocket = MQTT_WEBSOCKET;

    ESP_LOGD(TAG,"NetworkConnect %s:%d ...",MQTT_SERVER,MQTT_PORT);
    ret = NetworkConnect(&network, MQTT_SERVER, MQTT_PORT);
    if (ret != 0) {
        ESP_LOGI(TAG, "NetworkConnect not SUCCESS: %d", ret);
        goto exit;
    }
    ESP_LOGI(TAG,"MQTTClientInit  ...");
    MQTTClientInit(&client, &network,
            2000,            // command_timeout_ms
            mqtt_sendBuf,         //sendbuf,
            MQTT_BUF_SIZE, //sendbuf_size,
            mqtt_readBuf,         //readbuf,
            MQTT_BUF_SIZE  //readbuf_size
            );

    char buf[30];
    MQTTString clientId = MQTTString_initializer;
    sprintf(buf, MQTT_CLIENTID);
    ESP_LOGI(TAG,"MQTTClientInit  %s",buf);
    clientId.cstring = buf;

    MQTTString username = MQTTString_initializer;
    username.cstring = MQTT_USER;

    MQTTString password = MQTTString_initializer;
    password.cstring = MQTT_PASS;

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.clientID          = clientId;
    data.willFlag          = 0;
    data.MQTTVersion       = 4; // 3 = 3.1 4 = 3.1.1
    data.keepAliveInterval = 5;
    data.cleansession      = 1;
    data.username          = username;
    data.password          = password;

    ESP_LOGI(TAG,"MQTTConnect  ...");
    ret = MQTTConnect(&client, &data);
    if (ret != SUCCESS) {
        ESP_LOGI(TAG, "MQTTConnect not SUCCESS: %d", ret);
        goto exit;
    }

    ESP_LOGI(TAG, "MQTT Connected!");
    char msgbuf[200];
    // Read the sensor and publish data to MQTT 
    {
        ret = readDHT();
        if (ret != DHT_OK)
            errorHandler(ret);
        else
        {
            temperature = getTemperature();
            humidity = getHumidity();

            ESP_LOGI(TAG, "Temperature : %.1f", temperature);
            ESP_LOGI(TAG, "Humidity : %.1f", humidity);
        }

        MQTTMessage message;
        sprintf(msgbuf, "{\"temperature\":%.2f, \"humidity\": %.2f }", temperature, humidity);

        ESP_LOGI(TAG, "MQTTPublish  ... %s",msgbuf);
        message.qos = QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void*)msgbuf;
        message.payloadlen = strlen(msgbuf)+1;

        ret = MQTTPublish(&client, "iotcebu/testuser/weather", &message);
        if (ret != SUCCESS) {
            ESP_LOGI(TAG, "MQTTPublish not SUCCESS: %d", ret);
            goto exit;
        }
    }
exit:
    MQTTDisconnect(&client);
    NetworkDisconnect(&network);

    // Set the LED indicator to Off
    gpio_set_level(CONFIG_LED_INDICATOR, 0);

    const int deep_sleep_sec = 10;
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", deep_sleep_sec);
    esp_deep_sleep(1000000LL * deep_sleep_sec);
}


void app_main()
{
    boot_count ++;

    nvs_flash_init();
    initialise_wifi();

    // MQTT and TLS needs a deep stack, 12288 seems to work, anything
    // less will get us a stack overflow error
    xTaskCreate(&mqtt_task, "mqtt_task", 12288, NULL, 5, NULL);

}
