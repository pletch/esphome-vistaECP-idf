#include "vistabus.h"
//#include "vistaprotocol.h"
#include "vistaalarm.h"  //to bring in init macro definitions
#ifdef DEBUG_PULSE
#include "driver/rmt_rx.h"
#endif


VistaBus::VistaBus()
{
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
    if (this->rx_tx_task_Handle != NULL) 
        stop();
    vQueueDelete(this->receiveQueue);
    vQueueDelete(this->sendQueue);
}

void VistaBus::begin(int uartnum, int rxpin, int txpin, int extuartnum = -1, int monitorpin = -1) 
{
    this->uartNum = uartnum;
    this->rxPin = rxpin;
    this->txPin = txpin;
    this->extuartNum = extuartnum;
    this->monitorPin = monitorpin;

    if (this->receiveQueue == NULL || this->sendQueue == NULL || this->deviceMsgQueue == NULL)
    {
        ESP_LOGE(TAG, "Memory for task queues was not allocated. Aborting!");
        return;
    }

    init_uart(static_cast<uart_port_t>(this->uartNum),static_cast<gpio_num_t>(this->rxPin), static_cast<gpio_num_t>(this->txPin));

    xTaskCreate(rx_tx_task_start, "uart_rx_tx_task", UART_RX_TASK_STACK_SIZE, (void *) this, configMAX_PRIORITIES-1, &this->rx_tx_task_Handle);
    if (monitorPin != -1)
    {
        init_uart(static_cast<uart_port_t>(this->extuartNum),static_cast<gpio_num_t>(this->monitorPin), static_cast<gpio_num_t>(-1));
        xTaskCreate(monitor_rx_task_start, "uart_monitor_rx_task", UART_RX_EXT_TASK_STACK_SIZE, (void *) this, configMAX_PRIORITIES-10, &this->monitor_rx_task_Handle);
    }
}

