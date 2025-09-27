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

#define F6_ACK_MESSAGE_LENGTH 4
#define F7_MESSAGE_LENGTH 45
#define F9_MESSAGE_LENGTH 12
#define F9_EXT_MESSAGE_LENGTH 8
#define FE_EXT_MESSAGE_LENGTH 8
#define FA_MESSAGE_LENGTH 6
#define FB_MESSAGE_LENGTH 5

#define RX_BUF_SIZE (128)
#define UART_RX_TASK_STACK_SIZE (4096)
#define UART_RX_EXT_TASK_STACK_SIZE (3072)
#define UART_DELAY 10


struct ReceivedPacket  
{
    int type; //0 = yellow wire, 1 = green wire
    int source{0};
    char payload[48];
    int size; 
};

struct SendPacket  
{
    int type{-1}; //0 = hex, 1 = text, 2 = no_ack_expected
    char payload[24];
    int keypadaddress;
    int size;
};

struct gpioTaskArgs  
{
    TaskHandle_t task_handle;
    int pin;
};


class VistaBus
{
public:
    VistaBus();
    ~VistaBus();
    void begin(int uartnum, int rxpin, int txpin, int extuartnum, int monitorpin);
    bool stop();
    bool write(const char * data_to_write, int size, int keypadaddress);
    bool writedirect(const char * hex_data_to_write, int size, int keypadaddress);
    bool connected();
    bool read_packet(char * data, int &len, int &type, int &src, bool with_delay = false);
    void emulateLRR(bool enabled);
    void add_emulated_expander(uint8_t zone);
    void emulateRFR(uint8_t address);
    void setExpFaultBits(uint8_t zone, bool fault);
    void setExpTamper(uint8_t zone, bool tamper_active);
    void sendRFmsg(uint32_t serial, uint8_t msg);

protected:
    const char* const TAG = "vistabus";
    int rxPin, txPin;
    int uartNum;
    int extuartNum;
    int monitorPin;
    bool panel_connected;
    bool stop_requested;
    bool LRRemulation;
    bool EXPemulation;
    bool RFRemulation;
    static void rx_tx_task_start(void *args );
    static void monitor_rx_task_start(void *args);
    void rx_tx_task(void * args);
    void monitor_rx_task(void * args);
    void processF9(const char * cbuf);
    void processFA(const char * cbuf);
    void processFB(const char * cbuf);
    void requestF1(uint8_t address);

    void capture_pulse_pattern(gpio_num_t rx_pin);

    bool mark_pulse(uint8_t address);
    static void gpio_isr_handler(void * args);

    struct DeviceMsg
    {
        uint8_t address{255};
        uint32_t source{0}; //either zone or serial
        uint8_t msg{0};
    };

    struct emulatedExpander  
    {   
        uint8_t address{0};
        char fault_NO_Bits{0};
        char fault_NC_Bits{0xFF};
    };

    struct emulatedRFReceiver  
    {   
        uint8_t address{0};
    };

    emulatedExpander *getExpander(uint8_t address);
    std::vector<emulatedExpander> emulated_expanders{};
    emulatedRFReceiver emulated_rf_receiver;
    TaskHandle_t rx_tx_task_Handle;
    TaskHandle_t monitor_rx_task_Handle;
    QueueHandle_t receiveQueue;
    QueueHandle_t sendQueue;
    QueueHandle_t deviceMsgQueue;
    QueueHandle_t uartevtQueue;

    void init_uart(uart_port_t u_n, gpio_num_t rx_pin, gpio_num_t tx_pin);
};