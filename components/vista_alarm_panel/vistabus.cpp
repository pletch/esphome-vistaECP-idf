#include "vistabus.h"
#include "esp_log.h"
#include "vistaalarm.h"  //to bring in init macro definitions


VistaBus::VistaBus()
{
    this->receiveQueue = xQueueCreate(25,sizeof(ReceivedPacket)); 
    this->sendQueue = xQueueCreate(8, sizeof(SendPacket));
    this->panel_connected = false;
    this->stop_requested = false;
    this->LRRemulation = false;
    this->EXPemulation = false;
}

VistaBus::~VistaBus()
{
    if (this->rx_tx_task_Handle != NULL) 
    {
        stop();
    }
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

    if (this->receiveQueue == NULL || this->sendQueue == NULL)
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
    //send an FF byte to wake so it shuts down. Must shut down gracefully to free(data).
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

bool VistaBus::write(const char * data_to_write, int size, int keypadaddress) 
{
    SendPacket sendpkt;
    strncpy(sendpkt.payload,data_to_write,24);
    sendpkt.keypadaddress = keypadaddress;
    sendpkt.type = 1;
    sendpkt.size = size;
    bool result = false;
    result = xQueueSend(sendQueue,&sendpkt,0) == pdPASS;

    return result;
}

bool VistaBus::writedirect(const char * hex_data_to_write, int size, int keypadaddress) 
{
    SendPacket sendpkt;
    memcpy(sendpkt.payload,hex_data_to_write,size);
    sendpkt.keypadaddress = keypadaddress;
    sendpkt.type = 0;
    sendpkt.size = size;
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

static bool example_rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
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
        .resolution_hz = 1 * 1000 * 1000, // 0.1 MHz tick resolution, i.e., 1 tick = 10 µs
        .mem_block_symbols = 128,          // memory block size, 64 * 4 = 256 Bytes
        .intr_priority = 0,
        .flags = {
            .invert_in = true,         // do not invert input signal
            .with_dma = false,          // do not need DMA backend
            .io_loop_back = false,
        }
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_chan));

    
    QueueHandle_t receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));

    ESP_ERROR_CHECK(rmt_enable(rx_chan));
    
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = example_rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, receive_queue));
    
    // the following timing requirement is based on NEC protocol
    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 2000,     // the shortest duration for NEC signal is 560 µs, 1250 ns < 560 µs, valid signal is not treated as noise
        .signal_range_max_ns = 20000000, // the longest duration for NEC signal is 9000 µs, 12000000 ns > 9000 µs, the receive does not stop early
        .flags = {
            .en_partial_rx = false
        }
    };
    
    rmt_symbol_word_t raw_symbols[128]; // 64 symbols should be sufficient for a standard NEC frame
    // ready to receive
    ESP_ERROR_CHECK(rmt_receive(rx_chan, raw_symbols, sizeof(raw_symbols), &receive_config));
    // wait for the RX-done signal
    rmt_rx_done_event_data_t rx_data;
    xQueueReceive(receive_queue, &rx_data, portMAX_DELAY);
    ESP_LOGI(TAG, "Received %d symbols", rx_data.num_symbols);
    // parse the received symbols
    for (int i = 0; i < rx_data.num_symbols; i++)
    {
        uint16_t lowus = rx_data.received_symbols[i].duration0;
        uint16_t highus = rx_data.received_symbols[i].duration1;
        ESP_LOGI(TAG, "Low Duration: %d  High Duration %d", lowus, highus);
    }
    ESP_ERROR_CHECK(rmt_disable(rx_chan));
    ESP_ERROR_CHECK(rmt_del_channel(rx_chan));
    vQueueDelete(receive_queue);
}

bool VistaBus::read_packet(char * data, int &len, int &type, bool with_delay) 
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
        ESP_ERROR_CHECK(uart_driver_install(u_n, RX_BUF_SIZE + 8, 0, 20, &uartevtQueue, intr_alloc_flags));
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

static bool validChksum(const char * cbuf, int start, int len)
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


