// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#include "vista_bus.h"

// Select the matching ECP protocol implementation.
#ifndef LEGACY_SE_PROTOCOL
#include "ecp_prot_vista20P.h"
#else
#include "ecp_prot_vistaSE.h"
#endif

// RMT capture is only compiled in when pulse-width debugging is requested.
#ifdef DEBUG_PULSE
#include "driver/rmt_rx.h"
#endif


// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

VistaBus::VistaBus()
{
    // Instantiate the correct protocol handler for this build.
    // The protocol object is given a back-reference to this VistaBus so it
    // can access the queues and GPIO information it needs.
#ifndef LEGACY_SE_PROTOCOL
    vprotocol = new Vista20P(*this);
#else
    vprotocol = new VistaSE(*this);
#endif

    // Pre-allocate the inter-task queues so they are ready before begin() is
    // called.  Queue depths are chosen to hold a small burst without blocking
    // the real-time UART task.
    this->receiveQueue    = xQueueCreate(15, sizeof(ReceivedPacket)); // app reads decoded frames here
    this->sendQueue       = xQueueCreate(8,  sizeof(SendPacket));     // app posts commands here
    this->deviceMsgQueue  = xQueueCreate(4,  sizeof(DeviceMsg));      // expander/RF notifications
    this->rf_direct_queue = xQueueCreate(8,  sizeof(RfDirectMsg));    // fast direct-to-HA RF path

    this->rx_tx_task_Handle      = nullptr;
    this->monitor_rx_task_Handle = nullptr;
    this->uartevtQueue           = nullptr;
    this->panel_connected  = false;
    this->stop_requested   = false;
    this->LRRemulation     = false;
    this->EXPemulation     = false;
    this->RFRemulation     = false;
}

VistaBus::~VistaBus()
{
    // If the UART task is still running, request a clean shutdown first so
    // the UART driver is not deleted while the task is using it.
    if (this->rx_tx_task_Handle != nullptr)
        stop();

    vQueueDelete(this->receiveQueue);
    vQueueDelete(this->sendQueue);
    vQueueDelete(this->deviceMsgQueue);
    if (this->rf_direct_queue != nullptr)
        vQueueDelete(this->rf_direct_queue);

    // Fix: vprotocol was new-allocated in the constructor but never deleted.
    delete vprotocol;
    vprotocol = nullptr;
}


// ---------------------------------------------------------------------------
// begin() / stop()
// ---------------------------------------------------------------------------

void VistaBus::begin(int uartnum, int rxpin, int txpin, int extuartnum, int monitorpin)
{
    this->uart_num      = static_cast<uart_port_t>(uartnum);
    this->rx_pin        = static_cast<gpio_num_t>(rxpin);
    this->tx_pin        = static_cast<gpio_num_t>(txpin);
    this->ext_uart_num  = static_cast<uart_port_t>(extuartnum);
    this->monitor_pin   = static_cast<gpio_num_t>(monitorpin);

    // Abort early if queue memory was not allocated (e.g. heap exhaustion).
    if (this->receiveQueue == nullptr || this->sendQueue == nullptr
            || this->deviceMsgQueue == nullptr || this->rf_direct_queue == nullptr)
    {
        ESP_LOGE(TAG, "Memory for task queues was not allocated. Aborting!");
        return;
    }

    // Initialise the primary UART for bidirectional ECP keypad traffic.
    init_uart(this->uart_num, this->rx_pin, this->tx_pin);

    // The primary RX/TX task runs at the highest possible FreeRTOS priority so
    // it can respond to panel poll cycles without being pre-empted.
    xTaskCreate(rx_tx_task_start, "uart_rx_tx_task", kUartRxTxTaskStackSize,
                (void *)this, configMAX_PRIORITIES - 1, &this->rx_tx_task_Handle);

    // The optional monitor UART listens on the expansion bus (TX / green wire)
    // to capture RF receiver and wireless sensor data.  It runs at a lower
    // priority because it is receive-only and not timing-critical.
    if (this->monitor_pin != -1)
    {
        // tx_pin == -1 tells init_uart to install in RX-only mode.
        init_uart(this->ext_uart_num, this->monitor_pin, static_cast<gpio_num_t>(-1));
        xTaskCreate(monitor_rx_task_start, "uart_monitor_rx_task", kUartMonitorTaskStackSize,
                    (void *)this, configMAX_PRIORITIES - 10, &this->monitor_rx_task_Handle);
    }
}

