#include "com.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "router.h"


#define ESP_WIFI_CTRL_QUEUE_LENGTH (2)
#define ESP_WIFI_CTRL_QUEUE_SIZE (sizeof(esp_routable_packet_t))
#define ESP_PM_QUEUE_LENGTH (2)
#define ESP_PM_QUEUE_SIZE (sizeof(esp_routable_packet_t))
#define ESP_APP_QUEUE_LENGTH (2)
#define ESP_APP_QUEUE_SIZE (sizeof(esp_routable_packet_t))
#define ESP_TEST_QUEUE_LENGTH (2)
#define ESP_TEST_QUEUE_SIZE (sizeof(esp_routable_packet_t))

static xQueueHandle espWiFiCTRLQueue;
static xQueueHandle espPMQueue;
static xQueueHandle espAPPQueue;
static xQueueHandle espTESTQueue;

static esp_routable_packet_t rxp;

// This is probably too big, but let's keep things simple....
#define ESP_ROUTER_TX_QUEUE_LENGTH (4)
#define ESP_ROUTER_RX_QUEUE_LENGTH (4)
#define ESP_ROUTER_QUEUE_SIZE (sizeof(esp_routable_packet_t))

static xQueueHandle espRxQueue;
static xQueueHandle espTxQueue;

static void com_rx(void* _param) {

  while (1) {
    ESP_LOGD("COM", "Waiting for packet");
    xQueueReceive(espRxQueue, &rxp, (TickType_t) portMAX_DELAY);
    ESP_LOGD("COM", "Received packet for 0x%02X", rxp.route.destination);
    ESP_LOG_BUFFER_HEX_LEVEL("COM", &rxp, 10, ESP_LOG_DEBUG);
    switch (rxp.route.function) {
      case TEST:
        xQueueSend(espTESTQueue, &rxp, (TickType_t) portMAX_DELAY);
        break;
      case WIFI_CTRL:
        xQueueSend(espWiFiCTRLQueue, &rxp, (TickType_t) portMAX_DELAY);
        break; 
      default:
        ESP_LOGW("COM", "Cannot handle 0x%02X", rxp.route.function);
    }
  }
}

void com_init() {
  espWiFiCTRLQueue = xQueueCreate(ESP_WIFI_CTRL_QUEUE_LENGTH, ESP_WIFI_CTRL_QUEUE_SIZE);
  espPMQueue = xQueueCreate(ESP_PM_QUEUE_LENGTH, ESP_PM_QUEUE_SIZE);
  espAPPQueue = xQueueCreate(ESP_APP_QUEUE_LENGTH, ESP_APP_QUEUE_SIZE);
  espTESTQueue = xQueueCreate(ESP_TEST_QUEUE_LENGTH, ESP_TEST_QUEUE_SIZE);

  espRxQueue = xQueueCreate(ESP_ROUTER_RX_QUEUE_LENGTH, ESP_ROUTER_QUEUE_SIZE);
  espTxQueue = xQueueCreate(ESP_ROUTER_TX_QUEUE_LENGTH, ESP_ROUTER_QUEUE_SIZE);
  
  xTaskCreate(com_rx, "COM RX", 10000, NULL, 1, NULL);

  ESP_LOGI("COM", "Initialized");
}

void com_receive_test_blocking(esp_routable_packet_t * packet) {
  xQueueReceive(espTESTQueue, packet, (TickType_t) portMAX_DELAY);
}

void com_receive_wifi_ctrl_blocking(esp_routable_packet_t * packet) {
  xQueueReceive(espWiFiCTRLQueue, packet, (TickType_t) portMAX_DELAY);
}

void com_send_blocking(esp_routable_packet_t * packet) {
  xQueueSend(espTxQueue, packet, (TickType_t) portMAX_DELAY);
}

void com_router_post_packet(esp_packet_t * packet) {
  xQueueSend(espRxQueue, packet, (TickType_t) portMAX_DELAY);
}

void com_router_get_packet(esp_packet_t * packet) {
  xQueueReceive(espTxQueue, packet, (TickType_t) portMAX_DELAY);
}
