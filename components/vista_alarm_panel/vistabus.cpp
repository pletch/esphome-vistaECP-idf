#include "vistabus.h"
#include "esp_log.h"


VistaBus::VistaBus()
{
    this->receiveQueue = xQueueCreate(20,sizeof(ReceivedPacket)); 
    this->sendQueue = xQueueCreate(6, sizeof(SendPacket));
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
        ESP_LOGE("VistaBus", "Memory for task queues was not allocated. Aborting!");
        return;
    }

    init_uart(static_cast<uart_port_t>(this->uartNum),static_cast<gpio_num_t>(this->rxPin), static_cast<gpio_num_t>(this->txPin));
    if (extuartNum > 0) 
    {
        init_uart(static_cast<uart_port_t>(this->extuartNum),static_cast<gpio_num_t>(this->monitorPin), static_cast<gpio_num_t>(-1));
    }

    //xTaskCreate(uart_evt_task_start, "uart_evt_task", UART_EVT_TASK_STACK_SIZE, (void *) this, configMAX_PRIORITIES-12, &this->uart_evt_task_Handle);
    xTaskCreate(rx_tx_task_start, "uart_rx_tx_task", UART_RX_TASK_STACK_SIZE, (void *) this, configMAX_PRIORITIES-9, &this->rx_tx_task_Handle);
    if (monitorPin != -1)
    {
        xTaskCreate(monitor_rx_task_start, "uart_monitor_rx_task", UART_RX_EXT_TASK_STACK_SIZE, (void *) this, configMAX_PRIORITIES-10, &this->monitor_rx_task_Handle);
    }
}