static int get_Packet(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, int len, uart_port_t uart_num, int timeout)
{
    const int rxBytes = uart_read_bytes(uart_num, rxbuf, len, timeout);
    memcpy(received_packet->payload+start,rxbuf,rxBytes);
    received_packet->payload[rxBytes+start] = '\0';
    received_packet->size = rxBytes+start;
    return rxBytes;
}


static int uart_read_bytes_event(uart_port_t uart_num, uint8_t * rxbuf, int len, int timeout, QueueHandle_t queue)
{
    uart_event_t event;
    int bytes = 0;
    while (bytes < len)
    {
        if (!(xQueueReceive(queue, (void *)&event, timeout) == pdPASS))
            break;
        switch (event.type)
        {
            case UART_DATA:
                uart_read_bytes(uart_num, &rxbuf[bytes], 1, 0);
                bytes++;
                break;
            case UART_BREAK:
                break;
            default:
                break;
        }
    }
    return bytes;
}


static int get_Packet_event(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, int len, uart_port_t uart_num, int timeout, QueueHandle_t queue)
{
    const int rxBytes = uart_read_bytes_event(uart_num, rxbuf, len, timeout, queue);
    memcpy(received_packet->payload+start,rxbuf,rxBytes);
    received_packet->payload[rxBytes+start] = '\0';
    received_packet->size = rxBytes+start;
    return rxBytes;
}


