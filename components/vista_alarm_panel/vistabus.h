#pragma once

/*
The Vistabus class interfaces with the Ademco Vista Keybus via one (or optionally two) hardware UARTs on the ESP32 family devices.
The primary RX / TX traffic (yellow / green wires) is collected in the first required UART via a FreeRTOS Task that handles both data receipt and writing
to the bus. If configured, a second FreeRTOS task is instantiated to handle the second optional UART to receive additional data 
emitted to the bus on the TX (green wire) associated with expansion devices such as RF wireless sensors.

Data from expander modules and relay mddules is not currently handled as I have no devices with which to test.

Data is exchanged / exposed with calling application via separate FreeRTOS Queues.

Written by Tim Pletcher.
Date: 2-Feb-2025

*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "hal/uart_ll.h"
#include <vector>
#include <memory>
#include "esphome/core/defines.h"
#include "helper_structs.h"
#include "helper_funcs.h"

#ifndef LEGACY_SE_PROTOCOL
class Vista20P;
#else
class VistaSE;
#endif

class VistaBus
{
public:
    VistaBus();
    ~VistaBus();
    void begin(int uartnum, int rxpin, int txpin, int extuartnum, int monitorpin);
    bool stop();
    bool write(const char * data_to_write, int size, int keypadaddress, int sequence);
    bool writedirect(const char * hex_data_to_write, int size, int keypadaddress, int sequence);
    bool connected();
    bool read_packet(char * data, int &len, int &type, int &src, bool with_delay = false);
    void emulateLRR(bool enabled);  //ToDo: verify setter is still needed.
    void add_emulated_expander(uint8_t zone);
    void emulateRFR(uint8_t address);  //ToDo: verify setter is still needed.
    void setExpFaultBits(uint8_t zone, bool fault);
    void setExpTamper(uint8_t zone, bool tamper_active);
    void sendRFmsg(uint32_t serial, uint8_t msg);
    QueueHandle_t uartevtQueue;
    QueueHandle_t sendQueue;
    QueueHandle_t receiveQueue;
    QueueHandle_t deviceMsgQueue;
    TaskHandle_t rx_tx_task_Handle;
    TaskHandle_t monitor_rx_task_Handle;
    uart_port_t uart_num;
    uart_port_t ext_uart_num;
    gpio_num_t rx_pin;
    bool LRRemulation;
    bool EXPemulation;
    bool RFRemulation;
    
    struct EmulatedExpander  
    {   
        uint8_t address{0};
        char fault_NO_Bits{0};
        char fault_NC_Bits{0xFF};
    };
    std::vector<EmulatedExpander> emulated_expanders{};
    EmulatedExpander *getExpander(uint8_t address);
    EmulatedRFReceiver emulated_rf_receiver;

private:
#ifndef LEGACY_SE_PROTOCOL
    Vista20P* vprotocol;
#else
    VistaSE* vprotocol;
#endif
    const char* const TAG = "vistabus";
    gpio_num_t tx_pin;
    gpio_num_t monitor_pin;
    bool panel_connected;
    bool stop_requested;

    static void rx_tx_task_start(void *args );
    static void monitor_rx_task_start(void *args);
    void rx_tx_task(void * args);
    void monitor_rx_task(void * args);

    void requestF1(uint8_t address);

    int64_t last_data_received = 0;
    int64_t request_F1_time = 0;

    void capture_pulse_pattern(gpio_num_t rx_pin);

    void init_uart(uart_port_t u_n, gpio_num_t rx_pin, gpio_num_t tx_pin);
};