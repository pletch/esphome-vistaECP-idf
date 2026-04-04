// Derived class implementing Legacy Vista SE Protocol Functionality
#pragma once

#include "protocol_base.h"

class VistaECP;

class VistaSE : public ProtocolBase<VistaSE> {
public:
    using ProtocolBase<VistaSE>::ProtocolBase;

    bool legacy_protocol = true;

    void check_send_Q_impl(SendPacket &pkt)
    {
        while (uxQueueMessagesWaiting(this->vistabus_.sendQueue))
        {
            if (!this->req_to_send)
            {
                xQueueReceive(this->vistabus_.sendQueue,&pkt,0);
                this->req_to_send = true;
            }
            if (pkt.size > 1) //split data into individual chars
            {   
                SendPacket next_char;
                for (int i = 0; i < pkt.size; i++)
                {
                    next_char.keypadaddress = pkt.keypadaddress;
                    next_char.type = pkt.type;
                    next_char.sequence = pkt.sequence;
                    next_char.size = 1;
                    next_char.payload[0] = pkt.payload[i];
                    xQueueSend(this->vistabus_.sendQueue,&next_char,pdMS_TO_TICKS(0));
                }
                xQueueReceive(this->vistabus_.sendQueue,&pkt,0); 
                break;
            }
            else
                break;
        }   
    }