bool VistaBus::stop() 
{
    this->stop_requested = true;

    //vTaskDelete(this->uart_evt_task_Handle);

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
    bool result = xQueueSend(sendQueue,&sendpkt,0) == pdPASS;
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
        .stop_bits = STOP_BIT_SETTING,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {} 
    };

    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    //if (static_cast<int>(tx_pin) == -1)
    //{      
        ESP_ERROR_CHECK(uart_driver_install(u_n, RX_BUF_SIZE + 8, 0, 0, NULL, intr_alloc_flags));
    //}
    //else
    //{
    //    ESP_ERROR_CHECK(uart_driver_install(u_n, RX_BUF_SIZE + 8, 0, 5, &uartevtQueue, intr_alloc_flags));
    //}
    ESP_ERROR_CHECK(uart_param_config(u_n, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(u_n, tx_pin, rx_pin, -1, -1));
    ESP_ERROR_CHECK(uart_set_rx_timeout(u_n, 4));
    ESP_ERROR_CHECK(uart_set_tx_empty_threshold(u_n,5));
    if (static_cast<int>(tx_pin) == -1) 
    {
        ESP_ERROR_CHECK(uart_set_rx_full_threshold(u_n, 6));
        ESP_ERROR_CHECK(uart_set_line_inverse(u_n, UART_SIGNAL_RXD_INV));
        
    } 
    else 
    {
        ESP_ERROR_CHECK(uart_set_rx_full_threshold(u_n, 2));
        ESP_ERROR_CHECK(uart_set_line_inverse(u_n, UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV));
        //ESP_ERROR_CHECK(uart_enable_intr_mask(u_n, UART_INTR_BRK_DET | UART_INTR_FRAM_ERR | UART_INTR_PARITY_ERR));
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

/*static void emit_Packet(const char * cbuf, int len, const char * tag)  
{
    char s[256];
    memset(s,'\0',sizeof(s));
    for (int i = 0; i < len-1; i++) 
    {
        char st[3];
        sprintf(st,"%02x",static_cast<unsigned char>(cbuf[i]));
        strcat(s," ");
        strcat(s,st);
    }
    time_t now = time(0);
    tm* timeinfo = localtime(&now);
    char strftime_buf[64];
    localtime_r(&now, timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%T", timeinfo);
    ESP_LOGI("", "Timestamp: %s  Packet: %s",strftime_buf,s);    
}*/

static int get_Packet(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, int len, uart_port_t uart_num, int timeout)
{
        const int rxBytes = uart_read_bytes(uart_num, rxbuf, len, timeout);
        memcpy(received_packet->payload+start,rxbuf,rxBytes);
        received_packet->payload[rxBytes+start] = '\0';
        received_packet->size = rxBytes+start;
        return rxBytes;
}


void IRAM_ATTR VistaBus::gpio_isr_handler(void * args)
{
    TaskHandle_t task_handle = (TaskHandle_t) args;
    int val = gpio_get_level(GPIO_NUM_18);
    xTaskNotifyFromISR(task_handle,val, eSetValueWithOverwrite,0);

} 

void VistaBus::rx_tx_task(void * args)
{
    static const char *TASK_TAG = "[VISTABUS]RX_TX";
    esp_log_level_set(TASK_TAG, ESP_LOG_INFO);
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
    SendPacket pkt_to_send;
    int send_retries = 0;
    int sequence = 0;
    char tempbuff[13];
    int tempbuff_fill = 0;
    int cksum = 0;
    while (1) 
    {
        if(this->stop_requested && monitor_rx_task_Handle == NULL)
        {
            this->panel_connected = false;
            break;
        }
        uint64_t now = esp_timer_get_time();
        if (now - last_data_received > 30*1000*1000)
        {
            this->panel_connected = false;
        }
        if (!req_to_send) 
        {
            req_to_send = xQueueReceive(this->sendQueue,&pkt_to_send,0) == pdPASS;
        }
        if (req_to_send) 
        {
            char snd_data[3];
            if (pkt_to_send.keypadaddress < 8)
            {
                snd_data[0] = 0xFF ^ (0x01 << (pkt_to_send.keypadaddress));
                snd_data[1] = 0;
                snd_data[2] = 0;
            }
            else if (pkt_to_send.keypadaddress < 17)
            {
                snd_data[0] = 0xFF;
                snd_data[1] = 0xFF ^ (0x01 << (pkt_to_send.keypadaddress - 8));
                snd_data[2] = 0;
            }
            else
            {
                snd_data[0] = 0xFF;
                snd_data[1] = 0xFF;
                snd_data[2] =  0xFF^(0x01 << (pkt_to_send.keypadaddress - 16));
            }
            //Use GPIO on rxPin to find pulse signal
            gpio_set_intr_type(static_cast<gpio_num_t>(this->rxPin), GPIO_INTR_ANYEDGE);
            gpio_isr_handler_add(static_cast<gpio_num_t>(this->rxPin), gpio_isr_handler, (void *) this->rx_tx_task_Handle);
            uint32_t result = 1;
            while (result) 
            { 
                xTaskNotifyWait(0xFFFFFFFF,0,&result,pdMS_TO_TICKS(400));   //find first falling edge. Pulsing freq seems to be ~ 330 ms.  
                                                                            //'result' is the pin value and needs to be low to proceed.
            } 
            gpio_set_intr_type(static_cast<gpio_num_t>(this->rxPin),GPIO_INTR_POSEDGE);
            bool notified = (xTaskNotifyWait(0xFFFFFFFF,0,&result,pdMS_TO_TICKS(11)) == pdTRUE); //confirm still low after 11ms by waiting for timeout
            if (!notified) 
            {
                xTaskNotifyWait(0xFFFFFFFF,0,&result,pdMS_TO_TICKS(7)); //first rising edge           
                uart_write_bytes(static_cast<uart_port_t>(this->uartNum), &snd_data[0], 1);
                
                xTaskNotifyWait(0xFFFFFFFF,0,&result,pdMS_TO_TICKS(7)); //second rising edge              
                if (snd_data[1] != 0)
                    uart_write_bytes(static_cast<uart_port_t>(this->uartNum), &snd_data[1], 1);

                xTaskNotifyWait(0xFFFFFFFF,0,&result,pdMS_TO_TICKS(7)); //third rising edge           
                if (snd_data[2] != 0)
                    uart_write_bytes(static_cast<uart_port_t>(this->uartNum), &snd_data[2], 1);
            }
            gpio_isr_handler_remove(static_cast<gpio_num_t>(this->rxPin));
        }
        int rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->uartNum), data, 1, pdMS_TO_TICKS(250)); 
        if (rxBytes > 0) 
        {
            this->panel_connected = true;
            last_data_received = esp_timer_get_time();
            data[rxBytes] = 0;
            memset(received_packet.payload,'\0',sizeof(received_packet.payload));
            received_packet.payload[0] = data[0];
            if ( data[0] == 0xF6) //SEND ACK Received
            {                 
                rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->uartNum), data, 1, pdMS_TO_TICKS(UART_DELAY)); //Get Address
                if(data[0] != 0)
                {
                    uint32_t val = 0xF6 << 8 | data[0];
                    xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                }
                if(req_to_send && data[0] == pkt_to_send.keypadaddress) //ACK was for us.  Try to send.
                { 
                    char outbuffer[24];
                    memset(outbuffer,'\0',sizeof(outbuffer));
                    data[rxBytes] = 0;
                    char keys_to_send[24];
                    outbuffer[0] = (((++sequence<<6) & 0xc0) ^ 0xc0) | (pkt_to_send.keypadaddress & 0x3F);
                    outbuffer[1] = pkt_to_send.size+1;
                    int checksum = 0;
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
                                outbuffer[i] = (pkt_to_send.payload[i-2] = 0x0B);
                            else if (pkt_to_send.payload[i-2] == 0x2A)
                                outbuffer[i] = (pkt_to_send.payload[i-2] = 0x0A);
                            else if (pkt_to_send.payload[i-2] == 0x46)
                                outbuffer[i] = (pkt_to_send.payload[i-2] = 0x0C);
                            else if (pkt_to_send.payload[i-2] == 0x4D)
                                outbuffer[i] = (pkt_to_send.payload[i-2] = 0x0D);
                            else if (pkt_to_send.payload[i-2] == 0x50)
                                outbuffer[i] = (pkt_to_send.payload[i-2] = 0x0E);
                            else if (pkt_to_send.payload[i-2] == 0x47)
                                outbuffer[i] = (pkt_to_send.payload[i-2] = 0x0F);
                            else if (pkt_to_send.payload[i-2] >= 0x41 && pkt_to_send.payload[i-2] <= 0x44)
                                outbuffer[i] = (pkt_to_send.payload[i-2] - 0x25);
                        }
                        checksum += outbuffer[i];
                    }
                    outbuffer[pkt_to_send.size+2] = (0x100 - outbuffer[0] - (pkt_to_send.size + 1) - checksum ) & 0xff;
                    uart_write_bytes(static_cast<uart_port_t>(this->uartNum), outbuffer,pkt_to_send.size+3);

                    send_retries++;
                    get_Packet(&received_packet,data, 0, 2, static_cast<uart_port_t>(this->uartNum), pdMS_TO_TICKS(150)); 

                    if(received_packet.payload[1] == outbuffer[0]) 
                    {
                        req_to_send = false;
                        send_retries = 0;
                    }
                    else if (send_retries == 3)
                    {
                        req_to_send = false;
                        send_retries = 0;
                    };  

                } 
                else //ACK was for another device.
                {        

                }
            }
            else if ( data[0] == 0xF7) //DISPLAY
            {
                get_Packet(&received_packet,data,1,F7_MESSAGE_LENGTH-1, static_cast<uart_port_t>(this->uartNum), pdMS_TO_TICKS(UART_DELAY));
                if(validChksum(received_packet.payload,0,45))
                {
                    xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(20));
                }
            }
            else if ( data[0] == 0xF2) //AUI
            {
                rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->uartNum), data, 1, pdMS_TO_TICKS(UART_DELAY));
                received_packet.payload[1] = data[0];
                get_Packet(&received_packet,data,2,static_cast<int> (received_packet.payload[1]),static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY));
                if (validChksum(received_packet.payload,0,static_cast<int>(received_packet.payload[1])+2)) 
                {
                    xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
                }
            }
            else if ( data[0] == 0x98 ) //EXP
            {                    
                rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->uartNum), data, 4, pdMS_TO_TICKS(UART_DELAY));
                if (rxBytes != 4)
                    continue;
                for (int i=1; i<5; i++) 
                {
                    received_packet.payload[i] = data[i-1]; // 01? / dev id or len code if &1 / seq / type
                }
                if (received_packet.payload[2] & 1) // byte 2 = 01 if extended addressing for relay boards 14,15 so packet longer by 1 byte
                {
                    uart_read_bytes(static_cast<uart_port_t>(this->uartNum), data, 1, pdMS_TO_TICKS(UART_DELAY)); //extra byte
                    received_packet.payload[5] = data[0];
                    received_packet.size = 7;
                }
                else if (received_packet.payload[4] == 0x00 || received_packet.payload[4] == 0x0D) // 00 cmds use an extra byte
                {
                    uart_read_bytes(static_cast<uart_port_t>(this->uartNum), data, 1, pdMS_TO_TICKS(UART_DELAY)); 
                    received_packet.payload[5] = data[0];
                    received_packet.size = 7;
                }
                else
                {
                    received_packet.size = 6;
                }
                uart_read_bytes(static_cast<uart_port_t>(this->uartNum), data, 1, pdMS_TO_TICKS(UART_DELAY)); //checksum
                received_packet.payload[received_packet.size - 1] = data[0];
                if (EXPemulation)
                    this->process98(received_packet.payload);
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));          
            }
            else if ( data[0] == 0xF9 ) //LRR
            {   
                char response[6];
                rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->uartNum), data, 2, pdMS_TO_TICKS(UART_DELAY));
                received_packet.payload[1] = data[0];
                received_packet.payload[2] = data[1];
                get_Packet(&received_packet,data,3,static_cast<int> (received_packet.payload[2]),static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY));
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
            }
            else if ( data[0] == 0x9E ) //5881EN traffic on Vista 20p (address 0)??
            {   
                get_Packet(&received_packet,data,1,3,static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY));
                uint32_t val = 0x9E << 8 | (received_packet.payload[3]);
                xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
            }
            else if ( data[0] == 0xF8 ) //Unknown Command
            {
                get_Packet(&received_packet,data,1,7,static_cast<uart_port_t>(this->uartNum),pdMS_TO_TICKS(UART_DELAY));
                xQueueSend(this->receiveQueue,&received_packet,pdMS_TO_TICKS(0));
            }
            /*else if ( data[0] == 0x00) 
            {
            }*/
            else
            {
                if (tempbuff_fill == 0 && data[0] == 0) 
                {
                    //don't accumulate leading zeros
                }
                else
                {
                    tempbuff[tempbuff_fill] = data[0];
                    tempbuff_fill++;
                    cksum += data[0];
                }
            
                if (tempbuff_fill == 4) 
                {
                    if (tempbuff[0] != cksum) //don't clutter queue with 1 byte sequences
                    {
                        memcpy(received_packet.payload, tempbuff,tempbuff_fill);
                        received_packet.size = tempbuff_fill;
                        xQueueSend(this->receiveQueue, &received_packet,pdMS_TO_TICKS(20));
                    }
                    tempbuff_fill = 0;
                    cksum = 0;
                }
            }
        }
    }
    free(data);
    ESP_LOGI(TASK_TAG, "Stopping Task");
    this->rx_tx_task_Handle = NULL;
    vTaskDelete(NULL);
}