void IRAM_ATTR VistaBus::gpio_isr_handler(void * args)
{
    gpioTaskArgs * taskargs = (gpioTaskArgs *) args; 
    BaseType_t xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;
    int val = gpio_get_level(static_cast<gpio_num_t>(taskargs->pin));
    xTaskNotifyFromISR(taskargs->task_handle,val, eSetValueWithOverwrite,&xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
} 


bool VistaBus::mark_pulse(uint8_t address)
{
    uart_set_parity(static_cast<uart_port_t>(this->uartNum),UART_PARITY_DISABLE);
    char snd_data[3];
    bool sent_request = false;
    if (address < 8)
    {
        snd_data[0] = ~(0x01 << (address & 0x07));
        snd_data[1] = 0;
        snd_data[2] = 0;
    }
    else if (address < 17)
    {
        snd_data[0] = 0xFF;
        snd_data[1] = ~(0x01 << (address & 0x07));
        snd_data[2] = 0;
    }
    else
    {
        snd_data[0] = 0xFF;
        snd_data[1] = 0xFF;
        snd_data[2] = ~(0x01 << (address & 0x07));
    }
    //Use GPIO interrupts on rxPin to find send pulses.

    xTaskNotifyWait(0xFFFFFFFF,0,NULL,pdMS_TO_TICKS(5)); //first rising edge
    uart_write_bytes(static_cast<uart_port_t>(this->uartNum), &snd_data[0], 1); 

    xTaskNotifyWait(0xFFFFFFFF,0,NULL,pdMS_TO_TICKS(5)); //second rising edge
    if (snd_data[1] != 0)
        uart_write_bytes(static_cast<uart_port_t>(this->uartNum), &snd_data[1], 1);
            
    xTaskNotifyWait(0xFFFFFFFF,0,NULL,pdMS_TO_TICKS(5)); //third rising edge           
    if (snd_data[2] != 0)
        uart_write_bytes(static_cast<uart_port_t>(this->uartNum), &snd_data[2], 1);
    sent_request = true;

    uart_set_parity(static_cast<uart_port_t>(this->uartNum),UART_PARITY_EVEN);
    return sent_request;
}

void VistaBus::rx_tx_task(void * args)
{
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);
    struct ReceivedPacket received_packet;
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
    bool req_to_send = false;
    uint64_t last_data_received = 0;
    uint64_t last_pattern_measure_time = 0;
    SendPacket pkt_to_send;
    uint8_t ack_failures = 0;
    uint8_t mark_failures = 0;
    int sequence = 0;
    bool pulse_marked = false;
    
    uint64_t pulse_mark_time = 0;
    while (1) 
    {
        int uart_delay = 20;
        if(this->stop_requested && monitor_rx_task_Handle == NULL)
        {
            this->panel_connected = false;
            break;
        }
        uint64_t now = esp_timer_get_time();
        if (now - last_data_received > 30*1000*1000)
            this->panel_connected = false;

        if (now - last_pattern_measure_time > 60*1000*1000)
        {
            ESP_LOGE(TAG,"Collecting pulse pattern at %llu", now);
            capture_pulse_pattern(static_cast<gpio_num_t>(this->rxPin));
            vTaskDelay(10);
            continue;
            //last_pattern_measure_time = esp_timer_get_time();
        }
            
        
        while (uxQueueMessagesWaiting(sendQueue))
        {
            if (!req_to_send)
            {
                xQueueReceive(sendQueue,&pkt_to_send,0); //something in queue. Pop it out.
                req_to_send = true;
            }
            else
            {
                SendPacket next_pkt;
                xQueuePeek(sendQueue, &next_pkt,pdMS_TO_TICKS(20));
                if(next_pkt.keypadaddress == pkt_to_send.keypadaddress && (next_pkt.size + pkt_to_send.size) <= 24)
                {
                    xQueueReceive(sendQueue, &next_pkt, 0);
                    memcpy(pkt_to_send.payload + pkt_to_send.size, next_pkt.payload, next_pkt.size);
                    pkt_to_send.size += next_pkt.size;
                }
                else
                {
                    break;
                }
            }
        }

        int rxBytes = 0;
        uart_event_t event;
        xQueueReceive(uartevtQueue, (void *)&event, uart_delay);
        switch (event.type)
        {
            case UART_DATA:
                rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->uartNum), data, 1, 0);
                break;
            case UART_BREAK:
                gpioTaskArgs taskargs;
                taskargs.task_handle = this->rx_tx_task_Handle;
                taskargs.pin = this->rxPin;
                gpio_set_intr_type(static_cast<gpio_num_t>(this->rxPin), GPIO_INTR_NEGEDGE);
                gpio_isr_handler_add(static_cast<gpio_num_t>(this->rxPin), gpio_isr_handler, (void *) &taskargs );
                if (xTaskNotifyWait(0xFFFFFFFF,0,NULL,pdMS_TO_TICKS(310)) == pdPASS)
                {
                    uint64_t start = esp_timer_get_time();
                    gpio_set_intr_type(static_cast<gpio_num_t>(this->rxPin), GPIO_INTR_POSEDGE);
                    if (xTaskNotifyWait(0xFFFFFFFF,0,NULL,pdMS_TO_TICKS(10)) == pdPASS)
                    {
                        uint64_t end = esp_timer_get_time();
                        if (end - start > 5700 && end - start < 6300)
                        {
                            uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data, 1, pdMS_TO_TICKS(4), uartevtQueue); //flush leading zero
                            uart_set_baudrate(static_cast<uart_port_t>(this->uartNum),2400);
                            rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data, 1, pdMS_TO_TICKS(10), uartevtQueue);
                            uart_set_baudrate(static_cast<uart_port_t>(this->uartNum),4800);
                        }
                    }
                    else if (req_to_send && !pulse_marked)
                    {
                        pulse_marked = mark_pulse(pkt_to_send.keypadaddress);
                        pulse_mark_time = esp_timer_get_time();
                        rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data, 1, pdMS_TO_TICKS(10), uartevtQueue);
                    }
                }
                gpio_isr_handler_remove(static_cast<gpio_num_t>(this->rxPin));
                break;
            default:
                break;
        }
        if (req_to_send)
            uart_delay = 20;
        if (rxBytes > 0) 
        {
            this->panel_connected = true;
            last_data_received = esp_timer_get_time();
            data[rxBytes] = 0;
            memset(received_packet.payload,'\0',sizeof(received_packet.payload));
            received_packet.payload[0] = data[0];
            if (req_to_send && pulse_marked && pkt_to_send.type == 2)
            {
                req_to_send = false;
            }
            if ( data[0] == 0xF6) //SEND ACK Received
            {                 
                rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data, 1, pdMS_TO_TICKS(UART_DELAY), uartevtQueue); //Get Address
                if(data[0] != 0)
                {
                    uint32_t val = 0xF6 << 8 | data[0];
                    xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                }
                received_packet.payload[1] = data[0];
                received_packet.size = 2;
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(20));
                uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), &data[1], 1, pdMS_TO_TICKS(UART_DELAY), uartevtQueue); //flush lagging zero
                if(req_to_send && data[0] == pkt_to_send.keypadaddress) //ACK was for us.  Try to send.
                { 
                    char outbuffer[24];
                    memset(outbuffer,'\0',sizeof(outbuffer));
                    data[rxBytes] = 0;
                    char keys_to_send[24];
                    outbuffer[0] = (((++sequence<<6) & 0xc0) ^ 0xc0) | (pkt_to_send.keypadaddress & 0x3F);
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

                    rxBytes = get_Packet_event(&received_packet,data,0,2, static_cast<uart_port_t>(this->uartNum), pdMS_TO_TICKS(50), uartevtQueue);
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

                }
            }
            else if ( data[0] == 0xF7 ) //DISPLAY
            {
                get_Packet_event(&received_packet,data,1,F7_MESSAGE_LENGTH-1, static_cast<uart_port_t>(this->uartNum), pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                if(validChksum(received_packet.payload,0,45))
                {
                    xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(20));
                }
            }
            else if ( data[0] == 0xF2 ) //AUI
            {
                rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data, 1, pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                received_packet.payload[1] = data[0];
                get_Packet_event(&received_packet,data,2,static_cast<int> (received_packet.payload[1]),static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                if (validChksum(received_packet.payload,0,static_cast<int>(received_packet.payload[1])+2)) 
                {
                    xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
                }
            }
            else if ( data[0] == 0xFA ) //EXP
            {                    
                if (req_to_send && pulse_marked && pkt_to_send.type == 2)
                {
                    req_to_send = false;
                }     
                get_Packet_event(&received_packet,data,1,FA_MESSAGE_LENGTH-2,static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                if (EXPemulation)
                    this->processFA(received_packet.payload);
                get_Packet_event(&received_packet,data,5,1,static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));     
            }
            else if ( data[0] == 0xF9 ) //LRR
            {   
                char response[6];
                rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data, 2, pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                received_packet.payload[1] = data[0];
                received_packet.payload[2] = data[1];
                get_Packet_event(&received_packet,data,3,static_cast<int> (received_packet.payload[2]),static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                
                if (received_packet.payload[3] == 0x53)
                {
                    uint32_t val = 0xF9 << 8 | (received_packet.payload[1] + 0x40);
                    xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                    if (LRRemulation)
                    {
                        response[0] = received_packet.payload[1] + 0x40;
                        response[1] = 0x04;
                        response[2] = 0;
                        response[3] = 0;
                        response[4] = 0;
                        response[5] = (((0x0F - (response[0] >> 4)) & 0x0F) << 4) | 0x09;
                        uart_write_bytes(static_cast<uart_port_t>(this->uartNum),response, 6);
                    }
                }
                else if (LRRemulation && (received_packet.payload[3] == 0x48 || received_packet.payload[3] == 0x52 || received_packet.payload[3] == 0x58))
                {
                    uart_write_bytes(static_cast<uart_port_t>(this->uartNum),&received_packet.payload[1], 1);
                }
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
                vTaskDelay(pdMS_TO_TICKS(25)); //Delay to put command/response/ack in sequence in log
            }
            else if ( data[0] == 0xFB ) //5881EN traffic on Vista 20p (address 0)??
            {   
                uint32_t val = 0xFB << 8 | (received_packet.payload[3]);
                xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                get_Packet_event(&received_packet,data,1,4,static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
            }
            else if ( data[0] == 0xF8 ) //Unknown Command
            {
                rxBytes = uart_read_bytes_event(static_cast<uart_port_t>(this->uartNum), data, 2, pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
                received_packet.payload[1] = data[0];
                received_packet.payload[2] = data[1];
                get_Packet_event(&received_packet,data,3,static_cast<int> (received_packet.payload[2]),static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY), uartevtQueue);
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

        if (req_to_send && pulse_marked)
        {
            ack_failures++;
            mark_failures = 0;
            pulse_marked = false;
        }
        else if (req_to_send && !pulse_marked)
            mark_failures++;

        if (ack_failures == 3)
        {
            ESP_LOGW(TAG, "Failure to receive F6 ACK after 3 successive pulse marks.  Giving up.");
            req_to_send = false;
            ack_failures = 0;
            mark_failures = 0;
        }
        if (mark_failures == 67) //1340 ms total / 20 ms task frequency
        {
            ESP_LOGW(TAG, "Failure to mark pulse after 5 cycles.  Giving up.");
            req_to_send = false;
            ack_failures = 0;
            mark_failures =0;
        }
        if (!req_to_send)
        {
            ack_failures = 0;
            mark_failures = 0;
            pulse_marked = false;
        }
    }
    free(data);
    ESP_LOGI(TAG, "Stopping Task");
    this->rx_tx_task_Handle = NULL;
    vTaskDelete(NULL);
}

void VistaBus::monitor_rx_task(void * args)
{  
    uint8_t* data = (uint8_t*) malloc(128);
    struct ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    uint32_t val = 0;

    while (1) 
    {
        if(this->stop_requested)
        {
            break;
        }
        int rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->extuartNum), data, 1, portMAX_DELAY);
        if (val == 0)
            xTaskNotifyWait(0xFFFFFFFF,0,&val,0);  //data of interest incoming according to RX_TX Task
        if (rxBytes > 0) 
        {
            data[rxBytes] = 0;
            memset(rcvd_extPkt.payload,'\0',sizeof(rcvd_extPkt.payload));
            rcvd_extPkt.payload[0] = data[0];
            if (data[0]==0xFE) //Send known packets immediately
            {
                int res = get_Packet(&rcvd_extPkt, data, 1, FE_EXT_MESSAGE_LENGTH-1, static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(150)); //do not set delay to less than 125ms
                if (res > 0)
                    xQueueSend(this->receiveQueue,&rcvd_extPkt,pdMS_TO_TICKS(20));
            }
            else if(val >> 8 == 0xF6) //next byte will be header of sending sequence
            {
                rcvd_extPkt.payload[0]=data[0];
                rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->extuartNum), data, 1, pdMS_TO_TICKS(150));
                rcvd_extPkt.payload[1] = data[0]; //length
                get_Packet(&rcvd_extPkt, data, 2, rcvd_extPkt.payload[1], static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(150));
                xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                val = 0;

            }
            else if(val >> 8 == 0xF9 && (val & 0x0F) == 0x03) //expect response
            {
                get_Packet(&rcvd_extPkt, data, 1, 5, static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(UART_DELAY));
                xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                val = 0;

            }
            else if(val >> 8 == 0xFB && rcvd_extPkt.payload[0] == 0)  //responses to FB command
            {
                get_Packet(&rcvd_extPkt, data, 1, 3, static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(150));
                xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                val = 0;

            }
            else if (data[0] == 0xF0 || data[0] == 0x7F || data[0]==0xFB || data[0] == 0xFD || data[0] == 0xF7) //expanders such as 4219 7F=07,FE=08, FD=09, FB=10, F7=11
            {
                int res = get_Packet(&rcvd_extPkt, data, 1, 5, static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(150)); //do not set delay to less than 125ms
                if (res > 0)
                    xQueueSend(this->receiveQueue,&rcvd_extPkt,pdMS_TO_TICKS(20));
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
    free(data);
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
    auto it = std::find_if(emulated_expanders.begin(), emulated_expanders.end(), [address](emulatedExpander &f)
        { return f.address == address; });
    if (it != emulated_expanders.end())
        return &(*it);
    return NULL;
}

void VistaBus::processFA(const char * cbuf)
{
    char type = cbuf[4];
    // we use zone to either | or & bits depending if in fault or reset
    // 0xF1 - response to request, 0xf7 - poll, 0x80 - retry
    esp_timer_handle_t oneshot_timer;
    const esp_timer_create_args_t oneshot_timer_args = 
    {
        .callback = &precise_delay,
        .arg = (void *) this->rx_tx_task_Handle,
        .dispatch_method = ESP_TIMER_ISR,
        .name = "precise_delay_timer",
        .skip_unhandled_events = false
    };
    esp_timer_create(&oneshot_timer_args, &oneshot_timer);
    esp_timer_start_once(oneshot_timer, 2500);
    xTaskNotifyWait(0xFFFFFFFF,0,NULL,portMAX_DELAY);
    if (type == 0xF1)
    {   
        char seq = cbuf[3];
        char lcbuf[5];
        int lcbuflen = 5;
        bool valid_address = false;
        uint8_t expSeq = (seq == 0x20 ? 0x34 : 0x31);
        uint8_t address = 0;
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
            if (expander != NULL && expander->pending_update.zone != 0)
            {
                char header[1];
                lcbuf[0] = address;
                lcbuf[1] = expSeq;
                uint8_t z = expander->pending_update.zone & 0x07;
                lcbuf[2] = z ? 0 : 0x01;
                uint8_t chksum = 0;
                lcbuf[3] = (z << 5) ^ (0x10*expander->pending_update.fault); // we send out the current zone state
                for (int x = 0; x < lcbuflen-1; x++)
                {
                    chksum += lcbuf[x];
                }
                chksum = ~chksum + 1;
                lcbuf[lcbuflen-1] = chksum;
                uart_write_bytes(static_cast<uart_port_t>(this->uartNum),lcbuf, lcbuflen);
                expander->pending_update.zone = 0;
                expander->pending_update.fault = false;
            }
        }
    }
    else if (type == 0xF7)
    { // periodic zone state poll (every 30 seconds) expander
        char seq = cbuf[3];
        char lcbuf[5];
        bool valid_address = false;
        
        uint8_t address = 0;
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
            if (expander != NULL)
            {
                uint8_t expSeq = (seq == 0x20 ? 0x34 : 0x31);
                uint8_t lcbuflen = 0;
                lcbuflen = 4;       
                lcbuf[0] = 0xF0;
                lcbuf[1] = expSeq;
                lcbuf[2] = expander->fault_NO_Bits;                      
                lcbuf[3] = expander->fault_NC_Bits; 
                uint8_t chksum = 0;
                for (int x = 0; x < lcbuflen; x++)
                {
                    chksum += lcbuf[x];
                }
                lcbuflen ++;
                chksum = ~chksum + 1;
                lcbuf[lcbuflen-1] = chksum;
                uart_write_bytes(static_cast<uart_port_t>(this->uartNum),lcbuf, lcbuflen);
            }
        }
    }
    esp_timer_delete(oneshot_timer);
}

