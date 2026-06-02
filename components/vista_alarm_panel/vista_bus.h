// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

/*
The Vistabus class interfaces with the Ademco Vista Keybus via one (or optionally two) hardware UARTs on the ESP32 family devices.
The primary RX / TX traffic (yellow / green wires) is collected in the first required UART via a FreeRTOS Task that handles both data receipt and writing
to the bus. If configured, a second FreeRTOS task is instantiated to handle the second optional UART to receive additional data
emitted to the bus on the TX (green wire) associated with expansion devices such as RF wireless sensors.

Data from expander modules and relay mddules is not currently handled as I have no devices with which to test.

Data is exchanged / exposed with calling application via separate FreeRTOS Queues.

Written by Tim Tim Pletcherer.
Date: 2-Feb-2025

*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "hal/uart_ll.h"
#include <vector>
#include <memory>
#include "esphome/core/log.h"
#include "esphome/core/defines.h"
#include "helper_structs.h"
#include "helper_funcs.h"

// Select the ECP protocol implementation at compile time.
// Vista20P handles modern 20P-series panels; VistaSE handles older SE-series panels.
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

    // Initialise the primary UART (uartnum/rxpin/txpin) and optionally a
    // second monitor-only UART (extuartnum/monitorpin) for expansion-bus traffic.
    void begin(int uartnum, int rxpin, int txpin, int extuartnum = -1, int monitorpin = -1);

    // Signal both tasks to shut down and wait for them to exit before deleting
    // the UART drivers.  Returns true when the shutdown is complete.
    bool stop();

    // Suspend / resume both UART tasks around flash-cache-disabling events
    // (OTA writes).  Safe to call from any task context; no-ops if the task
    // handles are null.
    void suspend_monitor()
    {
        if (monitor_rx_task_Handle) { vTaskSuspend(monitor_rx_task_Handle); taskYIELD(); }
    }
    void suspend_rx_tx()
    {
        if (rx_tx_task_Handle) { vTaskSuspend(rx_tx_task_Handle); taskYIELD(); }
    }
    void resume_tasks()
    {
        if (rx_tx_task_Handle)    vTaskResume(rx_tx_task_Handle);
        if (monitor_rx_task_Handle) vTaskResume(monitor_rx_task_Handle);
    }

    // Queue a keypad-command payload for transmission.  The payload is treated
    // as raw bytes (not a C-string) so the full 'size' bytes are copied via
    // memcpy.  type=1 indicates a normal keypad write.
    bool write(const char * data_to_write, int size, int keypadaddress, int sequence);

    // Queue a pre-encoded hex payload for direct transmission without further
    // protocol encoding.  type=0 bypasses the normal encoding path.
    bool writedirect(const char * hex_data_to_write, int size, int keypadaddress, int sequence);

    // Returns true if a valid panel data frame was received within the last 30 s.
    bool connected() const;

    // Dequeue one received packet from receiveQueue.  If with_delay is true the
    // call blocks indefinitely until a packet arrives; otherwise it returns
    // immediately with false if the queue is empty.
    bool read_packet(char * data, int &len, int &type, int &src, bool with_delay = false);

    // Enable / disable emulation of a Long-Range Radio (LRR) reporting module.
    void emulateLRR(bool enabled);

    // Register a zone number so the bus driver will emulate the zone-expander
    // module that owns that zone.
    void add_emulated_expander(uint8_t zone);

    // Enable emulation of an RF receiver at the given keybus address.
    void emulateRFR(uint8_t address);

    // Set or clear the status bit (zone open/closed) for a zone on its emulated
    // expander and notify the panel via the deviceMsgQueue.
    void setZoneStatusBit(uint8_t zone, bool open);

    // Forward an RF sensor message (identified by serial number and message
    // byte) to the panel through the emulated RF receiver.
    void sendRFmsg(uint32_t serial, uint8_t msg);

    // --- FreeRTOS inter-task communication handles ---
    // uartevtQueue  – fed by the ESP-IDF UART ISR; consumed by handle_UART_events().
    // sendQueue     – commands queued by the application for transmission.
    // receiveQueue  – decoded packets delivered to the application via read_packet().
    // deviceMsgQueue– pending expander / RF device notifications waiting for an F1 poll slot.
    // rf_direct_queue – RfDirectMsg items posted by CC1101Receiver for the fast
    //                   direct-to-HA path; consumed by VistaESPHome::rf_direct_task().
    QueueHandle_t uartevtQueue;
    QueueHandle_t sendQueue;
    QueueHandle_t receiveQueue;
    QueueHandle_t deviceMsgQueue;
    QueueHandle_t rf_direct_queue;

    // Task handles used to track task lifecycle; set to nullptr on exit so
    // stop() can detect when each task has finished.
    TaskHandle_t rx_tx_task_Handle;
    TaskHandle_t monitor_rx_task_Handle;

    uart_port_t uart_num;       // Primary UART (keypad RX+TX).
    uart_port_t ext_uart_num;   // Secondary (monitor) UART for expansion-bus traffic.
    gpio_num_t  rx_pin;         // Primary UART RX gpio.

    bool LRRemulation;  // True when an LRR module is being emulated.
    bool EXPemulation;  // True when at least one zone-expander is being emulated.
    bool RFRemulation;  // True when an RF receiver is being emulated.

    // Represents a single emulated zone-expander module.
    // zone_status_bits – per-zone open/closed status; bit set = zone open (faulted),
    //                    bit cleared = zone closed (secure).  Sent as the first data
    //                    byte of the F7 poll response.
    // supervision_bits – per-zone supervision (EOL resistor) status; bit cleared during
    //                    enrollment via add_emulated_expander() to signal "supervised /
    //                    EOL OK"; sent as the second data byte of the F7 poll response.
    struct EmulatedExpander
    {
        uint8_t address{0};
        char zone_status_bits{0};
        char supervision_bits{0xFF};
    };

    std::vector<EmulatedExpander> emulated_expanders{};

    // Look up an emulated expander by its keybus address; returns nullptr if
    // no expander with that address has been registered.
    EmulatedExpander *getExpander(uint8_t address);

    EmulatedRFReceiver emulated_rf_receiver;

private:
// Owning handle to the active ECP protocol handler (Vista20P or VistaSE),
// which decodes/encodes all panel wire-format packets.  Destroyed automatically
// with the VistaBus; ~VistaBus() is defined in vista_bus.cpp where the concrete
// type is complete, so the unique_ptr can be deleted there.
#ifndef LEGACY_SE_PROTOCOL
    std::unique_ptr<Vista20P> vprotocol;
#else
    std::unique_ptr<VistaSE> vprotocol;
#endif

    static constexpr const char *TAG = "vista-bus";
    gpio_num_t tx_pin;          // Primary UART TX gpio.
    gpio_num_t monitor_pin;     // RX-only gpio for the expansion-bus monitor UART.
    bool panel_connected;       // Becomes false if no data arrives for 30 s.
    bool stop_requested;        // Set by stop(); tasks exit their loops when true.

    // Static trampoline functions required by xTaskCreate (which takes a plain
    // function pointer).  Each casts the void* arg back to VistaBus* and
    // delegates to the corresponding member function.
    static void rx_tx_task_start(void *args );
    static void monitor_rx_task_start(void *args);

    // Primary task: processes UART events, dispatches received panel frames,
    // and sends queued keypad commands.  Runs at the highest FreeRTOS priority.
    void rx_tx_task(void * args);

    // Secondary task: reads expansion-bus traffic from the monitor UART and
    // dispatches it for RF / expander decoding.
    void monitor_rx_task(void * args);

    // Build a type-2 (pulse-only) SendPacket for the given keybus address and
    // push it onto sendQueue.  Used to solicit an F1 response from a device.
    void requestF1(uint8_t address);

    int64_t last_data_received = 0; // esp_timer timestamp of last received byte.
    int64_t request_F1_time = 0;    // Timestamp of last F1 poll request; rate-limits polls.

    // Debug helper (compiled only when DEBUG_PULSE is defined): uses the RMT
    // peripheral to capture and log the raw pulse widths on rx_pin.
    void capture_pulse_pattern(gpio_num_t rx_pin);

    // Configure an ESP-IDF UART driver for ECP-bus operation: 4800 baud, 8E2,
    // inverted RX (and TX if a tx_pin is provided).  When tx_pin == -1 the
    // port is installed in RX-only mode (no event queue).
    void init_uart(uart_port_t u_n, gpio_num_t rx_pin, gpio_num_t tx_pin);
};