bool VistaBus::stop() 
{
    this->stop_requested = true;

    //monitor_rx_task must be shutdown first and is potentially parked at uartreadbytes.
    //send an FF byte to wake so it shuts down. Must shut down gracefully to delete data.
    char tmp[1];
    tmp[0] = 0xFF;
    while (monitor_rx_task_Handle != NULL) //wait for task to terminate
    {
        uart_write_bytes(static_cast<uart_port_t>(this->uartNum),tmp,1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    while(rx_tx_task_Handle != NULL) //wait for task to terminate
    {
        vTaskDelay(20);
    }
    uart_driver_delete(static_cast<uart_port_t>(this->uartNum));
    if(this->monitorPin != -1) {
        uart_driver_delete(static_cast<uart_port_t>(this->extuartNum));
    }
    return true;
}

bool VistaBus::write(const char * data_to_write, int size, int keypadaddress, int sequence) //move
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

bool VistaBus::writedirect(const char * hex_data_to_write, int size, int keypadaddress, int sequence) //move
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
    rmt_channel_handle_t rx_chan = NULL;
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

bool VistaBus::read_packet(char * data, int &len, int &type, int &src, bool with_delay) //move
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
        ESP_ERROR_CHECK(uart_driver_install(u_n, RX_BUF_SIZE + 8, 0, 0, NULL, intr_alloc_flags));
    }
    else
    {
        ESP_ERROR_CHECK(uart_driver_install(u_n, RX_BUF_SIZE + 8, 0, 25, &uartevtQueue, intr_alloc_flags));
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

inline bool validChksum(const char * cbuf, int start, int len) //move
{
  uint16_t chksum = 0;
  for (uint8_t x = start; x < len; x++)
  {
    chksum += cbuf[x];
  }
  if (chksum % 256 == 0)
    return true;
  else
    return false;
}

static int get_Packet(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, int len, uart_port_t uart_num, int timeout) //move
{
    const int rxBytes = uart_read_bytes(uart_num, rxbuf, len, timeout);
    memcpy(received_packet->payload+start,rxbuf,rxBytes);
    received_packet->payload[rxBytes+start] = '\0';
    received_packet->size = rxBytes+start;
    return rxBytes;
}

static int get_Packet_event(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, int len, uart_port_t uart_num, int timeout, QueueHandle_t queue)  //move
{
    const int rxBytes = uart_read_bytes_event(uart_num, rxbuf, len, timeout, queue);
    memcpy(received_packet->payload+start,rxbuf,rxBytes);
    received_packet->payload[rxBytes+start] = '\0';
    received_packet->size = rxBytes+start;
    return rxBytes;
}

void VistaBus::rx_tx_task(void * args)
{
    auto data = std::make_unique<uint8_t[]>(RX_BUF_SIZE+1);
    ReceivedPacket received_packet;
    received_packet.type = 0;

    gpio_config_t io_conf = 
    {
        .pin_bit_mask = static_cast<uint64_t>(1 << static_cast<gpio_num_t>(this->rxPin)),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,       
    };

    gpio_config(&io_conf);
    (void)gpio_install_isr_service(0);

    while (1) 
    {
        // Vista-20p pulse cycle is 330 ms but older panel such as 4140XMPT2 cycle is 525 ms.
        int uart_delay = 550;

        // Handle any queued device msgs via F1 request  
        if (uxQueueMessagesWaiting(deviceMsgQueue) && !req_to_send && (esp_timer_get_time() - request_F1_time > uart_delay*1000))
        {
            DeviceMsg q_msg;
            xQueuePeek(deviceMsgQueue,&q_msg,pdMS_TO_TICKS(0));
            requestF1(q_msg.address);
            request_F1_time = esp_timer_get_time();  
        }
        if(this->stop_requested && monitor_rx_task_Handle == NULL)
        {
            this->panel_connected = false;
            break;
        }
        uint64_t now = esp_timer_get_time();
        if (now - last_data_received > 30*1000*1000)
            this->panel_connected = false;

#ifdef DEBUG_PULSE
        if (now > 60*1000*1000)
        {
            ESP_LOGE(TAG,"Collecting pulse pattern at %llu", now);
            capture_pulse_pattern(static_cast<gpio_num_t>(this->rxPin));
            vTaskDelay(10);
            continue;
        }
#endif
        SendPacket pkt_to_send;
        //this->req_to_send = checkSendQ(pkt_to_send);
        this->req_to_send = vprotocol.checkSendQ(this->sendQueue, pkt_to_send, this->req_to_send);

        //int rxBytes = handleUARTevents(data.get(), pkt_to_send.keypadaddress);    
        int rxBytes = vprotocol.handleUARTevents(this->uartevtQueue, this->rx_tx_task_Handle, this->uartNum, this->rxPin, 
                this->req_to_send, this->pulse_marked, this->pulse_mark_time, data.get(), pkt_to_send.keypadaddress);

        if (this->req_to_send && !this->pulse_marked) //loop faster when needing to mark pulse to send
            uart_delay = 20;
        if (rxBytes) 
        {
            this->panel_connected = true;
            last_data_received = esp_timer_get_time();
            data[rxBytes] = 0;
            memset(received_packet.payload,'\0',sizeof(received_packet.payload));
            received_packet.payload[0] = data[0];
            received_packet.source = 0;
            if (this->req_to_send && this->pulse_marked && pkt_to_send.type == 2)  //No ACK for this type of send
            {
                this->req_to_send = false;
            }
            if ( data[0] == 0xF6) //SEND ACK Received
            {                 
                rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data.get(), 1, pdMS_TO_TICKS(UART_DELAY), uartevtQueue); //Get Address
                if(data[0] != 0 && monitor_rx_task_Handle != NULL)
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
                uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), &data[1], 1, pdMS_TO_TICKS(UART_DELAY), uartevtQueue); //flush lagging zero
                if(req_to_send && data[0] == pkt_to_send.keypadaddress) //ACK was for us.  Try to send.
                { 
                    char outbuffer[24];
                    memset(outbuffer,'\0',sizeof(outbuffer));
                    data[rxBytes] = 0;
                    char keys_to_send[24];
                    outbuffer[0] = pkt_to_send.sequence;
                    outbuffer[1] = pkt_to_send.size+1;
                    uint8_t chksum = 0;
                    chksum += outbuffer[0] + outbuffer[1];
                    for (int i=2; i < pkt_to_send.size+2; i++)
                    {
                        if (pkt_to_send.type == 0) //write direct as hex
                        {
                            outbuffer[i] = pkt_to_send.payload[i-2];
                        }
                        else //translate from ascii before write
                        {
                            if (pkt_to_send.payload[i-2] >= 0x30 && pkt_to_send.payload[i-2] <= 0x39)
                                outbuffer[i] = (pkt_to_send.payload[i-2] - 0x30);
                            else if (pkt_to_send.payload[i-2] == 0x23)
                                outbuffer[i] = 0x0B;
                            else if (pkt_to_send.payload[i-2] == 0x2A)
                                outbuffer[i] = 0x0A;
                            else if (pkt_to_send.payload[i-2] == 0x46)
                                outbuffer[i] = 0x0C;
                            else if (pkt_to_send.payload[i-2] == 0x4D)
                                outbuffer[i] = 0x0D;
                            else if (pkt_to_send.payload[i-2] == 0x50)
                                outbuffer[i] = 0x0E;
                            else if (pkt_to_send.payload[i-2] == 0x47)
                                outbuffer[i] = 0x0F;
                            else if (pkt_to_send.payload[i-2] >= 0x41 && pkt_to_send.payload[i-2] <= 0x44)
                                outbuffer[i] = (pkt_to_send.payload[i-2] - 0x25);
                        }
                        chksum += outbuffer[i];
                    }
                    outbuffer[pkt_to_send.size+2] = ~chksum+1;
                    uart_write_bytes(static_cast<uart_port_t>(this->uartNum), outbuffer,pkt_to_send.size+3);

                    rxBytes = get_Packet_event(&received_packet,data.get(),0,1, static_cast<uart_port_t>(this->uartNum), pdMS_TO_TICKS(100), uartevtQueue);
                    if(rxBytes)
                    {
                        if (data[0] == outbuffer[0])
                        {
                            req_to_send = false;
                            pulse_marked = false;
#ifdef DEBUG_LOG
                            xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(20));
#endif               
                        }

                        if (req_to_send)
                        {
                            ESP_LOGW(TAG, "Did not find expected byte in response of %d bytes.", rxBytes);
                            req_to_send = false;
                        }
                        
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Did not receive any response bytes from panel.");
                        req_to_send = false;
                    }
                } 
                else //ACK was for another device.
                {        
                    rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data.get(), 1, pdMS_TO_TICKS(50), uartevtQueue);
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
                rxBytes = get_Packet_event(&received_packet,data.get(),1,F7_MESSAGE_LENGTH-1, static_cast<uart_port_t>(this->uartNum), pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                if (validChksum(received_packet.payload,0,rxBytes+1))
                    received_packet.source = 0xF7;
                else
                    received_packet.source = 0xCF;
                xQueueSend(this->receiveQueue,&received_packet,0);
            }            
            else if ( data[0] == 0xF2 ) //AUI
            {
                rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data.get(), 1, pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                received_packet.payload[1] = data[0];
                rxBytes = get_Packet_event(&received_packet,data.get(),2,static_cast<int> (received_packet.payload[1]),static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                if (validChksum(received_packet.payload,0,rxBytes+2)) 
                    received_packet.source = 0xF2;
                else
                    received_packet.source = 0xCF;
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
            }
            else if ( data[0] == 0xFA ) //EXP
            {                        
                rxBytes = get_Packet_event(&received_packet,data.get(),1,FA_MESSAGE_LENGTH-1,static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                if (validChksum(received_packet.payload,0,rxBytes+1))
                {
                    uint32_t val = 0xFA << 16 | (received_packet.payload[2] << 8) | received_packet.payload[4];
                    if (monitor_rx_task_Handle != NULL)
                            xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                    if (EXPemulation)
                        this->processFA(received_packet.payload);
                    received_packet.source = 0xFA;
                }
                else
                    received_packet.source = 0xCF;
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));     
            }
            else if ( data[0] == 0xF9 ) //LRR
            {   
                rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data.get(), 2, pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                if (rxBytes == 2)
                {
                    received_packet.payload[1] = data[0];
                    received_packet.payload[2] = data[1];
                    rxBytes = get_Packet_event(&received_packet,data.get(),3,static_cast<int> (received_packet.payload[2]),static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                    if (validChksum(received_packet.payload,0,rxBytes+3))
                    {
                        received_packet.source = 0xF9;
                        uint32_t val = 0xF9 << 16 | received_packet.payload[1] << 8 | received_packet.payload[3];
                        if (monitor_rx_task_Handle != NULL)
                            xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                        if (LRRemulation)
                            this->processF9(received_packet.payload);
                    }
                    else
                        received_packet.source = 0xCF;
                    xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
                    if (received_packet.source == 0xF9)
                    {
                        rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data.get(), 2, pdMS_TO_TICKS(30), uartevtQueue);
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
            else if ( data[0] == 0xFB ) //5881EN traffic
            {    
                rxBytes = get_Packet_event(&received_packet,data.get(),1,FB_MESSAGE_LENGTH-1,static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                if (validChksum(received_packet.payload,0,rxBytes+1))
                {
                    uint32_t val = 0xFB << 16 | received_packet.payload[1] << 8 | received_packet.payload[3];
                    if (monitor_rx_task_Handle != NULL)
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
            else if ( data[0] == 0xF8 ) //Unknown Device
            {
                rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data.get(), 2, pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                if (rxBytes == 2)
                {
                    received_packet.payload[1] = data[0];
                    received_packet.payload[2] = data[1];
                    rxBytes = get_Packet_event(&received_packet,data.get(),3,static_cast<int> (received_packet.payload[2]),static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                    if (validChksum(received_packet.payload,0,rxBytes+3))
                    { 
                        uint32_t val = 0xF8 << 8 | received_packet.payload[1];
                        if (monitor_rx_task_Handle != NULL)
                            xTaskNotify(monitor_rx_task_Handle,val,eSetValueWithOverwrite);
                    }
                }
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
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
#endif
            }
        }

        if (req_to_send && pulse_marked && (esp_timer_get_time() - pulse_mark_time > 1200000 )) //should receive ack within 1.2 seconds
        {
            ack_failures++;
            pulse_marked = false;
        }

        if (ack_failures == 10)
        {
            ESP_LOGW(TAG, "Failure to receive F6 ACK after 10 successive pulse marks.  Giving up.");
            req_to_send = false;
            ack_failures = 0;
        }

        if (!req_to_send)
        {
            ack_failures = 0;
            pulse_marked = false;
        }
    }
    ESP_LOGI(TAG, "Stopping Task");
    this->rx_tx_task_Handle = NULL;
    vTaskDelete(NULL);
}

void VistaBus::monitor_rx_task(void * args)
{  
    auto data = std::make_unique<uint8_t[]>(128);
    struct ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    uint32_t val = 0;

    while (1) 
    {
        if(this->stop_requested)
        {
            break;
        }
        int rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->extuartNum), data.get(), 1, portMAX_DELAY);
        if (val == 0)
            {
                xTaskNotifyWait(0,0xFFFFFFFF,&val,pdMS_TO_TICKS(400));  //data of interest incoming according to RX_TX Task
            }
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
                    rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->extuartNum), data.get(), 1, pdMS_TO_TICKS(UART_DELAY));
                    n++;
                }
                rcvd_extPkt.payload[0]=data[0];
                rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->extuartNum), data.get(), 1, pdMS_TO_TICKS(150));
                rcvd_extPkt.payload[1] = data[0]; //length
                get_Packet(&rcvd_extPkt, data.get(), 2, rcvd_extPkt.payload[1], static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(150));
                if (static_cast<uint8_t>(val) == 1 || static_cast<uint8_t>(val) == 2 
                        || static_cast<uint8_t>(val) == 5 || static_cast<uint8_t>(val) == 6)
                    rcvd_extPkt.source = 0xF2;
                else
                    rcvd_extPkt.source = 0xF6;
                xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                val = 0;
            }
            else if(static_cast<uint8_t>(val >> 16) == 0xF9) //Expect response from LRR
            {
                uint8_t n = 0;
                uint8_t mb = static_cast<uint8_t>(val >> 8) + 0x40;
                while (data[0] != mb && data[0] != static_cast<uint8_t>(val >> 8) && n < 2)
                {
                    rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->extuartNum), data.get(), 1, pdMS_TO_TICKS(UART_DELAY));
                    n++;
                }
                rcvd_extPkt.payload[0] = data[0];
                rcvd_extPkt.source = 0xF9;
                if (static_cast<uint8_t>(val) == 0x53)
                {
                    get_Packet(&rcvd_extPkt, data.get(), 1, 5, static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(25));
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
            else if(static_cast<uint8_t>(val >> 8) == 0xF8) //Expect response from LRR
            {
                uint8_t n = 0;
                while ((data[0]) != static_cast<uint8_t>(val) && n < 2)
                {
                    rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->extuartNum), data.get(), 1, pdMS_TO_TICKS(UART_DELAY));
                    n++;
                }
