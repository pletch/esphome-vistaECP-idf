#include "ecp_protocol.h"
#include "vistabus.h"

VistaECP::VistaECP(VistaBus& vistabus) : vistabus_(vistabus) {}

void IRAM_ATTR VistaECP::gpio_isr_handler(void * args)
{
    GpioTaskArgs * taskargs = (GpioTaskArgs *) args; 
    BaseType_t xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;
    int val = gpio_get_level(static_cast<gpio_num_t>(taskargs->pin));
    xTaskNotifyFromISR(taskargs->task_handle,val, eSetValueWithOverwrite,&xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

void IRAM_ATTR VistaECP::timer_isr_handler(void * task_handle)
{
    TaskHandle_t th = (TaskHandle_t) task_handle;
    BaseType_t xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(th,0xFFFFFFFF, eSetValueWithOverwrite,&xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
} 

int VistaECP::uart_read_bytes_event(uart_port_t uart_num, uint8_t * rxbuf, int len, int timeout, QueueHandle_t queue)
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

bool VistaECP::mark_pulse(int uartNum, uint8_t address)
{
    uart_set_parity(static_cast<uart_port_t>(uartNum),UART_PARITY_DISABLE);
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
    xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(8)); //first rising edge
    uart_write_bytes(static_cast<uart_port_t>(uartNum), &snd_data[0], 1); 

    xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(4)); //second rising edge  Older panel 4140XMPT2 does not have 3 cycle pulse pattern.  Only one rising edge after 13 ms low signal.
    if (snd_data[1] != 0)
        uart_write_bytes(static_cast<uart_port_t>(uartNum), &snd_data[1], 1);
            
    xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(4)); //third rising edge           
    if (snd_data[2] != 0)
        uart_write_bytes(static_cast<uart_port_t>(uartNum), &snd_data[2], 1);
    sent_request = true;

    uart_set_parity(static_cast<uart_port_t>(uartNum),UART_PARITY_EVEN);
    return sent_request;
}

int VistaECP::get_packet_event(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, 
        int len, uart_port_t uart_num, int timeout, QueueHandle_t queue)
{
    const int rxBytes = uart_read_bytes_event(uart_num, rxbuf, len, timeout, queue);
    memcpy(received_packet->payload+start,rxbuf,rxBytes);
    received_packet->payload[rxBytes+start] = '\0';
    received_packet->size = rxBytes+start;
    return rxBytes;
}

int VistaECP::get_packet(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, 
        int len, uart_port_t uart_num, int timeout)
{
    const int rxBytes = uart_read_bytes(uart_num, rxbuf, len, timeout);
    memcpy(received_packet->payload+start,rxbuf,rxBytes);
    received_packet->payload[rxBytes+start] = '\0';
    received_packet->size = rxBytes+start;
    return rxBytes;
}

int VistaECP::keypad_write(const uart_port_t uart_n, const SendPacket &pkt_to_send)
{
    char outbuffer[24];
    memset(outbuffer,'\0',sizeof(outbuffer));
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
    return uart_write_bytes(uart_n, outbuffer, pkt_to_send.size+3);   
}

void VistaECP::dispatchF2()
{
    uint8_t data[kRXBufSize+1];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xF2;
    int rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 1, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    received_packet.payload[1] = data[0];
    rxBytes = this->get_packet_event(&received_packet, data, 2, static_cast<int> (received_packet.payload[1]),
            vistabus_.uart_num, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    if (valid_chksum(received_packet.payload,0,rxBytes+2)) 
        received_packet.source = 0xF2;
    else
        received_packet.source = 0xCF;
    xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
}

void VistaECP::dispatchF6(const SendPacket &pkt_to_send)
{
    uint8_t data[4];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xF6;
    int rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 1, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue); //Get Address
    if(data[0] != 0 && vistabus_.monitor_rx_task_Handle != nullptr)
    {
        uint32_t val = 0xF6 << 8 | data[0];
        xTaskNotify(vistabus_.monitor_rx_task_Handle,val, eSetValueWithOverwrite);
    }
    received_packet.payload[1] = data[0];
    received_packet.size = 2;
    if (received_packet.payload[1] == 1 || received_packet.payload[1] == 2 || 
            received_packet.payload[1] == 5 || received_packet.payload[1] == 6)
        received_packet.source = 0xF2;
    else
        received_packet.source = 0xF6;
    xQueueSend(vistabus_.receiveQueue,&received_packet,pdMS_TO_TICKS(20));
    this->uart_read_bytes_event(vistabus_.uart_num, &data[1], 1, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue); //flush lagging zero
    if(this->req_to_send && data[0] == pkt_to_send.keypadaddress) //ACK was for us.  Try to send.
    { 
        data[rxBytes] = 0;
        this->keypad_write(vistabus_.uart_num, pkt_to_send );

        rxBytes = this->get_packet_event(&received_packet, data, 0, 1, vistabus_.uart_num, pdMS_TO_TICKS(100), vistabus_.uartevtQueue);
        if(rxBytes)
        {
            if (data[0] == pkt_to_send.sequence)
            {
                this->req_to_send = false;
                this->pulse_marked = false;
#ifdef DEBUG_LOG
                xQueueSend(vistabus_.receiveQueue,&received_packet,pdMS_TO_TICKS(20));
#endif               
            }

            if (this->req_to_send)
            {
                ESP_LOGW(TAG, "Did not find expected byte in response of %d bytes.", rxBytes);
                this->req_to_send = false;
            }
            
        }
        else
        {
            ESP_LOGW(TAG, "Did not receive any response bytes from panel.");
            this->req_to_send = false;
        }
    } 
    else //ACK was for another device.
    {        
        rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 1, pdMS_TO_TICKS(50), vistabus_.uartevtQueue);
 #ifdef DEBUG_LOG
        if (rxBytes) //should receive single panel response byte
        {
            received_packet.payload[0] = data[0];
            received_packet.size = 1;
            xQueueSend(vistabus_.receiveQueue,&received_packet,pdMS_TO_TICKS(0));
        }
#endif
    }
}