void VistaBus::monitor_rx_task(void * args)
{  
    static const char *TASK_TAG = "[VISTABUS]MONITOR_RX";
    esp_log_level_set(TASK_TAG, ESP_LOG_INFO);
    uint8_t* data = (uint8_t*) malloc(128);
    struct ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    uint32_t val = 0;
    char tempbuff[13];
    int tempbuff_fill = 0;
    int cksum = 0;

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
                //memset(tempbuff,'\0', sizeof(tempbuff));
                //tempbuff_fill = 0;
                //emit_Packet(rcvd_extPkt.payload,rcvd_extPkt.size,TASK_TAG);
            }
            else if(val >> 8 == 0xF6) //next byte will be header of sending sequence
            {
                rcvd_extPkt.payload[0]=0xF6;
                rcvd_extPkt.payload[1]=data[0] & (val & 0xFF);
                rcvd_extPkt.payload[2]=data[0];
                rxBytes = uart_read_bytes(static_cast<uart_port_t>(this->extuartNum), data, 1, pdMS_TO_TICKS(125));
                rcvd_extPkt.payload[3] = data[0]; //length
                get_Packet(&rcvd_extPkt, data, 4, rcvd_extPkt.payload[3], static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(150));
                xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                val = 0;
                //memset(tempbuff,'\0', sizeof(tempbuff));
                //tempbuff_fill = 0;
            }
            else if(val >> 8 == 0xF9 && (val & 0x0F) == 0x03) //expect response
            {
                get_Packet(&rcvd_extPkt, data, 1, 6, static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(150));
                xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                val = 0;
                //memset(tempbuff,'\0', sizeof(tempbuff));
                //tempbuff_fill = 0;
            }
            else if(val >> 8 == 0x9E && (rcvd_extPkt.payload[0] == 0x21 || rcvd_extPkt.payload[0] == 0x24))  //responses to 9E command
            {
                get_Packet(&rcvd_extPkt, data, 1, 2, static_cast<uart_port_t>(this->extuartNum), pdMS_TO_TICKS(150));
                xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                val = 0;
               //memset(tempbuff,'\0', sizeof(tempbuff));
                //tempbuff_fill = 0;
            }
            else if (val == 0) //put byte in temp buffer to emit to log
            {
                if (tempbuff_fill == 0 && data[0] == 0) 
                {
                    //don't accumulate leading zeros
                }
                else
                {
                    tempbuff[tempbuff_fill] = data[0];
                    tempbuff_fill++;
                }
            }
            if (tempbuff_fill == 4)  //don't clutter queue with 1 byte sequences
            {
                if (tempbuff[0] != cksum)
                {
                    memcpy(rcvd_extPkt.payload, tempbuff,tempbuff_fill);
                    rcvd_extPkt.size = tempbuff_fill;
                    xQueueSend(this->receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(20));
                }
                tempbuff_fill = 0;
                cksum = 0;
            }
        }
    }
    free(data);
    ESP_LOGI(TASK_TAG, "Stopping Task");
    this->monitor_rx_task_Handle = NULL;
    vTaskDelete(NULL);
}