bool VistaBus::stop()
{
    this->stop_requested = true;

    // monitor_rx_task is parked in uart_read_bytes on ext_uart_num.  The ECP
    // bus wires tx_pin (uart_num's output) and monitor_pin (ext_uart_num's
    // input) to the same shared green wire, so a byte written on uart_num is
    // received by ext_uart_num and unblocks the monitor read.
    char tmp[1] = {static_cast<char>(0xFF)};
    while (monitor_rx_task_Handle != nullptr)
    {
        uart_write_bytes(this->uart_num, tmp, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Wait for the primary task to exit its loop and clear its handle.
    while (rx_tx_task_Handle != nullptr)
        vTaskDelay(pdMS_TO_TICKS(20));

    uart_driver_delete(this->uart_num);
    if (this->monitor_pin != -1)
        uart_driver_delete(this->ext_uart_num);

    // Post a sentinel packet (type == -1) so any caller blocked in
    // read_packet(with_delay=true) unblocks and can detect the shutdown.
    ReceivedPacket sentinel{};
    sentinel.type = -1;
    xQueueSend(this->receiveQueue, &sentinel, pdMS_TO_TICKS(100));

    return true;
}


// ---------------------------------------------------------------------------
// Application-facing write helpers
// ---------------------------------------------------------------------------

// Queue a keypad command for the rx_tx_task to transmit.
// type == 1 → the protocol layer will encode the payload before sending.
bool VistaBus::write(const char *data_to_write, int size, int keypadaddress, int sequence)
{
    SendPacket sendpkt;
    if (size > 24)
        size = 24;
    memcpy(sendpkt.payload, data_to_write, static_cast<size_t>(size));
    sendpkt.keypadaddress = keypadaddress;
    sendpkt.type          = 1;
    sendpkt.size          = size;
    sendpkt.sequence      = static_cast<char>(sequence);
    return xQueueSend(sendQueue, &sendpkt, 0) == pdPASS;
}

// Queue a pre-encoded (hex) payload for direct transmission.
// type == 0 → bypasses the protocol encoding step.
bool VistaBus::writedirect(const char *hex_data_to_write, int size, int keypadaddress, int sequence)
{
    SendPacket sendpkt;
    if (size > 24)
        return false;
    memcpy(sendpkt.payload, hex_data_to_write, static_cast<size_t>(size));
    sendpkt.keypadaddress = keypadaddress;
    sendpkt.type          = 0;
    sendpkt.size          = size;
    sendpkt.sequence      = static_cast<char>(sequence);
    return xQueueSend(sendQueue, &sendpkt, 0) == pdPASS;
}


bool VistaBus::connected() const
{
    return this->panel_connected;
}

void VistaBus::emulateLRR(bool enabled)
{
    LRRemulation = enabled;
}


// ---------------------------------------------------------------------------
// DEBUG_PULSE helper — captures raw keybus pulse timings via the RMT peripheral
// ---------------------------------------------------------------------------

#ifdef DEBUG_PULSE
// ISR callback: fires when the RMT receiver has finished capturing a symbol
// sequence.  Posts the event data to receive_queue so the task can log it.
static bool rmt_rx_done_callback(rmt_channel_handle_t channel,
                                  const rmt_rx_done_event_data_t *edata,
                                  void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

// Capture one burst of RMT symbols on rx_pin and log the low/high durations
// in microseconds.  Resolution is 1 MHz (1 µs per tick).  Signal is inverted
// to match the ECP bus idle-high polarity.
void VistaBus::capture_pulse_pattern(gpio_num_t rx_pin)
{
    rmt_channel_handle_t rx_chan = nullptr;
    rmt_rx_channel_config_t rx_chan_config = {
        .gpio_num          = rx_pin,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 1 * 1000 * 1000,
        .mem_block_symbols = 64,
        .intr_priority     = 0,
        .flags = {
            .invert_in    = true,
            .with_dma     = false,
            .io_loop_back = false,
        }
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_chan));

    QueueHandle_t receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    ESP_ERROR_CHECK(rmt_enable(rx_chan));

    rmt_rx_event_callbacks_t cbs = { .on_recv_done = rmt_rx_done_callback };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, receive_queue));

    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 2000,       // ignore glitches < 2 µs
        .signal_range_max_ns = 20000000,   // end-of-packet idle > 20 ms
        .flags = { .en_partial_rx = false }
    };

    rmt_symbol_word_t raw_symbols[64];
    ESP_ERROR_CHECK(rmt_receive(rx_chan, raw_symbols, sizeof(raw_symbols), &receive_config));

    rmt_rx_done_event_data_t rx_data;
    xQueueReceive(receive_queue, &rx_data, portMAX_DELAY);
    ESP_LOGI(TAG, "Received %d symbols", rx_data.num_symbols);
    for (int i = 0; i < rx_data.num_symbols; i++)
    {
        ESP_LOGI(TAG, "Low(us): %05d  High(us): %05d",
                 rx_data.received_symbols[i].duration0,
                 rx_data.received_symbols[i].duration1);
    }

    ESP_ERROR_CHECK(rmt_disable(rx_chan));
    ESP_ERROR_CHECK(rmt_del_channel(rx_chan));
    vQueueDelete(receive_queue);
}
#endif