void VistaECP::dispatchF7()
{
    uint8_t data[kF7MessageLength-1];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xF7;
    int rxBytes = this->get_packet_event(&received_packet, data, 1, kF7MessageLength-1, vistabus_.uart_num, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    if (valid_chksum(received_packet.payload,0,rxBytes+1))
        received_packet.source = 0xF7;
    else
        received_packet.source = 0xCF;
    xQueueSend(vistabus_.receiveQueue,&received_packet,0);
}

void VistaECP::dispatchF8()
{
    uint8_t data[kRXBufSize+1];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xF8;
    int rxBytes = 0;  
    //ToDo: Consider if checksum is needed in following section
    if (this->legacy_programmode)
    {
        received_packet.source = 0xDD;
        rxBytes = this->get_packet_event(&received_packet, data, 1, 32, vistabus_.uart_num, 
                pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    }
    else
    {
        rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 2, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
        if (rxBytes == 2)
        {
            received_packet.payload[1] = data[0];
            received_packet.payload[2] = data[1];  //Size of packet
            rxBytes = this->get_packet_event(&received_packet, data, 3, static_cast<int> (data[1]),
                    vistabus_.uart_num,pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue); 
            if (valid_chksum(received_packet.payload,0,rxBytes+3))
            { 
                uint32_t val = 0xF8 << 8 | received_packet.payload[1];
                if (vistabus_.monitor_rx_task_Handle != nullptr)
                    xTaskNotify(vistabus_.monitor_rx_task_Handle,val,eSetValueWithOverwrite);
            }
        }
    }
    xQueueSend(vistabus_.receiveQueue,&received_packet,pdMS_TO_TICKS(0));
}

void VistaECP::dispatchF9()
{
    uint8_t data[kRXBufSize+1];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xF9; 
    int rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 2, 
            pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    if (rxBytes == 2)
    {
        received_packet.payload[1] = data[0];
        received_packet.payload[2] = data[1];  //Size of packet
        rxBytes = this->get_packet_event(&received_packet, data, 3, static_cast<int> (data[0]),
                vistabus_.uart_num,pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
        if (valid_chksum(received_packet.payload,0,rxBytes+3))
        {
            received_packet.source = 0xF9;
            uint32_t val = 0xF9 << 16 | received_packet.payload[1] << 8 | received_packet.payload[3];
            if (vistabus_.monitor_rx_task_Handle != nullptr)
                xTaskNotify(vistabus_.monitor_rx_task_Handle,val, eSetValueWithOverwrite);
            if (vistabus_.LRRemulation)
                this->quick_decodeF9(received_packet.payload);
        }
        else
            received_packet.source = 0xCF;
        xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
        if (received_packet.source == 0xF9)
        {
            rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 2, pdMS_TO_TICKS(30), vistabus_.uartevtQueue);
#ifdef DEBUG_LOG
            if (rxBytes) //should receive single panel response byte
            {   
                received_packet.payload[0] = data[1]; //first byte is a zero
                received_packet.size = 1;
                xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
            }
#endif
        }
    }
}

void VistaECP::quick_decodeF9(const char * cbuf)
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
        uart_write_bytes(vistabus_.uart_num,response, 6);
    }
    else if (cbuf[3] == 0x48 || cbuf[3] == 0x52 || cbuf[3] == 0x58)
    {
        uart_write_bytes(vistabus_.uart_num,&cbuf[1], 1);
    }
}