    int handle_UART_events_impl(const SendPacket &pkt_to_send, uint8_t * buf)
    {
        int bytes = 0;
        uart_event_t event;
        xQueueReceive(this->vistabus_.uartevtQueue, (void *)&event, pdMS_TO_TICKS(kPulseCyclePeriod));
        switch (event.type)
        {
            case UART_DATA:
                bytes = uart_read_bytes(this->vistabus_.uart_num, buf, 1, 0);
                if (buf[0] == 0)
                    bytes = 0;
                break;
            case UART_BREAK:
                static GpioTaskArgs taskargs;
                taskargs.task_handle = this->vistabus_.rx_tx_task_Handle;
                taskargs.pin = this->vistabus_.rx_pin;
                if(this->vistabus_.monitor_rx_task_Handle != nullptr)                     
                    //notify break detected to sync tx_monitor task to allow reading of 2400/5/1 bits
                {
                    uint32_t val = 0x11 << 8;
                    xTaskNotify(this->vistabus_.monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                } 
                if (gpio_get_level( this->vistabus_.rx_pin))
                {                      
                    int64_t high_start = esp_timer_get_time();
                    gpio_set_intr_type(this->vistabus_.rx_pin, GPIO_INTR_NEGEDGE);
                    gpio_isr_handler_add(this->vistabus_.rx_pin, gpio_isr_handler, (void *) &taskargs );
                    if (xTaskNotifyWait(0,0xFFFFFFFF,nullptr,pdMS_TO_TICKS(535)) == pdPASS) //notify again that 4ms window is started
                    {
                        if(this->vistabus_.monitor_rx_task_Handle != nullptr)                        
                        {
                            uint32_t val = 0x12 << 8;
                            xTaskNotify(this->vistabus_.monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                        }            
                        int64_t start = esp_timer_get_time();
                        int64_t high_time = start - high_start; 
                        if ((high_time > 20000))
                        {
                            //ESP_LOGE("TAG", "start - high_start: %lld", start-high_start);                
                            gpio_set_intr_type(this->vistabus_.rx_pin, GPIO_INTR_POSEDGE);
                            if (this->req_to_send && pkt_to_send.type == 1 && (high_time < 60000 || high_time > 150000)) //try to write
                            {
                                const esp_timer_create_args_t oneshot_timer_args = 
                                {
                                    .callback = &timer_isr_handler,
                                    .arg = (void*) this->vistabus_.rx_tx_task_Handle,
                                    .name = "one-shot"
                                };
                                esp_timer_handle_t oneshot_timer;
                                esp_timer_create(&oneshot_timer_args, &oneshot_timer);
                                esp_timer_start_once(oneshot_timer, 3000); //delay 3ms before sending

                                xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(10));
                                gpio_set_intr_type(this->vistabus_.rx_pin, GPIO_INTR_ANYEDGE);    
                                bool data_written = true;
                                uart_set_baudrate(this->vistabus_.uart_num,2400);
                                uart_set_stop_bits(this->vistabus_.uart_num, UART_STOP_BITS_1);
                                uart_set_word_length(this->vistabus_.uart_num, UART_DATA_5_BITS);
                                keypad_write_SE(this->vistabus_.uart_num,pkt_to_send.payload);
                                if (xTaskNotifyWait(0,0xFFFFFFFF,NULL,0) != pdTRUE)
                                {
                                    keypad_write_SE(this->vistabus_.uart_num,pkt_to_send.payload);
                                    if (xTaskNotifyWait(0,0xFFFFFFFF,NULL,0) != pdTRUE)
                                        keypad_write_SE(this->vistabus_.uart_num,pkt_to_send.payload);
                                    else
                                        data_written = false;
                                }
                                else
                                    data_written = false;
                                if (data_written)
                                {
                                    this->req_to_send = false;
                                }
                                uart_set_word_length(this->vistabus_.uart_num, UART_DATA_8_BITS);
                                uart_set_stop_bits(this->vistabus_.uart_num, UART_STOP_BITS_2);
                                uart_set_baudrate(this->vistabus_.uart_num,4800);

                                esp_timer_stop(oneshot_timer);
                                esp_timer_delete(oneshot_timer);
                            }
                            else if (xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(10)) == pdPASS)
                            {
                                int64_t end = esp_timer_get_time();
                                if (end - start > 5700 && end - start < 6300)
                                {
                                    if (!this->legacy_programmode)
                                    {
                                        uart_flush(this->vistabus_.uart_num);  // flush UART to ensure sync of read against 2400 preamble
                                        uart_read_bytes_event(this->vistabus_.uart_num, buf, 1, pdMS_TO_TICKS(4), this->vistabus_.uartevtQueue); //flush leading zero
                                        uart_set_baudrate(this->vistabus_.uart_num,2400);
                                        bytes = uart_read_bytes_event(this->vistabus_.uart_num, buf, 1, pdMS_TO_TICKS(15), this->vistabus_.uartevtQueue);
                                        uart_set_baudrate(this->vistabus_.uart_num,4800);
                                        this->is_2400 = true;
                                    }
                                    else
                                    {
                                        uart_read_bytes_event(this->vistabus_.uart_num, buf, 1, pdMS_TO_TICKS(4), this->vistabus_.uartevtQueue); //flush leading zero
                                        bytes = uart_read_bytes_event(this->vistabus_.uart_num, buf, 1, pdMS_TO_TICKS(15), this->vistabus_.uartevtQueue);
                                    }

                                }
                            }
                            else if (this->req_to_send && !this->pulse_marked && pkt_to_send.type == 2)
                            {
                                mark_pulse(this->vistabus_.uart_num, pkt_to_send.keypadaddress);
                                this->pulse_marked = true;
                                this->pulse_mark_time = esp_timer_get_time();
                                bytes = uart_read_bytes_event(this->vistabus_.uart_num, buf, 1, pdMS_TO_TICKS(10), this->vistabus_.uartevtQueue);
                            }
                        }
                    }   
                    gpio_isr_handler_remove(this->vistabus_.rx_pin);
                }            
                break;
            default:
                break;
        }
        return bytes;
    }

