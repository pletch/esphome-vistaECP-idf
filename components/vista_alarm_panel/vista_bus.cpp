#include "vista_bus.h"

#ifndef LEGACY_SE_PROTOCOL
#include "ecp_prot_vista20P.h"
#else
#include "ecp_prot_vistaSE.h"
#endif

#ifdef DEBUG_PULSE
#include "driver/rmt_rx.h"
#endif


VistaBus::VistaBus()
{
#ifndef LEGACY_SE_PROTOCOL
    vprotocol = new Vista20P(*this);
#else
    vprotocol = new VistaSE(*this);
#endif
    this->receiveQueue   = xQueueCreate(15, sizeof(ReceivedPacket));
    this->sendQueue      = xQueueCreate(8,  sizeof(SendPacket));
    this->deviceMsgQueue = xQueueCreate(4,  sizeof(DeviceMsg));
    this->panel_connected  = false;
    this->stop_requested   = false;
    this->LRRemulation     = false;
    this->EXPemulation     = false;
    this->RFRemulation     = false;
}

VistaBus::~VistaBus()
{
    if (this->rx_tx_task_Handle != nullptr)
        stop();

    vQueueDelete(this->receiveQueue);
    vQueueDelete(this->sendQueue);

    // Fix: deviceMsgQueue was not deleted in the original destructor.
    vQueueDelete(this->deviceMsgQueue);

    // Fix: vprotocol was new-allocated in the constructor but never deleted.
    delete vprotocol;
    vprotocol = nullptr;
}

void VistaBus::begin(int uartnum, int rxpin, int txpin, int extuartnum, int monitorpin)
{
    this->uart_num      = static_cast<uart_port_t>(uartnum);
    this->rx_pin        = static_cast<gpio_num_t>(rxpin);
    this->tx_pin        = static_cast<gpio_num_t>(txpin);
    this->ext_uart_num  = static_cast<uart_port_t>(extuartnum);
    this->monitor_pin   = static_cast<gpio_num_t>(monitorpin);

    if (this->receiveQueue == nullptr || this->sendQueue == nullptr
            || this->deviceMsgQueue == nullptr)
    {
        ESP_LOGE(TAG, "Memory for task queues was not allocated. Aborting!");
        return;
    }

    init_uart(this->uart_num, this->rx_pin, this->tx_pin);

    xTaskCreate(rx_tx_task_start, "uart_rx_tx_task", kUartRxTxTaskStackSize,
                (void *)this, configMAX_PRIORITIES - 1, &this->rx_tx_task_Handle);

    if (this->monitor_pin != -1)
    {
        init_uart(this->ext_uart_num, this->monitor_pin, static_cast<gpio_num_t>(-1));
        xTaskCreate(monitor_rx_task_start, "uart_monitor_rx_task", kUartMonitorTaskStackSize,
                    (void *)this, configMAX_PRIORITIES - 10, &this->monitor_rx_task_Handle);
    }
}