void VistaECP::dispatchFB()
{
    uint8_t data[kFBMessageLength+1];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xFB; 
    int rxBytes = this->get_packet_event(&received_packet, data, 1, kFBMessageLength,
            vistabus_.uart_num, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    if (valid_chksum(received_packet.payload,0,rxBytes+1))
    {
        uint32_t val = 0xFB << 16 | received_packet.payload[1] << 8 | received_packet.payload[3];
        if (vistabus_.monitor_rx_task_Handle != nullptr)
            xTaskNotify(vistabus_.monitor_rx_task_Handle,val,eSetValueWithOverwrite);                
        if (vistabus_.RFRemulation)
            this->quick_decodeFB(received_packet.payload);
        received_packet.source = 0xFB;
    }
    else
    {
        received_packet.source = 0xCF;
    }
    xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
}

void VistaECP::quick_decodeFB(const char * cbuf)
{
    // For timing , must handle 0xFB packet here if emulating rather than through queues in vistaalarm process. 
    uint8_t type = cbuf[3];
    // 0xF1 - response to request, 0x80 - retry, 0x60 or 0x81 supervision, 0x82 supervision w/ type response
    if (type == 0xF1)
    {   
        DeviceMsg rfMsg;
        if (xQueueReceive(vistabus_.deviceMsgQueue, &rfMsg, pdMS_TO_TICKS(100)) == pdPASS)
        {
            uint8_t seq = cbuf[2];
            char lcbuf[7];
            uint8_t exp_seq = (seq == 0x20 ? 0x54 : 0x51);
            lcbuf[0] = vistabus_.emulated_rf_receiver.address;
            lcbuf[1] = exp_seq;
            lcbuf[2] = rfMsg.source >> 16 | 0x80;  //Set buf 2,3,4 to rf serial number
            lcbuf[3] = rfMsg.source >> 8 & 0xFF;
            lcbuf[4] = rfMsg.source & 0xFF;
            lcbuf[5] = rfMsg.msg; //Set to fault status with loop mask
            lcbuf[6] = calc_chksum_two(lcbuf, 0, 6);
            uart_write_bytes(vistabus_.uart_num,lcbuf, 7);
        }
    }
    else if (type == 0x60 || type == 0x81 || type == 0x82)
    { // supervision query  ToDo:  Come back and check this.
        uint8_t seq = cbuf[2];
        char lcbuf[4];
        uint8_t exp_seq = (seq == 0x20 ? 0x24 : 0x21);
        lcbuf[0] = vistabus_.emulated_rf_receiver.address;
        lcbuf[1] = exp_seq;
        lcbuf[2] = 0x05; // 5881ENL = 3, 5881ENH = 5
        lcbuf[3] = calc_chksum_two(lcbuf, 0, 3);
        uart_write_bytes(vistabus_.uart_num,lcbuf, 4);
    }
}

void VistaECP::dispatch_legacyStatusPacket(uint8_t header)
{
    uint8_t data[6];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = header; 
    uart_set_baudrate(vistabus_.uart_num,2400);
    int rxBytes = this->get_packet_event(&received_packet, data, 1, 4, vistabus_.uart_num, 
            pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    received_packet.source = 0xDD;
    uart_set_baudrate(vistabus_.uart_num,4800);
    xQueueSend(vistabus_.receiveQueue,&received_packet,0);
    if ((header != 0xFE && header != 0xFF) && rxBytes == 4)
    {
        this->legacy_programmode = data[2] & 0x40;
    }
}

void VistaECP::dispatchDebug(uint8_t header, uint8_t type)
{
    ReceivedPacket received_packet;
    received_packet.type = type;
    received_packet.payload[0] = header; 
    received_packet.size = 1;
    xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(20));
}

void VistaECP::track_write_attempts()
{
    if (this->req_to_send && this->pulse_marked && (esp_timer_get_time() - this->pulse_mark_time > 1200000 )) //should receive ack within 1.2 seconds
    {
        this->ack_failures++;
        this->pulse_marked = false;
    }

    if (this->ack_failures == 10)
    {
        ESP_LOGW(TAG, "Failure to receive F6 ACK after 10 successive pulse marks.  Giving up.");
        this->req_to_send = false;
        this->ack_failures = 0;
    }

    if (!this->req_to_send)
    {
        this->ack_failures = 0;
        this->pulse_marked = false;
    }    
}

void VistaECP::dispatch_extF6(uint32_t val, uint8_t header)
{
    uint8_t data[48];  //ToDo verify what maximum size is in write service
    ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    int rxBytes = 0;
    uint8_t n = 0;
    data[0] = header;
    while ((data[0] & 0x0F) != static_cast<uint8_t>(val & 0x0F) && n < 3) //discard any mark bytes
    {
        rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
        n++;
    }
    rcvd_extPkt.payload[0] = data[0];
    rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(150));
    rcvd_extPkt.payload[1] = data[0]; //length
    this->get_packet(&rcvd_extPkt, data, 2, rcvd_extPkt.payload[1], vistabus_.ext_uart_num, pdMS_TO_TICKS(150));
    if (static_cast<uint8_t>(val) == 1 || static_cast<uint8_t>(val) == 2 
            || static_cast<uint8_t>(val) == 5 || static_cast<uint8_t>(val) == 6)
        rcvd_extPkt.source = 0xF2;
    else
        rcvd_extPkt.source = 0xF6;
    xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt, pdMS_TO_TICKS(0));
}