    int monitor_task_sync_impl(uint8_t * buf, uint32_t &val)
    {
        ReceivedPacket rcvd_extPkt;
        rcvd_extPkt.type = 1;
        int bytes = 0;

        xTaskNotifyWait(0xFFFFFFFF,0xFFFFFFFF,&val,portMAX_DELAY);
        if (static_cast<uint8_t>(val >> 8) == 0x11) //in a break
        {
            uart_set_baudrate(this->vistabus_.ext_uart_num,2400);
            uart_set_stop_bits(this->vistabus_.ext_uart_num, UART_STOP_BITS_1);
            uart_set_word_length(this->vistabus_.ext_uart_num, UART_DATA_5_BITS);
            buf[0] = 0;
            memset(rcvd_extPkt.payload,'\0',sizeof(rcvd_extPkt.payload));
            xTaskNotifyWait(0xFFFFFFFF,0,&val,portMAX_DELAY); //start of low time after break
            if (static_cast<uint8_t>(val >> 8) == 0x12)
            {
                rcvd_extPkt.source = 0xDD; //only VistaSE protocol writes here
                bytes = this->get_packet(&rcvd_extPkt, buf, 0, 3, this->vistabus_.ext_uart_num, pdMS_TO_TICKS(8)); //wait minimum of 4ms for any data to start arriving
            }
            uart_set_word_length(this->vistabus_.ext_uart_num, UART_DATA_8_BITS);
            uart_set_stop_bits(this->vistabus_.ext_uart_num, UART_STOP_BITS_2);
            uart_set_baudrate(this->vistabus_.ext_uart_num,4800);
            if (bytes)
            {
                uart_flush(this->vistabus_.ext_uart_num);
                xQueueSend(this->vistabus_.receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                bytes = 0;
            }            
        }
        else
        {
            bytes = uart_read_bytes(this->vistabus_.ext_uart_num, buf, 1, pdMS_TO_TICKS(20));
        }
        return bytes;
    }

    void quick_decodeFA_impl(const char * cbuf)
    {
        // For timing , must handle 0xFA packet here if emulating rather than through queues in vistaalarm process.
        VistaBus::EmulatedExpander *expander = vistabus_.getExpander(0x01); 
        char lcbuf[5];
        char type = cbuf[2];
        char exp_sequence = (cbuf[1] == 0x21 ? 0x35 : 0x30);
        if (type == 0xF1)
        {
            DeviceMsg expMsg;
            xQueueReceive(vistabus_.deviceMsgQueue,&expMsg,portMAX_DELAY);
            lcbuf[0] = 0x01;
            lcbuf[1] = exp_sequence;
            uint8_t z = expMsg.source & 0x07;
            lcbuf[2] = z ? 0 : 0x01;
            uint8_t chksum = 0;
            lcbuf[3] = (z << 5) ^ (0x10*expMsg.msg); // we send out the current zone state
            lcbuf[4] = calc_chksum_two(lcbuf, 0, 4);
            uart_write_bytes(vistabus_.uart_num,lcbuf, 5);
        }
        else if (type == 0xF7)
        {
            lcbuf[0] = 0xF0;
            lcbuf[1] = exp_sequence;
            lcbuf[2] = expander->fault_NO_Bits;                      
            lcbuf[3] = expander->fault_NC_Bits; 
            lcbuf[4] = calc_chksum_two(lcbuf, 0, 4);
            uart_write_bytes(vistabus_.uart_num,lcbuf, 5);
        }
    }

    void dispatchFA_impl()
    {
        uint8_t data[kFALegacyMessageLength+1];
        ReceivedPacket received_packet;
        received_packet.type = 0;
        received_packet.payload[0] = 0xFA;
        uint8_t length = kFALegacyMessageLength;

        int rxBytes = this->get_packet_event(&received_packet, data, 1, length-1, vistabus_.uart_num, 
                pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
        bool chk = valid_chksum_two(received_packet.payload,0,rxBytes+1);
        if (chk)
        {
            uint32_t val = 0xFA << 16 | (received_packet.payload[1] << 8) | received_packet.payload[2];
            if (vistabus_.monitor_rx_task_Handle != nullptr)
                    xTaskNotify(vistabus_.monitor_rx_task_Handle,val, eSetValueWithOverwrite);
            if (vistabus_.EXPemulation)
                this->quick_decodeFA(received_packet.payload);
            received_packet.source = 0xFA;
        }
        else
            received_packet.source = 0xCF;
        xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
    }

private:
    int keypad_write_SE( uart_port_t uart_n, const char * character )
    {
        char outbuffer[2];
        memset(outbuffer,'\0',sizeof(outbuffer));
        if (character[0] >= 0x30 && character[0] <= 0x39)
            outbuffer[0] = (character[0] - 0x30);
        else if (character[0] == 0x23)
            outbuffer[0] = 0x0B;
        else if (character[0] == 0x2A)
            outbuffer[0] = 0x0A;
        else if (character[0] == 0x46)
            outbuffer[0] = 0x0C;
        else if (character[0] == 0x4D)
            outbuffer[0] = 0x0D;
        else if (character[0] == 0x50)
            outbuffer[0] = 0x0E;
        else if (character[0] == 0x47)
            outbuffer[0] = 0x0F;
        else if (character[0] >= 0x41 && character[0] <= 0x44)
            outbuffer[0] = (character[0] - 0x25);
        uart_tx_chars(uart_n, outbuffer,1);
        return uart_wait_tx_done(uart_n, pdMS_TO_TICKS(10));
    }
};