// ---------------------------------------------------------------------------
// read_packet — application-facing receive
// ---------------------------------------------------------------------------

// Dequeue one decoded panel packet and unpack it into the caller's buffers.
// Returns false if no packet was available (non-blocking) or if the queue
// receive failed (blocking path should not return false unless reset).
bool VistaBus::read_packet(char *data, int &len, int &type, int &src, bool with_delay)
{
    ReceivedPacket pkt;
    const bool result = with_delay
        ? xQueueReceive(this->receiveQueue, &pkt, portMAX_DELAY) == pdPASS
        : xQueueReceive(this->receiveQueue, &pkt, 0)             == pdPASS;

    if (result)
    {
        memcpy(data, pkt.payload, static_cast<size_t>(pkt.size));
        len  = pkt.size;
        type = pkt.type;
        src  = pkt.source;
        return true;
    }
    return false;
}


// ---------------------------------------------------------------------------
// init_uart — hardware UART configuration for ECP-bus operation
// ---------------------------------------------------------------------------

// The Ademco ECP keybus runs at 4800 baud, 8 data bits, even parity, 2 stop
// bits.  Both RX and TX lines are idle-high but the ESP32 UART hardware expects
// idle-low, so the line signals must be inverted in the UART peripheral.
// When tx_pin == -1 the port is RX-only (no event queue, no TX inversion).
void VistaBus::init_uart(uart_port_t u_n, gpio_num_t rx_pin, gpio_num_t tx_pin)
{
    const uart_config_t uart_config =
    {
        .baud_rate           = 4800,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_EVEN,
        .stop_bits           = UART_STOP_BITS_2,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
        .flags               = {}
    };

    const int intr_alloc_flags = 0;

    // RX-only mode: install without a TX buffer or event queue.
    // Full duplex mode: install with an event queue so the task can react to
    // UART_DATA and UART_BREAK events from the ISR.
    if (static_cast<int>(tx_pin) == -1)
        ESP_ERROR_CHECK(uart_driver_install(u_n, kRXBufSize + 8, 0, 0, nullptr, intr_alloc_flags));
    else
        ESP_ERROR_CHECK(uart_driver_install(u_n, kRXBufSize + 8, 0, 25, &uartevtQueue, intr_alloc_flags));

    ESP_ERROR_CHECK(uart_param_config(u_n, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(u_n, tx_pin, rx_pin, -1, -1));

    // Trigger a UART_DATA event after 2 symbol-times of silence — this provides
    // the inter-byte gap detection used to delimit ECP frames.
    ESP_ERROR_CHECK(uart_set_rx_timeout(u_n, 2));

    // Fire the TX-empty interrupt as soon as the FIFO has one byte of headroom,
    // giving the protocol handler the earliest possible notification that the
    // bus is free to transmit again.
    ESP_ERROR_CHECK(uart_set_tx_empty_threshold(u_n, 1));
    ESP_ERROR_CHECK(uart_enable_rx_intr(u_n));

    if (static_cast<int>(tx_pin) == -1)
    {
        // Monitor-only port: single-byte RX threshold and RX inversion only.
        ESP_ERROR_CHECK(uart_set_rx_full_threshold(u_n, 1));
        ESP_ERROR_CHECK(uart_set_line_inverse(u_n, UART_SIGNAL_RXD_INV));
    }
    else
    {
        // Full-duplex port: invert both RX and TX to match ECP idle-high polarity.
        ESP_ERROR_CHECK(uart_set_rx_full_threshold(u_n, 1));
        ESP_ERROR_CHECK(uart_set_line_inverse(u_n, UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV));
        ESP_ERROR_CHECK(uart_enable_tx_intr(u_n, 1, 0));
    }
}


// ---------------------------------------------------------------------------
// rx_tx_task — primary FreeRTOS task
// ---------------------------------------------------------------------------

// This is the heart of the bus driver.  It runs at the highest FreeRTOS
// priority and handles the ECP panel's rigid polling schedule:
//
//  1. Device messages: if the deviceMsgQueue holds a pending expander or RF
//     notification and no transmission is already in progress, a type-2
//     "pulse" (F1 poll) is sent to the target device to open a reply window.
//     Messages older than 5 s are discarded to avoid stale state.
//
//  2. Stop detection: once the monitor task has also exited the loop can
//     safely break and the task deletes itself.
//
//  3. Connection watchdog: if no data is received for 30 s the panel is
//     marked disconnected.
//
//  4. Protocol dispatch: check_send_Q arms the next outgoing packet if one
//     is queued, then handle_UART_events blocks briefly on the UART event
//     queue and returns any byte received during that window.  The received
//     byte is used to route to the appropriate dispatchFx() handler.
void VistaBus::rx_tx_task(void *args)
{
    // Configure the RX GPIO for any-edge interrupts.  The protocol layer uses
    // this to detect the start-of-frame pulse that precedes each panel packet.
    gpio_config_t io_conf =
    {
        .pin_bit_mask = static_cast<uint64_t>(1ULL << this->rx_pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };

    gpio_config(&io_conf);
    (void)gpio_install_isr_service(0); // no-op if already installed

    SendPacket pkt_to_send;
    while (1)
    {
        // --- Device message / F1 poll handling ---
        // If there is a pending device message (expander fault or RF sensor
        // event) and the bus is idle, send an F1 poll to the target device.
        // Rate-limit to one poll per kPulseCyclePeriod ms to avoid bus
        // collisions with back-to-back polls.
        if (uxQueueMessagesWaiting(deviceMsgQueue)
                && !vprotocol->req_to_send
                && (esp_timer_get_time() - request_F1_time > kPulseCyclePeriod * 1000))
        {
            DeviceMsg q_msg;
            xQueuePeek(deviceMsgQueue, &q_msg, pdMS_TO_TICKS(0));
            if (esp_timer_get_time() - q_msg.time < 5 * 1000 * 1000)
            {
                requestF1(q_msg.address);
                request_F1_time = esp_timer_get_time();
            }
            else
            {
                // Message has been waiting > 5 s with no F1 window — drop it.
                xQueueReceive(deviceMsgQueue, &q_msg, pdMS_TO_TICKS(0));
                ESP_LOGE(TAG, "Dropping msg for device at address %d — no F1 response in 5 s.",
                         q_msg.address);
            }
        }

        // --- Stop check ---
        // Only exit after the monitor task has already shut down; this ensures
        // the monitor task can still unblock via the sentinel byte written by stop().
        if (this->stop_requested && monitor_rx_task_Handle == nullptr)
        {
            this->panel_connected = false;
            break;
        }

        // --- Connection watchdog ---
        // The panel sends data every ~250 ms.  If 30 s elapse with nothing
        // received, assume the panel is powered off or disconnected.
        const int64_t now = esp_timer_get_time();
        if (now - last_data_received > 30LL * 1000 * 1000)
            this->panel_connected = false;

#ifdef DEBUG_PULSE
        // Capture a single burst of RMT pulse data after 60 s of operation
        // to allow the bus to stabilise before sampling.
        if (now > 60LL * 1000 * 1000)
        {
            ESP_LOGE(TAG, "Collecting pulse pattern at %lld", now);
            capture_pulse_pattern(this->rx_pin);
            vTaskDelay(10);
            continue;
        }
#endif

        // --- Protocol handling ---
        // check_send_Q: if a SendPacket is ready and the bus timing allows,
        // move it into pkt_to_send so handle_UART_events can transmit it.
        vprotocol->check_send_Q(pkt_to_send);

        // handle_UART_events: processes one UART event (data / break / timeout)
        // and returns the number of bytes received (always 0 or 1 for ECP).
        // Also manages the TX state machine, sending pkt_to_send when the bus
        // grants a transmission window.
        uint8_t buf[1];
        const int rxBytes = vprotocol->handle_UART_events(pkt_to_send, buf);

        if (rxBytes)
        {
            this->panel_connected = true;
            last_data_received = esp_timer_get_time();

            // A type-2 send is a pulse-only marker (no data bytes).  Once the
            // bus has been marked, consider the send complete immediately —
            // there is no ACK to wait for.
            if (vprotocol->req_to_send && vprotocol->pulse_marked && pkt_to_send.type == 2)
                vprotocol->req_to_send = false;

            // Route the received command byte to the appropriate handler.
            // Each 0xFx value corresponds to a specific panel message type
            // defined by the Ademco ECP protocol.
            if (buf[0] == 0xF2)
                vprotocol->dispatchF2();           // keypad poll
            else if (buf[0] == 0xF6)
                vprotocol->dispatchF6(pkt_to_send); // status / command packet
            else if (buf[0] == 0xF7)
                vprotocol->dispatchF7();            // long-range radio message
            else if (buf[0] == 0xF8)
                vprotocol->dispatchF8();            // zone-expander poll
            else if (buf[0] == 0xF9)
                vprotocol->dispatchF9();            // RF receiver poll
            else if (buf[0] == 0xFA && vprotocol->is_2400)
                vprotocol->dispatchFA();            // 2400-series extended status
            else if (buf[0] == 0xFB && vprotocol->is_2400)
                vprotocol->dispatchFB();            // 2400-series extended data
            else if (vprotocol->legacy_protocol && vprotocol->is_2400)
                vprotocol->dispatch_legacyStatusPacket(buf[0]); // SE-series status
            else if (buf[0] == 0)
            {
                // Single zero byte — discard silently.
            }
            else
            {
#ifdef DEBUG_LOG
                vprotocol->dispatchDebug(buf[0], 0);
#endif
            }
        }

        // Track consecutive failed send attempts and log / back-off as needed.
        vprotocol->track_write_attempts();
    }

    ESP_LOGI(TAG, "Stopping rx_tx_task");
    this->rx_tx_task_Handle = nullptr;
    vTaskDelete(nullptr); // FreeRTOS requires a task to delete itself this way
}


// ---------------------------------------------------------------------------
// monitor_rx_task — secondary (expansion-bus) receive task
// ---------------------------------------------------------------------------

// This task reads the TX (green) wire of the keybus via a separate UART
// configured in RX-only mode.  Expansion modules and RF receivers transmit
// their responses on this wire.  Data arrives as 1–3 byte sequences; the
// first byte(s) identify the message type and the final byte carries the
// payload.  The accumulated value 'val' shifts in received bytes and is
// compared against known header patterns to route each message.
void VistaBus::monitor_rx_task(void *args)
{
    uint8_t  buf[1];
    uint32_t val = 0; // shift register accumulating up to 3 header bytes

    while (1)
    {
        if (this->stop_requested)
            break;

        // monitor_task_sync blocks until a byte is available on the extension
        // UART, then returns it in buf[0] and accumulates it into val.
        const int rxBytes = vprotocol->monitor_task_sync(buf, val);
        if (rxBytes)
        {
            // Identify message type from the upper bytes of the shift register.
            // Once a known header is matched, dispatch and reset val so the
            // next message starts fresh.
            if (static_cast<uint8_t>(val >> 8) == 0xF6)
            {
                vprotocol->dispatch_extF6(val, buf[0]); // expander status
                val = 0;
            }
            else if (static_cast<uint8_t>(val >> 8) == 0xF8)
            {
                vprotocol->dispatch_extF8(val, buf[0]); // zone-expander response
                val = 0;
            }
            else if (static_cast<uint8_t>(val >> 16) == 0xF9)
            {
                vprotocol->dispatch_extF9(val, buf[0]); // RF receiver response
                val = 0;
            }
            else if (static_cast<uint8_t>(val >> 16) == 0xFA)
            {
                vprotocol->dispatch_extFA(val, buf[0], vprotocol->legacy_protocol);
                val = 0;
            }
            else if (static_cast<uint8_t>(val >> 16) == 0xFB)
            {
                vprotocol->dispatch_extFB(val, buf[0]);
                val = 0;
            }
            else
            {
#ifdef DEBUG_LOG
                vprotocol->dispatchDebug(buf[0], 1);
#endif
            }
        }
    }

    ESP_LOGI(TAG, "Stopping monitor_rx_task");
    this->monitor_rx_task_Handle = nullptr;
    vTaskDelete(nullptr);
}


// ---------------------------------------------------------------------------
// Zone-expander helpers
// ---------------------------------------------------------------------------

// Map a zone number to the keybus address of the expander that owns it.
// Expanders occupy consecutive 8-zone blocks starting at keybus address 7.
// Returns 0 if the zone is not in a supported expander range.
static uint8_t getExpanderAddress(uint8_t zone)
{
    // Expander address → zone range mapping:
    //   address 7  →  zones  9–16
    //   address 8  →  zones 17–24
    //   address 9  →  zones 25–32
    //   address 10 →  zones 33–40
    //   address 11 →  zones 41–48
    if (zone >  8 && zone < 17) return 7;
    if (zone > 16 && zone < 25) return 8;
    if (zone > 24 && zone < 33) return 9;
    if (zone > 32 && zone < 41) return 10;
    if (zone > 40 && zone < 49) return 11;
    return 0;
}

// Linear search through the emulated_expanders vector for the given address.
// Returns a raw pointer into the vector element (safe as long as the vector
// is not reallocated while the pointer is in use).
VistaBus::EmulatedExpander *VistaBus::getExpander(uint8_t address)
{
    for (auto &it : emulated_expanders)
    {
        if (it.address == address)
            return &it;
    }
    return nullptr;
}

// Build and queue an F1 poll packet (type == 2, payload size == 0) for the
// given keybus address.  The rx_tx_task will send the pulse on the next
// available bus window; the target device responds with its state.
void VistaBus::requestF1(uint8_t address)
{
    SendPacket pkt;
    pkt.type           = 2;
    pkt.keypadaddress  = address;
    pkt.payload[0]     = 0;
    pkt.size           = 0;
    xQueueSend(sendQueue, &pkt, 0);
}

// Update the fault (normally-open) bit for a zone on its expander and
// enqueue a DeviceMsg so the rx_tx_task will request an F1 poll slot.
void VistaBus::setExpFaultBits(uint8_t zone, bool fault)
{
    uint8_t address = 0;
    if (!vprotocol->legacy_protocol)
        address = getExpanderAddress(zone);
    else
        address = 1; // legacy SE protocol uses a fixed expander address

    EmulatedExpander *expander = getExpander(address);
    if (expander != nullptr)
    {
        const uint8_t z    = zone & 0x07;         // zone position within the 8-zone block
        const uint8_t bit  = static_cast<uint8_t>(1u << (8u - z)); // bit mask for this zone

        // Set or clear the fault bit while leaving all other zone bits unchanged.
        expander->fault_NO_Bits = static_cast<char>(
            fault ? (static_cast<uint8_t>(expander->fault_NO_Bits) |  bit)
                  : (static_cast<uint8_t>(expander->fault_NO_Bits) & ~bit));

        // Notify the rx_tx_task that this expander needs an F1 poll.
        DeviceMsg expMsg;
        expMsg.address = address;
        expMsg.source  = zone;
        expMsg.msg     = static_cast<uint8_t>(fault);
        expMsg.time    = esp_timer_get_time();
        xQueueSend(deviceMsgQueue, &expMsg, 0);
    }
}

// Forward an RF sensor event to the panel.  Packages the sensor serial number
// and message byte into a DeviceMsg addressed to the emulated RF receiver so
// the rx_tx_task can deliver it on the next F1 poll cycle.
void VistaBus::sendRFmsg(uint32_t serial, uint8_t msg)
{
    DeviceMsg rfMsg;
    rfMsg.address = emulated_rf_receiver.address;
    rfMsg.source  = serial;
    rfMsg.msg     = msg;
    rfMsg.time    = esp_timer_get_time();
    xQueueSend(deviceMsgQueue, &rfMsg, 0);
}

// Register a zone with an emulated expander.  If no expander for that zone's
// address exists yet, a new EmulatedExpander is created and added to the
// vector.  The NC (tamper) bit for this zone is cleared to "normal" in either
// case, marking the zone as enrolled.
void VistaBus::add_emulated_expander(uint8_t zone)
{
    uint8_t address = 0;
    if (!vprotocol->legacy_protocol)
        address = getExpanderAddress(zone);
    else
        address = 1;

    const uint8_t z   = zone & 0x07;
    const uint8_t bit = static_cast<uint8_t>(1u << (8u - z));

    EmulatedExpander *expander = getExpander(address);
    if (expander == nullptr)
    {
        // First zone for this expander address — create the expander entry.
        EmulatedExpander new_expander;
        new_expander.address       = address;
        new_expander.fault_NC_Bits = static_cast<char>(
            static_cast<uint8_t>(new_expander.fault_NC_Bits) & ~bit);
        this->emulated_expanders.push_back(new_expander);
        ESP_LOGI(TAG, "Adding emulated expander address:%d for zone:%d", address, zone);
        EXPemulation = true;
    }
    else
    {
        // Expander already exists — just enroll this additional zone.
        expander->fault_NC_Bits = static_cast<char>(
            static_cast<uint8_t>(expander->fault_NC_Bits) & ~bit);
        ESP_LOGI(TAG, "Existing emulated expander address:%d handling zone:%d", address, zone);
    }
}

// Enable RF receiver emulation at the specified keybus address.
void VistaBus::emulateRFR(uint8_t address)
{
    RFRemulation = true;
    emulated_rf_receiver.address = address;
}


// ---------------------------------------------------------------------------
// Static trampolines — required by xTaskCreate which takes a plain function pointer
// ---------------------------------------------------------------------------

void VistaBus::rx_tx_task_start(void *args)
{
    VistaBus *tsk = static_cast<VistaBus *>(args);
    tsk->rx_tx_task(args);
}

void VistaBus::monitor_rx_task_start(void *args)
{
    VistaBus *tsk = static_cast<VistaBus *>(args);
    tsk->monitor_rx_task(args);
}
