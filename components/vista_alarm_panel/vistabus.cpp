#include "vistabus.h"

#ifndef LEGACY_SE_PROTOCOL
#include "vista20P.h"
#else
#include "vistaSE.h"
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
    this->receiveQueue = xQueueCreate(15,sizeof(ReceivedPacket)); 
    this->sendQueue = xQueueCreate(8, sizeof(SendPacket));
    this->deviceMsgQueue = xQueueCreate(4, sizeof(DeviceMsg));
    this->panel_connected = false;
    this->stop_requested = false;
    this->LRRemulation = false;
    this->EXPemulation = false;
    this->RFRemulation = false;
}

VistaBus::~VistaBus()
{
    if (this->rx_tx_task_Handle != nullptr) 
        stop();
    vQueueDelete(this->receiveQueue);
    vQueueDelete(this->sendQueue);
}

void VistaBus::begin(int uartnum, int rxpin, int txpin, int extuartnum = -1, int monitorpin = -1) 
{
    this->uart_num = static_cast<uart_port_t>(uartnum);
    this->rx_pin = static_cast<gpio_num_t>(rxpin);
    this->tx_pin = static_cast<gpio_num_t>(txpin);
    this->ext_uart_num = static_cast<uart_port_t>(extuartnum);
    this->monitor_pin = static_cast<gpio_num_t>(monitorpin);

    if (this->receiveQueue == nullptr || this->sendQueue == nullptr || this->deviceMsgQueue == nullptr)
    {
        ESP_LOGE(TAG, "Memory for task queues was not allocated. Aborting!");
        return;
    }

    init_uart(this->uart_num, this->rx_pin, this->tx_pin);

    xTaskCreate(rx_tx_task_start, "uart_rx_tx_task", kUartRxTxTaskStackSize, (void *) this, configMAX_PRIORITIES-1, &this->rx_tx_task_Handle);
    if (this->monitor_pin != -1)
    {
        init_uart(this->ext_uart_num,this->monitor_pin, static_cast<gpio_num_t>(-1));
        xTaskCreate(monitor_rx_task_start, "uart_monitor_rx_task", kUartMonitorTaskStackSize, (void *) this, configMAX_PRIORITIES-10, &this->monitor_rx_task_Handle);
    }
}

bool VistaBus::stop() 
{
    this->stop_requested = true;

    //monitor_rx_task must be shutdown first and is potentially parked at uartreadbytes.
    //send an FF byte to wake so it shuts down. Must shut down gracefully to delete data.
    char tmp[1];
    tmp[0] = 0xFF;
    while (monitor_rx_task_Handle != nullptr) //wait for task to terminate
    {
        uart_write_bytes(this->uart_num,tmp,1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    while(rx_tx_task_Handle != nullptr) //wait for task to terminate
    {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    uart_driver_delete(this->uart_num);
    if(this->monitor_pin != -1) {
        uart_driver_delete(this->ext_uart_num);
    }
    return true;
}

bool VistaBus::write(const char * data_to_write, int size, int keypadaddress, int sequence)
{
    SendPacket sendpkt;
    strncpy(sendpkt.payload,data_to_write,24);
    sendpkt.keypadaddress = keypadaddress;
    sendpkt.type = 1;
    sendpkt.size = size;
    sendpkt.sequence = sequence;
    bool result = false;
    result = xQueueSend(sendQueue,&sendpkt,0) == pdPASS;
    return result;
}

bool VistaBus::writedirect(const char * hex_data_to_write, int size, int keypadaddress, int sequence)
{
    SendPacket sendpkt;
    if (size > 24)
        return false;
    memcpy(sendpkt.payload,hex_data_to_write,size);
    sendpkt.keypadaddress = keypadaddress;
    sendpkt.type = 0;
    sendpkt.size = size;
    sendpkt.sequence = sequence;
    bool result = xQueueSend(sendQueue,&sendpkt,0) == pdPASS;
    return result;
}

bool VistaBus::connected() 
{
    return this->panel_connected;
}

void VistaBus::emulateLRR(bool enabled) 
{
    LRRemulation = enabled;
}

#ifdef DEBUG_PULSE
static bool rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    // send the received RMT symbols to the parser task
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    // return whether any task is woken up
    return high_task_wakeup == pdTRUE;
}

void VistaBus::capture_pulse_pattern(gpio_num_t rx_pin)
{
    rmt_channel_handle_t rx_chan = nullptr;
    rmt_rx_channel_config_t rx_chan_config = {
        .gpio_num = rx_pin,                    // GPIO number
        .clk_src = RMT_CLK_SRC_DEFAULT,   // select source clock
        .resolution_hz = 1 * 1000 * 1000, // 1 MHz tick resolution, i.e., 1 tick = 1 µs
        .mem_block_symbols = 64,          // memory block size, 64 * 4 = 256 Bytes  C6 cannot use more than 64 symbols
        .intr_priority = 0,
        .flags = {
            .invert_in = true,         // invert input signal
            .with_dma = false,          // do not need DMA backend
            .io_loop_back = false,
        }
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_chan));

    QueueHandle_t receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));

    ESP_ERROR_CHECK(rmt_enable(rx_chan));
    
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, receive_queue));
    
    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 2000,     // 2 us.
        .signal_range_max_ns = 20000000, // use 20 ms as longest duration
        .flags = {
            .en_partial_rx = false
        }
    };
    
    rmt_symbol_word_t raw_symbols[64];
    // ready to receive
    ESP_ERROR_CHECK(rmt_receive(rx_chan, raw_symbols, sizeof(raw_symbols), &receive_config));
    // wait for the RX-done signal
    rmt_rx_done_event_data_t rx_data;
    xQueueReceive(receive_queue, &rx_data, portMAX_DELAY);
    ESP_LOGI(TAG, "Received %d symbols", rx_data.num_symbols);
    // output the received symbols
    for (int i = 0; i < rx_data.num_symbols; i++)
    {
        uint16_t lowus = rx_data.received_symbols[i].duration0;
        uint16_t highus = rx_data.received_symbols[i].duration1;
        ESP_LOGI(TAG, "Low Duration(us): %05d  High Duration(us) %05d", lowus, highus);
    }
    ESP_ERROR_CHECK(rmt_disable(rx_chan));
    ESP_ERROR_CHECK(rmt_del_channel(rx_chan));
    vQueueDelete(receive_queue);
}
#endif