bool VistaBus::stop()
{
    this->stop_requested = true;

    // monitor_rx_task must shut down first — it may be parked in uart_read_bytes.
    // Write 0xFF to the UART to unblock it.
    char tmp[1] = {static_cast<char>(0xFF)};
    while (monitor_rx_task_Handle != nullptr)
    {
        uart_write_bytes(this->uart_num, tmp, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    while (rx_tx_task_Handle != nullptr)
        vTaskDelay(pdMS_TO_TICKS(20));

    uart_driver_delete(this->uart_num);
    if (this->monitor_pin != -1)
        uart_driver_delete(this->ext_uart_num);

    return true;
}

bool VistaBus::write(const char *data_to_write, int size, int keypadaddress, int sequence)
{
    SendPacket sendpkt;
    // Use memcpy rather than strncpy: the payload is not null-terminated and
    // strncpy would stop early if any byte happened to be 0x00.
    if (size > 24)
        size = 24;
    memcpy(sendpkt.payload, data_to_write, static_cast<size_t>(size));
    sendpkt.keypadaddress = keypadaddress;
    sendpkt.type          = 1;
    sendpkt.size          = size;
    sendpkt.sequence      = static_cast<char>(sequence);
    return xQueueSend(sendQueue, &sendpkt, 0) == pdPASS;
}

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

#ifdef DEBUG_PULSE
static bool rmt_rx_done_callback(rmt_channel_handle_t channel,
                                  const rmt_rx_done_event_data_t *edata,
                                  void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

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
        .signal_range_min_ns = 2000,
        .signal_range_max_ns = 20000000,
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

    if (static_cast<int>(tx_pin) == -1)
        ESP_ERROR_CHECK(uart_driver_install(u_n, kRXBufSize + 8, 0, 0, nullptr, intr_alloc_flags));
    else
        ESP_ERROR_CHECK(uart_driver_install(u_n, kRXBufSize + 8, 0, 25, &uartevtQueue, intr_alloc_flags));

    ESP_ERROR_CHECK(uart_param_config(u_n, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(u_n, tx_pin, rx_pin, -1, -1));
    ESP_ERROR_CHECK(uart_set_rx_timeout(u_n, 2));
    ESP_ERROR_CHECK(uart_set_tx_empty_threshold(u_n, 1));
    ESP_ERROR_CHECK(uart_enable_rx_intr(u_n));

    if (static_cast<int>(tx_pin) == -1)
    {
        ESP_ERROR_CHECK(uart_set_rx_full_threshold(u_n, 1));
        ESP_ERROR_CHECK(uart_set_line_inverse(u_n, UART_SIGNAL_RXD_INV));
    }
    else
    {
        ESP_ERROR_CHECK(uart_set_rx_full_threshold(u_n, 1));
        ESP_ERROR_CHECK(uart_set_line_inverse(u_n, UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV));
        ESP_ERROR_CHECK(uart_enable_tx_intr(u_n, 1, 0));
    }
}

void VistaBus::rx_tx_task(void *args)
{
    gpio_config_t io_conf =
    {
        .pin_bit_mask = static_cast<uint64_t>(1ULL << this->rx_pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };

    gpio_config(&io_conf);
    (void)gpio_install_isr_service(0);

    SendPacket pkt_to_send;
    while (1)
    {
        // Handle any queued device messages by sending an F1 poll request.
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
                xQueueReceive(deviceMsgQueue, &q_msg, pdMS_TO_TICKS(0));
                ESP_LOGE(TAG, "Dropping msg for device at address %d — no F1 response in 5 s.",
                         q_msg.address);
            }
        }

        if (this->stop_requested && monitor_rx_task_Handle == nullptr)
        {
            this->panel_connected = false;
            break;
        }

        const int64_t now = esp_timer_get_time();
        if (now - last_data_received > 30LL * 1000 * 1000)
            this->panel_connected = false;

#ifdef DEBUG_PULSE
        if (now > 60LL * 1000 * 1000)
        {
            ESP_LOGE(TAG, "Collecting pulse pattern at %lld", now);
            capture_pulse_pattern(this->rx_pin);
            vTaskDelay(10);
            continue;
        }
#endif

        vprotocol->check_send_Q(pkt_to_send);
        uint8_t buf[1];
        const int rxBytes = vprotocol->handle_UART_events(pkt_to_send, buf);

        if (rxBytes)
        {
            this->panel_connected = true;
            last_data_received = esp_timer_get_time();

            // No ACK expected for type-2 (pulse-mark only) sends.
            if (vprotocol->req_to_send && vprotocol->pulse_marked && pkt_to_send.type == 2)
                vprotocol->req_to_send = false;

            if (buf[0] == 0xF2)
                vprotocol->dispatchF2();
            else if (buf[0] == 0xF6)
                vprotocol->dispatchF6(pkt_to_send);
            else if (buf[0] == 0xF7)
                vprotocol->dispatchF7();
            else if (buf[0] == 0xF8)
                vprotocol->dispatchF8();
            else if (buf[0] == 0xF9)
                vprotocol->dispatchF9();
            else if (buf[0] == 0xFA && vprotocol->is_2400)
                vprotocol->dispatchFA();
            else if (buf[0] == 0xFB && vprotocol->is_2400)
                vprotocol->dispatchFB();
            else if (vprotocol->legacy_protocol && vprotocol->is_2400)
                vprotocol->dispatch_legacyStatusPacket(buf[0]);
            // Fix: original had `buf == 0` which compared a pointer to zero —
            // always false, making this a dead branch.  Corrected to buf[0] == 0.
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

        vprotocol->track_write_attempts();
    }

    ESP_LOGI(TAG, "Stopping rx_tx_task");
    this->rx_tx_task_Handle = nullptr;
    vTaskDelete(nullptr);
}

void VistaBus::monitor_rx_task(void *args)
{
    uint8_t  buf[1];
    uint32_t val = 0;

    while (1)
    {
        if (this->stop_requested)
            break;

        const int rxBytes = vprotocol->monitor_task_sync(buf, val);
        if (rxBytes)
        {
            if (static_cast<uint8_t>(val >> 8) == 0xF6)
            {
                vprotocol->dispatch_extF6(val, buf[0]);
                val = 0;
            }
            else if (static_cast<uint8_t>(val >> 8) == 0xF8)
            {
                vprotocol->dispatch_extF8(val, buf[0]);
                val = 0;
            }
            else if (static_cast<uint8_t>(val >> 16) == 0xF9)
            {
                vprotocol->dispatch_extF9(val, buf[0]);
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

VistaBus::EmulatedExpander *VistaBus::getExpander(uint8_t address)
{
    for (auto &it : emulated_expanders)
    {
        if (it.address == address)
            return &it;
    }
    return nullptr;
}

void VistaBus::requestF1(uint8_t address)
{
    SendPacket pkt;
    pkt.type           = 2;
    pkt.keypadaddress  = address;
    pkt.payload[0]     = 0;
    pkt.size           = 0;
    xQueueSend(sendQueue, &pkt, 0);
}

void VistaBus::setExpFaultBits(uint8_t zone, bool fault)
{
    uint8_t address = 0;
    if (!vprotocol->legacy_protocol)
        address = getExpanderAddress(zone);
    else
        address = 1;

    EmulatedExpander *expander = getExpander(address);
    if (expander != nullptr)
    {
        // Fix: original used `0x01 << (8-z)` with a signed int, which produces
        // undefined behaviour when z==0 (shift of 8 into a char).  Using an
        // unsigned shift and casting to char avoids the UB.
        const uint8_t z    = zone & 0x07;
        const uint8_t bit  = static_cast<uint8_t>(1u << (8u - z));

        expander->fault_NO_Bits = static_cast<char>(
            fault ? (static_cast<uint8_t>(expander->fault_NO_Bits) |  bit)
                  : (static_cast<uint8_t>(expander->fault_NO_Bits) & ~bit));

        DeviceMsg expMsg;
        expMsg.address = address;
        expMsg.source  = zone;
        expMsg.msg     = static_cast<uint8_t>(fault);
        expMsg.time    = esp_timer_get_time();
        xQueueSend(deviceMsgQueue, &expMsg, 0);
    }
}

void VistaBus::setExpTamper(uint8_t zone, bool tamper_active)
{
    const uint8_t address  = getExpanderAddress(zone);
    EmulatedExpander *expander = getExpander(address);
    if (expander != nullptr)
    {
        // Fix: same unsigned-shift correction as setExpFaultBits.
        const uint8_t z   = zone & 0x07;
        const uint8_t bit = static_cast<uint8_t>(1u << (8u - z));

        expander->fault_NC_Bits = static_cast<char>(
            tamper_active ? (static_cast<uint8_t>(expander->fault_NC_Bits) |  bit)
                          : (static_cast<uint8_t>(expander->fault_NC_Bits) & ~bit));

        expander->fault_NO_Bits = static_cast<char>(
            tamper_active ? (static_cast<uint8_t>(expander->fault_NO_Bits) |  bit)
                          : (static_cast<uint8_t>(expander->fault_NO_Bits) & ~bit));
    }
}

void VistaBus::sendRFmsg(uint32_t serial, uint8_t msg)
{
    DeviceMsg rfMsg;
    rfMsg.address = emulated_rf_receiver.address;
    rfMsg.source  = serial;
    rfMsg.msg     = msg;
    rfMsg.time    = esp_timer_get_time();
    xQueueSend(deviceMsgQueue, &rfMsg, 0);
}

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
        expander->fault_NC_Bits = static_cast<char>(
            static_cast<uint8_t>(expander->fault_NC_Bits) & ~bit);
        ESP_LOGI(TAG, "Existing emulated expander address:%d handling zone:%d", address, zone);
    }
}

void VistaBus::emulateRFR(uint8_t address)
{
    RFRemulation = true;
    emulated_rf_receiver.address = address;
}

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