void VistaBus::process98(const char * cbuf)
{
    char type = cbuf[4];
    char seq = cbuf[3];
    char lcbuf[8];

    int idx;

    if (cbuf[2] & 1)
    {
        seq = cbuf[4];
        type = cbuf[5];
        for (idx = 0; idx < 9; idx++)
        {
            if (!expander[idx].address)
                continue;
            if (cbuf[2] == (0x01 << (expander[idx].address - 13)))
                break; // for us - relay addresses 14-15
        }
    }
    else
    {
        for (idx = 0; idx < 9; idx++)
        {
            if (!expander[idx].address)
                continue;
            if (cbuf[2] == (0x01 << (expander[idx].address - 6)))
                break; // for us - address range 7 -13
        }
    }
    if (idx == 9)
    {
        return; // no match return
    }

    uint8_t lcbuflen = 0;
    uint8_t expSeq = (seq == 0x20 ? 0x31 : 0x34);

    // we use zone to either | or & bits depending if in fault or reset
    // 0xF1 - response to request, 0xf7 - poll, 0x80 - retry ,0x00 relay control
    if (type == 0xF1)
    {
        lcbuflen = 4;
        lcbuf[0] = expander[idx].address;
        lcbuf[1] = expSeq;
        // lcbuf[2] = (char) currentFault.relayState;
        lcbuf[2] = 0;
        lcbuf[3] = expander[idx].fault; // we send out the current zone state
    }
    else if (type == 0xF7)
    { // periodic  zone state poll (every 30 seconds) expander
        // I think the actual sequence looks something like this but I don't have a real 4219 with which to test.  
        // A 0xFE in checksum would eliminate need to subtract 1 below.
        // The response should include device address??
        // The response sequence should also include a 0xF7??
        //lcbuflen = 5;         
        //lcbuf[0] = 0xFE;
        //lcbuf[1] = 1 << (expander[idx].address-6);
        //lcbuf[2] = expSeq;
        //lcbuf[3] = 0;
        //lcbuf[4] = expander[idx].faultBits;
        //lcbuf[5] = 0; 
        lcbuflen = 4;       //This sequence works but doesn't match what monitor line expected sequence is for parsing for so it must not be correct.
        lcbuf[0] = 0xF0;
        lcbuf[1] = expSeq;
        lcbuf[2] = 0;                       // we simulate having a termination resistor so set to zero for all zones
        lcbuf[3] = expander[idx].faultBits; // opens zones - we send out the list of zone states. if 0 in both fields, means terminated
    }
    else if (type == 0x00 || type == 0x0D)
    { // relay module
        lcbuflen = 4;
        lcbuf[0] = expander[idx].address;
        lcbuf[1] = expSeq;
        lcbuf[2] = 0x00;
        if (cbuf[2] & 1)
        { // address 14/15
            expander[idx].relayState = cbuf[6] & 0x80 ? expander[idx].relayState | (cbuf[6] & 0x7f) : expander[idx].relayState & ((cbuf[6] & 0x7f) ^ 0xFF);
            lcbuf[3] = cbuf[6];
        }
        else
        {
            expander[idx].relayState = cbuf[5] & 0x80 ? expander[idx].relayState | (cbuf[5] & 0x7f) : expander[idx].relayState & ((cbuf[5] & 0x7f) ^ 0xFF);
            lcbuf[3] = cbuf[5];
        }
    }
    else
    {
        return; // we don't acknowledge if we don't know  //0x80 or 0x81
    }
    uint8_t chksum = 0;
    for (int x = 0; x < lcbuflen; x++)
    {
        chksum += lcbuf[x];
    }
    lcbuflen ++;
    chksum -= 1;
    chksum = chksum ^ 0xFF;
    lcbuf[lcbuflen-1] = chksum;
    uart_write_bytes(static_cast<uart_port_t>(this->uartNum),lcbuf, lcbuflen);
}