void IRAM_ATTR VistaBus::precise_delay(void * args)
{
    TaskHandle_t task_handle = (TaskHandle_t) args;
    BaseType_t xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(task_handle,0, eNoAction, &xHigherPriorityTaskWoken);
    esp_timer_isr_dispatch_need_yield();
}


void VistaBus::setExpFaultBits(uint8_t zone, bool fault)
{
    uint8_t address = getExpanderAddress(zone);
    emulatedExpander *expander = getExpander(address);
    if (expander != NULL)
    {
        uint8_t expSeq;
        uint8_t lcbuflen = 5;
        char lcbuf[5];
        char header[1];
        uint8_t z = zone & 0x07;
        lcbuf[2] = z ? 0 : 0x01;
        expander->fault_NO_Bits = fault ? expander->fault_NO_Bits | (0x01 << (8-z)) : expander->fault_NO_Bits & ~(0x01 << (8-z));
        expander->pending_update.zone = zone;
        expander->pending_update.fault = fault;
        //Nudge panel to send F1 request
        SendPacket pkt;
        pkt.type = 2;
        pkt.keypadaddress = address;
        pkt.payload[0] = 0;
        pkt.size = 0;
        xQueueSend(sendQueue,&pkt,0);
    }
}

void VistaBus::setExpTamper(int32_t zone, bool tamper_active)
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