bool VistaBus::read_packet(char * data, int &len, int &type, int &src, bool with_delay)
{
    ReceivedPacket pkt;
    bool result = false;
    if (with_delay)
        result = xQueueReceive(this->receiveQueue,&pkt,portMAX_DELAY) == pdPASS;
    else
        result = xQueueReceive(this->receiveQueue,&pkt,0) == pdPASS;
    if (result) 
    {
        memcpy(data,pkt.payload,pkt.size);
        len = pkt.size;
        type = pkt.type;
        src = pkt.source;
        return true;
    }
    else
        return false;
}

void VistaBus::init_uart(uart_port_t u_n, gpio_num_t rx_pin, gpio_num_t tx_pin) 
{
    const uart_config_t uart_config = 
    {
        .baud_rate = 4800,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {} 
    };

    int intr_alloc_flags = 0;

    if (static_cast<int>(tx_pin) == -1)
    {      
        ESP_ERROR_CHECK(uart_driver_install(u_n, kRXBufSize + 8, 0, 0, nullptr, intr_alloc_flags));
    }
    else
    {
        ESP_ERROR_CHECK(uart_driver_install(u_n, kRXBufSize + 8, 0, 25, &uartevtQueue, intr_alloc_flags));
    }

    ESP_ERROR_CHECK(uart_param_config(u_n, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(u_n, tx_pin, rx_pin, -1, -1));
    ESP_ERROR_CHECK(uart_set_rx_timeout(u_n, 2));
    ESP_ERROR_CHECK(uart_set_tx_empty_threshold(u_n,1));
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
        ESP_ERROR_CHECK(uart_enable_tx_intr(u_n,1,0));
    }
    
}

