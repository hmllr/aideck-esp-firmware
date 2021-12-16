// SPI Transport implementation

#include "uart_transport.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

#define UART_BUFFER_LEN (UART_TRANSPORT_MTU + 2)

#define TX_QUEUE_LENGTH 10
#define RX_QUEUE_LENGTH 10

#define DEBUG(...) printf(__VA_ARGS__)
//#define DEBUG(...)


static xQueueHandle tx_queue;
static xQueueHandle rx_queue;

static uart_transport_packet_t txp;
static uart_transport_packet_t rxp;

static EventGroupHandle_t evGroup;

#define TXD_PIN (GPIO_NUM_1) // Nina 22 => 1
#define RXD_PIN (GPIO_NUM_3) // Nina 23 => 3

#define CTS_EVENT (1<<0)
#define CTR_EVENT (1<<1)
#define TXQ_EVENT (1<<2)

static void uart_tx_task(void* _param) {
  uint8_t ctr[] = {0xFF, 0x00};
  EventBits_t evBits = 0;

     // We need to hold off here to make sure that the RX task
    // has started up and is waiting for chars, otherwise we might send
    // CTR and miss CTS (which means that the STM32 will stop sending CTS
    // too early and we cannot sync)
    vTaskDelay(100);

    do {
      uart_write_bytes(UART_NUM_0, &ctr, sizeof(ctr));
      vTaskDelay(10);
      evBits = xEventGroupGetBits(evGroup);
    } while ((evBits & CTS_EVENT) != CTS_EVENT);


  while(1) {
    // If we have nothing to send then wait, either for something to be
    // queued or for a request to send CTR
    if (uxQueueMessagesWaiting(tx_queue) == 0) {
      ESP_LOGD("UART", "Waiting for CTR/TXQ");
      evBits = xEventGroupWaitBits(evGroup,
                                CTR_EVENT | TXQ_EVENT,
                                pdTRUE, // Clear bits before returning
                                pdFALSE, // Wait for any bit
                                portMAX_DELAY);
      if ((evBits & CTR_EVENT) == CTR_EVENT) {
        ESP_LOGD("UART", "Sent CTR");
        uart_write_bytes(UART_NUM_0, &ctr, sizeof(ctr));
      }
    }

    if (uxQueueMessagesWaiting(tx_queue) > 0) {
      // Dequeue and wait for either CTS or CTR
      xQueueReceive(tx_queue, &txp, 0);
      do {
        ESP_LOGD("UART", "Waiting for CTR/CTS");
        evBits = xEventGroupWaitBits(evGroup,
                                CTR_EVENT | CTS_EVENT,
                                pdTRUE, // Clear bits before returning
                                pdFALSE, // Wait for any bit
                                portMAX_DELAY);
        if ((evBits & CTR_EVENT) == CTR_EVENT) {
          ESP_LOGD("UART", "Sent CTR");
          uart_write_bytes(UART_NUM_0, &ctr, sizeof(ctr));
        }       
      } while ((evBits & CTS_EVENT) != CTS_EVENT);
      ESP_LOGD("UART", "Sending packet");
      txp.start = 0xFF;
      uart_write_bytes(UART_NUM_0, &txp, txp.length+2);
    }      
  }
}


static void uart_rx_task(void* _param) {

    while(1) {
        
        do {
          uart_read_bytes(UART_NUM_0, &rxp.start, 1, (TickType_t)portMAX_DELAY);
        } while (rxp.start != 0xFF);

        uart_read_bytes(UART_NUM_0, &rxp.length, 1, (TickType_t)portMAX_DELAY);

        if (rxp.length == 0) {
          ESP_LOGD("UART", "Received CTS");
          xEventGroupSetBits(evGroup, CTS_EVENT);
        } else {
          uart_read_bytes(UART_NUM_0, rxp.data, rxp.length, (TickType_t)portMAX_DELAY);
          ESP_LOGD("UART", "Received packet");
          // Post on RX queue and send flow control
          // Optimize a bit here
          if (uxQueueSpacesAvailable(rx_queue) > 0) {
            xEventGroupSetBits(evGroup, CTR_EVENT);
            xQueueSend(rx_queue, &rxp, portMAX_DELAY);
          } else {
            xQueueSend(rx_queue, &rxp, portMAX_DELAY);
            xEventGroupSetBits(evGroup, CTR_EVENT);
          }
        }
    }
}

void uart_transport_init() {

    // Setting up synchronization items
    tx_queue = xQueueCreate(TX_QUEUE_LENGTH, sizeof(uart_transport_packet_t));
    rx_queue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(uart_transport_packet_t));

    evGroup = xEventGroupCreate();

    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        //.source_clk = UART_SCLK_APB,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_0, UART_TRANSPORT_MTU * 2, UART_TRANSPORT_MTU * 2, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Allocating buffers
    /*tx_buffer = (char*)heap_caps_malloc(UART_BUFFER_LEN, MALLOC_CAP_DMA);
    rx_buffer = (char*)heap_caps_malloc(UART_BUFFER_LEN, MALLOC_CAP_DMA);*/

    // Launching SPI communication task
    xTaskCreate(uart_tx_task, "UART TX transport", 10000, NULL, 1, NULL);
    xTaskCreate(uart_rx_task, "UART RX transport", 10000, NULL, 1, NULL);

    ESP_LOGI("UART", "Transport initialized");
}

void uart_transport_send(const uart_transport_packet_t *packet) {
    xQueueSend(tx_queue, packet, portMAX_DELAY);
    xEventGroupSetBits(evGroup, TXQ_EVENT);
}

void uart_transport_receive(uart_transport_packet_t *packet) {
    xQueueReceive(rx_queue, packet, portMAX_DELAY);
}