void VistaECP::dispatch_extF8(uint32_t val, uint8_t header)
{
    uint8_t data[2];
    ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    int rxBytes = 0;
    uint8_t n = 0;
    data[0] = header;
    while ((data[0]) != static_cast<uint8_t>(val) && n < 2)
    {
        rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
        n++;
    }
#ifdef DEBUG_LOG
    rcvd_extPkt.payload[0] = data[0];
    rcvd_extPkt.size = 1;
    xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
#endif
}

void VistaECP::dispatch_extF9(uint32_t val, uint8_t header)
{
    uint8_t data[7];
    ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    int rxBytes = 0;
    uint8_t n = 0;
    data[0] = header;
    uint8_t mb = static_cast<uint8_t>(val >> 8) + 0x40;
    while (data[0] != mb && data[0] != static_cast<uint8_t>(val >> 8) && n < 2)
    {
        rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
        n++;
    }
    rcvd_extPkt.payload[0] = data[0];
    rcvd_extPkt.source = 0xF9;
    if (static_cast<uint8_t>(val) == 0x53)
    {
        this->get_packet(&rcvd_extPkt, data, 1, 5, vistabus_.ext_uart_num, pdMS_TO_TICKS(25));
        xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
    }
#ifdef DEBUG_LOG                
    else
    {
        rcvd_extPkt.size = 1;
        xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
    }
#endif
}

void VistaECP::dispatch_extFA(uint32_t val, uint8_t header, bool legacy)
{
    uint8_t data[7];
    ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    int rxBytes = 0;
    uint8_t n = 0;
    data[0] = header;
    if (static_cast<uint8_t>(val) == 0xF1) //Incoming zone data from Expander
    {
        uint8_t req_addr = 99;
        if (legacy)
        {
            req_addr = 1;
        }
        else
        {
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
        }
        uint8_t n = 0;
        while (data[0] != req_addr && n < 2)
        {
            rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
            n++;
        }
        rcvd_extPkt.payload[0] = data[0];
        int res = this->get_packet(&rcvd_extPkt, data, 1, 3, vistabus_.ext_uart_num, 
                pdMS_TO_TICKS(kUartDelay)); 
        if (res > 0)
        {
            rcvd_extPkt.source = 0xFA;
            xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt, pdMS_TO_TICKS(10));
        }
    }
    else
    {
        uint8_t n = 0;
        while (data[0] != 0xF0 && n < 2)
        {
            rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
            n++;
        }
        rcvd_extPkt.payload[0] = data[0];
        this->get_packet(&rcvd_extPkt, data, 1, 5, vistabus_.ext_uart_num, pdMS_TO_TICKS(50));
        rcvd_extPkt.source = 0xFA;
        xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));                    
    }
}

void VistaECP::dispatch_extFB(uint32_t val, uint8_t header)
{
    uint8_t data[kRFZoneMessageLength+1];
    ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    int rxBytes = 0;
    uint8_t n = 0;
    data[0] = header;
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
    while (data[0] != req_addr && n < 2)
    {
        rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
        n++;
    }
    rcvd_extPkt.payload[0] = data[0];
    if ((val & 0xFF) == 0xF1) //Incoming zone data from Radio Frequency Receiver
    {
        int res = this->get_packet(&rcvd_extPkt, data, 1, kRFZoneMessageLength-1, vistabus_.ext_uart_num, 
            pdMS_TO_TICKS(kUartDelay));
        if (res > 0)
        {
            rcvd_extPkt.source = 0xFB;
            xQueueSend(vistabus_.receiveQueue,&rcvd_extPkt,pdMS_TO_TICKS(20));
        }
    }
    else //Response to FB poll command
    {
        this->get_packet(&rcvd_extPkt, data, 1, 3, vistabus_.ext_uart_num, pdMS_TO_TICKS(kUartDelay));
        rcvd_extPkt.source = 0xFB;
        xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
    }
}