void VistaBus::rx_tx_task(void * args)
{
    auto data = std::make_unique<uint8_t[]>(kRXBufSize+1);
    ReceivedPacket received_packet;
    received_packet.type = 0;

    gpio_config_t io_conf = 
    {
        .pin_bit_mask = static_cast<uint64_t>(1 << this->rx_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,       
    };

    gpio_config(&io_conf);
    (void)gpio_install_isr_service(0);
    SendPacket pkt_to_send;
    while (1) 
    {
        // Handle any queued device msgs via F1 request  
        if (uxQueueMessagesWaiting(deviceMsgQueue) && !vprotocol->req_to_send 
                && (esp_timer_get_time() - request_F1_time > kPulseCyclePeriod*1000))
        {
            DeviceMsg q_msg;
            xQueuePeek(deviceMsgQueue,&q_msg,pdMS_TO_TICKS(0));
            if(esp_timer_get_time() - q_msg.time < 5*1000*1000)
            {
                requestF1(q_msg.address);
                request_F1_time = esp_timer_get_time();
            }
            else
            {
                xQueueReceive(deviceMsgQueue,&q_msg,pdMS_TO_TICKS(0));
                ESP_LOGE(TAG,"Dropping msg for device at address %d.  No F1 response in 5 seconds.",q_msg.address);
            }  
        }
        if(this->stop_requested && monitor_rx_task_Handle == nullptr)
        {
            this->panel_connected = false;
            break;
        }
        int64_t now = esp_timer_get_time();
        if (now - last_data_received > 30*1000*1000)
            this->panel_connected = false;

#ifdef DEBUG_PULSE
        if (now > 60*1000*1000)
        {
            ESP_LOGE(TAG,"Collecting pulse pattern at %lld", now);
            capture_pulse_pattern(this->rx_pin);
            vTaskDelay(10);
            continue;
        }
#endif

        vprotocol->check_send_Q(pkt_to_send); 
        int rxBytes = vprotocol->handle_UART_events(pkt_to_send, data.get());

        if (rxBytes) 
        {
            this->panel_connected = true;
            last_data_received = esp_timer_get_time();
            data[rxBytes] = 0;
            memset(received_packet.payload,'\0',sizeof(received_packet.payload));
            received_packet.payload[0] = data[0];
            received_packet.source = 0;
            if (vprotocol->req_to_send && vprotocol->pulse_marked && pkt_to_send.type == 2)  //No ACK for this type of send
            {
                vprotocol->req_to_send = false;
            }
            if ( data[0] == 0xF2 ) //AUI
            {
                rxBytes = vprotocol->uart_read_bytes_event(this->uart_num, data.get(), 1, pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                received_packet.payload[1] = data[0];
                rxBytes = vprotocol->get_packet_event(&received_packet,data.get(),2,static_cast<int> (received_packet.payload[1]),this->uart_num,pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                if (valid_chksum(received_packet.payload,0,rxBytes+2)) 
                    received_packet.source = 0xF2;
                else
                    received_packet.source = 0xCF;
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
            }
            else if ( data[0] == 0xF6) //SEND ACK Received
            {                 
                rxBytes = vprotocol->uart_read_bytes_event(this->uart_num, data.get(), 1, pdMS_TO_TICKS(kUartDelay), uartevtQueue); //Get Address
                if(data[0] != 0 && monitor_rx_task_Handle != nullptr)
                {
                    uint32_t val = 0xF6 << 8 | data[0];
                    xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                }
                received_packet.payload[1] = data[0];
                received_packet.size = 2;
                if (received_packet.payload[1] == 1 || received_packet.payload[1] == 2 || 
                        received_packet.payload[1] == 5 || received_packet.payload[1] == 6)
                    received_packet.source = 0xF2;
                else
                    received_packet.source = 0xF6;
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(20));
                vprotocol->uart_read_bytes_event(this->uart_num, &data[1], 1, pdMS_TO_TICKS(kUartDelay), uartevtQueue); //flush lagging zero
                if(vprotocol->req_to_send && data[0] == pkt_to_send.keypadaddress) //ACK was for us.  Try to send.
                { 
                    data[rxBytes] = 0;
                    vprotocol->keypad_write(this->uart_num, pkt_to_send );

                    rxBytes = vprotocol->get_packet_event(&received_packet,data.get(),0,1, this->uart_num, pdMS_TO_TICKS(100), uartevtQueue);
                    if(rxBytes)
                    {
                        if (data[0] == pkt_to_send.sequence)
                        {
                            vprotocol->req_to_send = false;
                            vprotocol->pulse_marked = false;
#ifdef DEBUG_LOG
                            xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(20));
#endif               
                        }

                        if (vprotocol->req_to_send)
                        {
                            ESP_LOGW(TAG, "Did not find expected byte in response of %d bytes.", rxBytes);
                            vprotocol->req_to_send = false;
                        }
                        
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Did not receive any response bytes from panel.");
                        vprotocol->req_to_send = false;
                    }
                } 
                else //ACK was for another device.
                {        
                    rxBytes = vprotocol->uart_read_bytes_event(this->uart_num, data.get(), 1, pdMS_TO_TICKS(50), uartevtQueue);
 #ifdef DEBUG_LOG
                    if (rxBytes) //should receive single panel response byte
                    {
                        received_packet.payload[0] = data[0];
                        received_packet.size = 1;
                        xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
                    }
#endif
                }
            }
            else if ( data[0] == 0xF7 ) //DISPLAY
            {
                rxBytes = vprotocol->get_packet_event(&received_packet,data.get(),1,kF7MessageLength-1, this->uart_num, pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                if (valid_chksum(received_packet.payload,0,rxBytes+1))
                    received_packet.source = 0xF7;
                else
                    received_packet.source = 0xCF;
                xQueueSend(this->receiveQueue,&received_packet,0);
            }            
            else if ( data[0] == 0xF8 ) //Unknown Device on vista20P. VistaSE uses to send display updates in program mode.
            {
                if (vprotocol->legacy_programmode)
                {
                    received_packet.source = 0xDD;
                    rxBytes = vprotocol->get_packet_event(&received_packet,data.get(),1,32, this->uart_num, pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                }
                else
                {
                    rxBytes = vprotocol->uart_read_bytes_event(this->uart_num, data.get(), 2, pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                    if (rxBytes == 2)
                    {
                        received_packet.payload[1] = data[0];
                        received_packet.payload[2] = data[1];
                        rxBytes = vprotocol->get_packet_event(&received_packet,data.get(),3,static_cast<int> (received_packet.payload[2]),this->uart_num,pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                        if (valid_chksum(received_packet.payload,0,rxBytes+3))
                        { 
                            uint32_t val = 0xF8 << 8 | received_packet.payload[1];
                            if (monitor_rx_task_Handle != nullptr)
                                xTaskNotify(monitor_rx_task_Handle,val,eSetValueWithOverwrite);
                        }
                    }
                }
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
            }
            else if ( data[0] == 0xF9 ) //LRR
            {   
                rxBytes = vprotocol->uart_read_bytes_event(this->uart_num, data.get(), 2, pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                if (rxBytes == 2)
                {
                    received_packet.payload[1] = data[0];
                    received_packet.payload[2] = data[1];
                    rxBytes = vprotocol->get_packet_event(&received_packet,data.get(),3,static_cast<int> (received_packet.payload[2]),this->uart_num,pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                    if (valid_chksum(received_packet.payload,0,rxBytes+3))
                    {
                        received_packet.source = 0xF9;
                        uint32_t val = 0xF9 << 16 | received_packet.payload[1] << 8 | received_packet.payload[3];
                        if (monitor_rx_task_Handle != nullptr)
                            xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                        if (LRRemulation)
                            this->processF9(received_packet.payload);
                    }
                    else
                        received_packet.source = 0xCF;
                    xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
                    if (received_packet.source == 0xF9)
                    {
                        rxBytes = vprotocol->uart_read_bytes_event(this->uart_num, data.get(), 2, pdMS_TO_TICKS(30), uartevtQueue);
#ifdef DEBUG_LOG
                        if (rxBytes) //should receive single panel response byte
                        {   
                            received_packet.payload[0] = data[1]; //first byte is a zero
                            received_packet.size = 1;
                            xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
                        }
#endif
                    }
                }
            }
            else if ( data[0] == 0xFA && vprotocol->is_2400) //EXP
            {   
                uint8_t length = 0;
                if (!vprotocol->legacy_protocol)
                    length = kFAMessageLength;
                else
                    length = kFALegacyMessageLength;

                rxBytes = vprotocol->get_packet_event(&received_packet,data.get(),1,length-1,this->uart_num,pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                bool chk;
                if (!vprotocol->legacy_protocol)
                    chk = valid_chksum(received_packet.payload,0,rxBytes+1);
                else
                    chk = valid_chksum_two(received_packet.payload,0,rxBytes+1);
                uint32_t val;
                if (chk)
                {
                    if (!vprotocol->legacy_protocol)
                        val = 0xFA << 16 | (received_packet.payload[2] << 8) | received_packet.payload[4];
                    else
                        val = 0xFA << 16 | (received_packet.payload[1] << 8) | received_packet.payload[2];
                    if (monitor_rx_task_Handle != nullptr)
                            xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                    if (EXPemulation)
                        vprotocol->processFA(received_packet.payload);
                    received_packet.source = 0xFA;
                }
                else
                    received_packet.source = 0xCF;
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));     
            }
            else if ( data[0] == 0xFB && vprotocol->is_2400) //5881EN traffic
            {    
                rxBytes = vprotocol->get_packet_event(&received_packet,data.get(),1,kFBMessageLength,this->uart_num,pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                if (valid_chksum(received_packet.payload,0,rxBytes+1))
                {
                    uint32_t val = 0xFB << 16 | received_packet.payload[1] << 8 | received_packet.payload[3];
                    if (monitor_rx_task_Handle != nullptr)
                        xTaskNotify(monitor_rx_task_Handle,val,eSetValueWithOverwrite);                
                    if (RFRemulation)
                        this->processFB(received_packet.payload);
                    received_packet.source = 0xFB;
                }
                else
                {
                    received_packet.source = 0xCF;
                }
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
            }
            else if ( vprotocol->legacy_protocol && data[0] == 0xFE && vprotocol->is_2400 ) //VistaSE 
            {   
                uart_set_baudrate(this->uart_num,2400);
                rxBytes = vprotocol->get_packet_event(&received_packet,data.get(),1,4, this->uart_num, pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                received_packet.source = 0xDD;
                uart_set_baudrate(this->uart_num,4800);
                xQueueSend(this->receiveQueue,&received_packet,0);
            }
            else if ( vprotocol->legacy_protocol && data[0] == 0xFF && vprotocol->is_2400 ) //VistaSE 
            {   
                uart_set_baudrate(this->uart_num,2400);
                rxBytes = vprotocol->get_packet_event(&received_packet,data.get(),1,4, this->uart_num, pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                received_packet.source = 0xDD;
                uart_set_baudrate(this->uart_num,4800);
                xQueueSend(this->receiveQueue,&received_packet,0);
            }
            else if ( vprotocol->legacy_protocol && vprotocol->is_2400 ) //VistaSE 
            {   
                uart_set_baudrate(this->uart_num,2400);
                rxBytes = vprotocol->get_packet_event(&received_packet,data.get(),1,4, this->uart_num, pdMS_TO_TICKS(kUartDelay), uartevtQueue);
                received_packet.source = 0xDD;
                uart_set_baudrate(this->uart_num,4800);
                xQueueSend(this->receiveQueue,&received_packet,0);
                if (rxBytes == 4)
                {
                    vprotocol->legacy_programmode = data[2] & 0x40;
                }
            }
            else if (data[0] == 0)
            {
                //send single zeros to void!
            }
            else
            {
#ifdef DEBUG_LOG
                received_packet.payload[0] = data[0];
                received_packet.size = 1;
                xQueueSend(this->receiveQueue, &received_packet,pdMS_TO_TICKS(20));
                ESP_LOGI(TAG,"Legacy Protocol: %i", vprotocol->legacy_protocol);
#endif
            }
        }

        if (vprotocol->req_to_send && vprotocol->pulse_marked && (esp_timer_get_time() - vprotocol->pulse_mark_time > 1200000 )) //should receive ack within 1.2 seconds
        {
            ack_failures++;
            vprotocol->pulse_marked = false;
        }

        if (ack_failures == 10)
        {
            ESP_LOGW(TAG, "Failure to receive F6 ACK after 10 successive pulse marks.  Giving up.");
            vprotocol->req_to_send = false;
            ack_failures = 0;
        }

        if (!vprotocol->req_to_send)
        {
            ack_failures = 0;
            vprotocol->pulse_marked = false;
        }
    }
    ESP_LOGI(TAG, "Stopping Task");
    this->rx_tx_task_Handle = nullptr;
    vTaskDelete(nullptr);
}

void VistaBus::monitor_rx_task(void * args)
{  
    auto data = std::make_unique<uint8_t[]>(128);
    struct ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    uint32_t val = 0;

    while (1) 
    {
        //uint32_t val = 0;  // might be ok here and remove individual setters in sections below.
        if(this->stop_requested)
        {
            break;
        }

        int rxBytes = vprotocol->monitor_task_sync(data.get(), val, rcvd_extPkt);
        if (rxBytes) 
        {
            data[rxBytes] = 0;
            memset(rcvd_extPkt.payload,'\0',sizeof(rcvd_extPkt.payload));
            rcvd_extPkt.payload[0] = data[0];
            rcvd_extPkt.source = 0;
            if(static_cast<uint8_t>(val >> 8) == 0xF6) //next byte will be header of sending sequence
            {
                uint8_t n = 0;
                while ((data[0] & 0x0F) != static_cast<uint8_t>(val & 0x0F) && n < 3) //discard any mark bytes
                {
                    rxBytes = uart_read_bytes(this->ext_uart_num, data.get(), 1, pdMS_TO_TICKS(kUartDelay));
                    n++;
                }
                rcvd_extPkt.payload[0]=data[0];
                rxBytes = uart_read_bytes(this->ext_uart_num, data.get(), 1, pdMS_TO_TICKS(150));
                rcvd_extPkt.payload[1] = data[0]; //length
                vprotocol->get_packet(&rcvd_extPkt, data.get(), 2, rcvd_extPkt.payload[1], this->ext_uart_num, pdMS_TO_TICKS(150));
                if (static_cast<uint8_t>(val) == 1 || static_cast<uint8_t>(val) == 2 
                        || static_cast<uint8_t>(val) == 5 || static_cast<uint8_t>(val) == 6)
                    rcvd_extPkt.source = 0xF2;
                else
                    rcvd_extPkt.source = 0xF6;
                xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                val = 0;
            }
            else if(static_cast<uint8_t>(val >> 8) == 0xF8) //Expect response from ?
            {
                uint8_t n = 0;
                while ((data[0]) != static_cast<uint8_t>(val) && n < 2)
                {
                    rxBytes = uart_read_bytes(this->ext_uart_num, data.get(), 1, pdMS_TO_TICKS(kUartDelay));
                    n++;
                }
#ifdef DEBUG_LOG
                    rcvd_extPkt.payload[0] = data[0];
                    rcvd_extPkt.size = 1;
                    xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
#endif
                val = 0;
            }
            else if(static_cast<uint8_t>(val >> 16) == 0xF9) //Expect response from LRR
            {
                uint8_t n = 0;
                uint8_t mb = static_cast<uint8_t>(val >> 8) + 0x40;
                while (data[0] != mb && data[0] != static_cast<uint8_t>(val >> 8) && n < 2)
                {
                    rxBytes = uart_read_bytes(this->ext_uart_num, data.get(), 1, pdMS_TO_TICKS(kUartDelay));
                    n++;
                }
                rcvd_extPkt.payload[0] = data[0];
                rcvd_extPkt.source = 0xF9;
                if (static_cast<uint8_t>(val) == 0x53)
                {
                    vprotocol->get_packet(&rcvd_extPkt, data.get(), 1, 5, this->ext_uart_num, pdMS_TO_TICKS(25));
                    xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                }
#ifdef DEBUG_LOG                
                else
                {
                    rcvd_extPkt.size = 1;
                    xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                }
#endif
                val = 0;
            }
            else if (static_cast<uint8_t>(val >> 16) == 0xFA) // Incoming from expander such as 4219. 7F=07,FE=08, FD=09, FB=10, F7=11
            {
                if (static_cast<uint8_t>(val) == 0xF1) //Incoming zone data from Expander
                {
                    uint8_t req_addr = 99;
                    switch (static_cast<uint8_t>(val >> 8))
                    {
                        case 0x02:
                            req_addr = 7;
                            break;
                        case 0x04:
                            req_addr = 8;
                            break;
                        case 0x08:
                            req_addr = 9;
                            break;
                        case 0x10:
                            req_addr = 10;
                            break;
                        case 0x20:
                            req_addr = 11;
                            break;
                        default:
                            break;                                                                                                                
                    }
                    uint8_t n = 0;
                    while (data[0] != req_addr && n < 2)
                    {
                        rxBytes = uart_read_bytes(this->ext_uart_num, data.get(), 1, pdMS_TO_TICKS(kUartDelay));
                        n++;
                    }
                    rcvd_extPkt.payload[0] = data[0];
                    int res = vprotocol->get_packet(&rcvd_extPkt, data.get(), 1, 3, this->ext_uart_num, 
                            pdMS_TO_TICKS(kUartDelay)); 
                    if (res > 0)
                    {
                        rcvd_extPkt.source = 0xFA;
                        xQueueSend(this->receiveQueue,&rcvd_extPkt,pdMS_TO_TICKS(10));
                    }
                }
                else
                {
                    uint8_t n = 0;
                    while (data[0] != 0xF0 && n < 2)
                    {
                        rxBytes = uart_read_bytes(this->ext_uart_num, data.get(), 1, pdMS_TO_TICKS(kUartDelay));
                        n++;
                    }
                    rcvd_extPkt.payload[0] = data[0];
                    vprotocol->get_packet(&rcvd_extPkt, data.get(), 1, 5, this->ext_uart_num, pdMS_TO_TICKS(50));
                    rcvd_extPkt.source = 0xFA;
                    xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));                    
                }
                val = 0;
            }
            else if (static_cast<uint8_t>(val >> 16) == 0xFB)
            {
                uint8_t req_addr = 99;
                switch (static_cast<uint8_t>(val >> 8))
                {
                    case 0x01: 
                        req_addr = 7;
                        break;
                    case 0x02: 
                        req_addr = 0;
                        break;
                    case 0x04: 
                        req_addr = 1;
                        break;
                    case 0x08: 
                        req_addr = 2;
                        break;
                    case 0x10: 
                        req_addr = 3;
                        break;
                    case 0x20: 
                        req_addr = 4;
                        break;
                    case 0x40: 
                        req_addr = 5;
                        break;
                    case 0x80: 
                        req_addr = 6;
                        break;
                    default:
                        break;                                                                                                                
                }
                uint8_t n = 0;
                while (data[0] != req_addr && n < 2)
                {
                    rxBytes = uart_read_bytes(this->ext_uart_num, data.get(), 1, pdMS_TO_TICKS(kUartDelay));
                    n++;
                }
                rcvd_extPkt.payload[0] = data[0];
                if ((val & 0xFF) == 0xF1) //Incoming zone data from Radio Frequency Receiver
                {
                    int res = vprotocol->get_packet(&rcvd_extPkt, data.get(), 1, kRFZoneMessageLength-1, this->ext_uart_num, 
                        pdMS_TO_TICKS(kUartDelay));
                    if (res > 0)
                    {
                        rcvd_extPkt.source = 0xFB;
                        xQueueSend(this->receiveQueue,&rcvd_extPkt,pdMS_TO_TICKS(20));
                    }
                }
                else //Response to FB poll command
                {
                    vprotocol->get_packet(&rcvd_extPkt, data.get(), 1, 3, this->ext_uart_num, pdMS_TO_TICKS(kUartDelay));
                    rcvd_extPkt.source = 0xFB;
                    xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                }
                val = 0;
            }
            else //put in buffer for printing to log
            {
#ifdef DEBUG_LOG
                rcvd_extPkt.payload[0] = data[0];
                rcvd_extPkt.size = 1;
                xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(20));
#endif
            }
        }
    }
    ESP_LOGI(TAG, "Stopping Task");
    this->monitor_rx_task_Handle = nullptr;
    vTaskDelete(nullptr);
}

static uint8_t getExpanderAddress(uint8_t zone)
{
    // expander address 7 = zones: 9 - 16
    // expander address 8 = zones:  17 - 24
    // expander address 9 = zones: 25 - 32
    // expander address 10 = zones: 33 - 40
    // expander address 11 = zones: 41 - 48
    uint8_t address = 0;
    if (zone > 8 && zone < 17)  
        address = 7;
    else if (zone > 16 && zone < 25)
        address = 8;
    else if (zone > 24 && zone < 33)
        address = 9;
    else if (zone > 32 && zone < 41)
        address = 10;
    else if (zone > 40 && zone < 49)
        address = 11;
    return address;
}

VistaBus::EmulatedExpander *VistaBus::getExpander(uint8_t address)
{
    for (auto &it: emulated_expanders)
    {
        if (it.address == address)
            return &(it);
    }
    return nullptr;
}

void VistaBus::processF9(const char * cbuf)
{
    // For timing , must handle 0xF9 packet here if emulating rather than through queues in vistaalarm process. 
    char response[6];
    if (cbuf[3] == 0x53)
    // F9 83 02 53 2F 
    // C3 04 00 60 00 D9 
    {
        response[0] = cbuf[1] + 0x40;
        response[1] = 0x04;
        response[2] = 0;
        response[3] = 0;
        response[4] = 0;
        response[5] = (((0x0F - (response[0] >> 4)) & 0x0F) << 4) | 0x09;
        uart_write_bytes(this->uart_num,response, 6);
    }
    else if (cbuf[3] == 0x48 || cbuf[3] == 0x52 || cbuf[3] == 0x58)
    {
        uart_write_bytes(this->uart_num,&cbuf[1], 1);
    }
}

void VistaBus::processFB(const char * cbuf)
{
    // For timing , must handle 0xFB packet here if emulating rather than through queues in vistaalarm process. 
    char type = cbuf[3];
    // 0xF1 - response to request, 0x80 - retry, 0x60 or 0x81 supervision, 0x82 supervision w/ type response
    if (type == 0xF1)
    {   
        DeviceMsg rfMsg;
        if (xQueueReceive(this->deviceMsgQueue,&rfMsg,pdMS_TO_TICKS(100)) == pdPASS)
        {
            char seq = cbuf[2];
            char lcbuf[7];
            uint8_t expSeq = (seq == 0x20 ? 0x54 : 0x51);
            lcbuf[0] = emulated_rf_receiver.address;
            lcbuf[1] = expSeq;
            lcbuf[2] = rfMsg.source >> 16 | 0x80;  //Set buf 2,3,4 to rf serial number
            lcbuf[3] = rfMsg.source >> 8 & 0xFF;
            lcbuf[4] = rfMsg.source & 0xFF;
            lcbuf[5] = rfMsg.msg; //Set to fault status with loop mask
            lcbuf[6] = calc_chksum_two(lcbuf,0,6);
            uart_write_bytes(this->uart_num,lcbuf, 7);
        }
    }
    else if (type == 0x60 || type == 0x81 || type == 0x82)
    { // supervision query  ToDo:  Come back and check this.
        char seq = cbuf[2];
        char lcbuf[4];
        uint8_t address = emulated_rf_receiver.address;  //Set this to rf address
        uint8_t expSeq = (seq == 0x20 ? 0x24 : 0x21);
        uint8_t lcbuflen = 4;   
        lcbuf[0] = emulated_rf_receiver.address;
        lcbuf[1] = expSeq;
        lcbuf[2] = 0x05; // 5881ENL = 3, 5881ENH = 5
        uint8_t chksum = 0;
        for (int x = 0; x < lcbuflen-1; x++)
        {
            chksum += lcbuf[x];
        }
        //lcbuflen ++;
        chksum = ~chksum + 1;
        lcbuf[lcbuflen-1] = chksum;
        uart_write_bytes(this->uart_num,lcbuf, lcbuflen);
    }
}

//Put request in Queue to nudge panel to send F1 request
void VistaBus::requestF1(uint8_t address)
{
    SendPacket pkt;
    pkt.type = 2;
    pkt.keypadaddress = address;
    pkt.payload[0] = 0;
    pkt.size = 0;
    xQueueSend(sendQueue,&pkt,0);
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
        uint8_t z = zone & 0x07;
        expander->fault_NO_Bits = fault ? expander->fault_NO_Bits | (0x01 << (8-z)) : expander->fault_NO_Bits & ~(0x01 << (8-z));
        DeviceMsg expMsg;
        expMsg.address = address;
        expMsg.source = zone;
        expMsg.msg = fault;
        expMsg.time = esp_timer_get_time();
        xQueueSend(deviceMsgQueue,&expMsg,0);     
    }
}

void VistaBus::setExpTamper(uint8_t zone, bool tamper_active)
{
    uint8_t address = getExpanderAddress(zone);
    EmulatedExpander *expander = getExpander(address);
    if (expander != nullptr)
    {
        uint8_t z = zone & 0x07;
        expander->fault_NC_Bits = tamper_active ? expander->fault_NC_Bits | (0x01 << (8-z)) : expander->fault_NC_Bits & ~(0x01 << (8-z));
        expander->fault_NO_Bits = tamper_active ? expander->fault_NO_Bits | (0x01 << (8-z)) : expander->fault_NO_Bits & ~(0x01 << (8-z));
    }
}

void VistaBus::sendRFmsg(uint32_t serial, uint8_t msg)
{
    DeviceMsg rfMsg;
    rfMsg.address = emulated_rf_receiver.address;
    rfMsg.source = serial;
    rfMsg.msg = msg;
    rfMsg.time = esp_timer_get_time();
    xQueueSend(deviceMsgQueue,&rfMsg,0);
}

void VistaBus::add_emulated_expander(uint8_t zone)
{   
    uint8_t address = 0;
    if (!vprotocol->legacy_protocol)
        address = getExpanderAddress(zone);
    else
        address = 1;
    EmulatedExpander *expander = getExpander(address);
    uint8_t z = zone & 0x07;
    if (expander == nullptr) //emulated expander does not exist, add it
    {   
        EmulatedExpander new_expander;
        new_expander.address = address;
        new_expander.fault_NC_Bits = new_expander.fault_NC_Bits & ~(0x01 << (8-z));
        this->emulated_expanders.push_back(new_expander);
        ESP_LOGI(TAG,"Adding new emulated expander on address:%d for emulated zone:%d",new_expander.address,zone);
        EXPemulation = true;
    }
    else //emulated expander already created
    {
        expander->fault_NC_Bits = expander->fault_NC_Bits & ~(0x01 << (8-z));
        ESP_LOGI(TAG,"Existing emulated expander on address:%d handling emulated zone:%d",expander->address,zone);
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