void VistaBus::setExpFault(int zone, bool fault)
{
    // expander address 7 - zones: 9 - 16
    // expander address 8 - zones:  17 - 24
    // expander address 9 - zones: 25 - 32
    // expander address 10 - zones: 33 - 40
    // expander address 11 - zones: 41 - 48
    uint8_t idx = 0;
    if (zone > 8 && zone < 17)  
        idx = 0;
    else if (zone > 16 && zone < 25)
        idx = 1;
    else if (zone > 24 && zone < 33)
        idx = 2;
    else if (zone > 32 && zone < 41)
        idx = 3;
    else if (zone > 40 && zone < 49)
        idx = 4;
    else
        return;

    int z = zone % 8;                      // convert zone to range of 1 - 7,0 (last zone is 0)
    expander[idx].fault = z << 5 | (fault ? 0x8 : 0); // 0 = terminated(eol resistor), 0x08=open, 0x10 = closed (shorted)  - convert to bitfield for F1 response
    if (z > 0)
        z--;
    else
        z = 7;                                                                                   // now convert to 0 - 7 for F7 poll response
    expander[idx].faultBits = (fault ? expander[idx].faultBits | (0x80 >> z) : expander[idx].faultBits & ((0x80 >> z) ^ 0xFF)); // setup bit fields for return response with fault values for each zone
}