#ifdef DEBUG_LOG
                    rcvd_extPkt.payload[0] = data[0];
                    rcvd_extPkt.size = 1;
                    xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
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
                        rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->extuartNum), data.get(), 1, pdMS_TO_TICKS(UART_DELAY));
                        n++;
                    }
                    rcvd_extPkt.payload[0] = data[0];
                    int res = get_Packet(&rcvd_extPkt, data.get(), 1, 3, static_cast<uart_port_t>(this->extuartNum), 
                            pdMS_TO_TICKS(UART_DELAY)); 
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
                        rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->extuartNum), data.get(), 1, pdMS_TO_TICKS(UART_DELAY));
                        n++;
                    }
                    rcvd_extPkt.payload[0] = data[0];
                    get_Packet(&rcvd_extPkt, data.get(), 1, 5, static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(50));
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
                    rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->extuartNum), data.get(), 1, pdMS_TO_TICKS(UART_DELAY));
                    n++;
                }
                rcvd_extPkt.payload[0] = data[0];
                if ((val & 0xFF) == 0xF1) //Incoming zone data from Radio Frequency Receiver
                {
                    int res = get_Packet(&rcvd_extPkt, data.get(), 1, RF_ZONE_MESSAGE_LENGTH-1, static_cast<uart_port_t>(this->extuartNum), 
                        pdMS_TO_TICKS(UART_DELAY));
                    if (res > 0)
                    {
                        rcvd_extPkt.source = 0xFB;
                        xQueueSend(this->receiveQueue,&rcvd_extPkt,pdMS_TO_TICKS(20));
                    }
                }
                else //Response to FB poll command
                {
                    get_Packet(&rcvd_extPkt, data.get(), 1, 3, static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(UART_DELAY));
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
    this->monitor_rx_task_Handle = NULL;
    vTaskDelete(NULL);
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

VistaBus::emulatedExpander *VistaBus::getExpander(uint8_t address)
{
    for (auto &it: emulated_expanders)
    {
        if (it.address == address)
            return &(it);
    }
    return NULL;
}

void VistaBus::processF9(const char * cbuf)
{
    // For timing , must handle 0xF9 packet here if emulating rather than through queues in vistaalarm process. 
    char response[6];
    if (cbuf[3] == 0x53)
    {
        response[0] = cbuf[1] + 0x40;
        response[1] = 0x04;
        response[2] = 0;
        response[3] = 0;
        response[4] = 0;
        response[5] = (((0x0F - (response[0] >> 4)) & 0x0F) << 4) | 0x09;
        uart_write_bytes(static_cast<uart_port_t>(this->uartNum),response, 6);
    }
    else if (cbuf[3] == 0x48 || cbuf[3] == 0x52 || cbuf[3] == 0x58)
    {
        uart_write_bytes(static_cast<uart_port_t>(this->uartNum),&cbuf[1], 1);
    }
}

void VistaBus::processFA(const char * cbuf)
{
    // For timing , must handle 0xFA packet here if emulating rather than through queues in vistaalarm process. 
    char type = cbuf[4];
    char seq = cbuf[3];
    char lcbuf[5];
    int lcbuflen = 5;
    bool valid_address = false;
    uint8_t address = 0;
    uint8_t expSeq = (seq == 0x20 || seq == 0x21 ? 0x34 : 0x31);
    for (int index = 1; index <= 5; index++)
    {
        if (cbuf[2] == 0x01 << index)
        {
            address = 6+index;
            valid_address = true;
            break;
        }
    }
    if (emulated_expanders.size() && valid_address)  //check if any emulated expanders present
    {
        emulatedExpander *expander = getExpander(address);
        // we use zone to either | or & bits depending if in fault or reset
        // 0xF1 - response to request, 0xf7 - poll, 0x80 - retry
        if (expander != NULL)
        {
            if (type == 0xF1)
            {  
                DeviceMsg expMsg;
                xQueueReceive(this->deviceMsgQueue,&expMsg,portMAX_DELAY);
                lcbuf[0] = address;
                lcbuf[1] = expSeq;
                uint8_t z = expMsg.source & 0x07;
                lcbuf[2] = z ? 0 : 0x01;
                uint8_t chksum = 0;
                lcbuf[3] = (z << 5) ^ (0x10*expMsg.msg); // we send out the current zone state
                for (int x = 0; x < lcbuflen-1; x++)
                {
                    chksum += lcbuf[x];
                }
                chksum = ~chksum + 1;
                lcbuf[lcbuflen-1] = chksum;
                uart_write_bytes(static_cast<uart_port_t>(this->uartNum),lcbuf, lcbuflen);
            }
            else if (type == 0xF7)
            {
                lcbuf[0] = 0xF0;
                lcbuf[1] = expSeq;
                lcbuf[2] = expander->fault_NO_Bits;                      
                lcbuf[3] = expander->fault_NC_Bits; 
                uint8_t chksum = 0;
                for (int x = 0; x < lcbuflen-1; x++)
                {
                    chksum += lcbuf[x];
                }
                chksum = ~chksum + 1;
                lcbuf[lcbuflen-1] = chksum;
                uart_write_bytes(static_cast<uart_port_t>(this->uartNum),lcbuf, lcbuflen);
            }
        }
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
            int lcbuflen = 7;
            uint8_t expSeq = (seq == 0x20 ? 0x54 : 0x51);
            lcbuf[0] = emulated_rf_receiver.address;
            lcbuf[1] = expSeq;
            lcbuf[2] = rfMsg.source >> 16 | 0x80;  //Set buf 2,3,4 to rf serial number
            lcbuf[3] = rfMsg.source >> 8 & 0xFF;
            lcbuf[4] = rfMsg.source & 0xFF;
            lcbuf[5] = rfMsg.msg; //Set to fault status with loop mask
            uint8_t chksum = 0;
            for (int x = 0; x < lcbuflen-1; x++)
            {
                chksum += lcbuf[x];
            }
            chksum = ~chksum + 1;
            lcbuf[lcbuflen-1] = chksum;
            uart_write_bytes(static_cast<uart_port_t>(this->uartNum),lcbuf, lcbuflen);
        }
    }
    else if (type == 0x60 || type == 0x81 || type == 0x82)
    { // supervision query
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
        lcbuflen ++;
        chksum = ~chksum + 1;
        lcbuf[lcbuflen-1] = chksum;
        uart_write_bytes(static_cast<uart_port_t>(this->uartNum),lcbuf, lcbuflen);
    }
}

//Nudge panel to send F1 request
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
    uint8_t address = getExpanderAddress(zone);
    emulatedExpander *expander = getExpander(address);
    if (expander != NULL)
    {
        uint8_t z = zone & 0x07;
        expander->fault_NO_Bits = fault ? expander->fault_NO_Bits | (0x01 << (8-z)) : expander->fault_NO_Bits & ~(0x01 << (8-z));
        DeviceMsg expMsg;
        expMsg.address = address;
        expMsg.source = zone;
        expMsg.msg = fault;
        xQueueSend(deviceMsgQueue,&expMsg,0);       
    }
}

void VistaBus::setExpTamper(uint8_t zone, bool tamper_active)
{
    uint8_t address = getExpanderAddress(zone);
    emulatedExpander *expander = getExpander(address);
    if (expander != NULL)
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
    xQueueSend(deviceMsgQueue,&rfMsg,0);
}

void VistaBus::add_emulated_expander(uint8_t zone)
{   
    uint8_t address = getExpanderAddress(zone);
    emulatedExpander *expander = getExpander(address);
    uint8_t z = zone & 0x07;
    if (expander == NULL) //emulated expander does not exist, add it
    {   
        emulatedExpander new_expander;
        new_expander.address = getExpanderAddress(zone);
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