void VistaBus::setExpAddr(int address)
{   
    int idx = -1;
    if (address > 6 && address < 16)
    {
        idx = address-7; //valid range of addresses is 7-15.
        this->EXPemulation = true;
    }
    else
        return;
    this->expander[idx].address = address;
}
/*void VistaBus::uart_evt_task(void * args) //Task for monitoring for UART break/framing error/parity events from TX/RX UART.
{
    uart_event_t event;
    while (1)
    {
        if (xQueueReceive(uartevtQueue, (void *)&event, portMAX_DELAY)) 
        {
            switch (event.type) 
            {
            case UART_PARITY_ERR:
                //uart_clear_intr_status(static_cast<uart_port_t>(this->extuartNum), UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT);
                //uart_flush(static_cast<uart_port_t>(this->extuartNum));
                ESP_LOGI("", "uart rx parity error");
                break;
            //Event of UART frame error
            case UART_FRAME_ERR:
                //uart_clear_intr_status(static_cast<uart_port_t>(this->extuartNum), UART_INTR_RXFIFO_FULL| UART_INTR_RXFIFO_TOUT);
                //uart_flush(static_cast<uart_port_t>(this->extuartNum));
                ESP_LOGI("", "uart rx frame error");
                break;
            //Event of UART RX break detected
            case UART_BREAK:
                //uart_clear_intr_status(static_cast<uart_port_t>(this->extuartNum), UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT);
                //uart_flush(static_cast<uart_port_t>(this->extuartNum));
                ESP_LOGI("", "uart rx break");
                break;
            default:
                ESP_LOGI("", "uart event type: %d", event.type);
                break;
            }
        }
    }
}*/

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

/*void VistaBus::uart_evt_task_start(void *args)
{
    VistaBus *tsk = static_cast<VistaBus *>(args);
    tsk->uart_evt_task(args);
}*/
