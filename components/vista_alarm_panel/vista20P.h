// Derived class implementing Vista 15P/20P Protocol Functionality
#pragma once

#include "protocol_base.h"

class Vista20P : public ProtocolBase<Vista20P> 
{
public:
    using ProtocolBase<Vista20P>::ProtocolBase;
    
    bool legacy_protocol = false;

    void check_send_Q_impl(SendPacket &pkt)
    {
        while (uxQueueMessagesWaiting(this->vistabus_.sendQueue))
        {
            if (!req_to_send)
            {
                xQueueReceive(this->vistabus_.sendQueue,&pkt,0);
                req_to_send = true;
                if (pkt.sequence == last_sequence && pkt.keypadaddress == last_address)
                    pkt.sequence += 0x40; // Iterate sequencing if needed when consolidation occurs
                this->last_sequence = pkt.sequence;
                this->last_address = pkt.keypadaddress;
            }
            else
            {
                SendPacket next_pkt;
                xQueuePeek(this->vistabus_.sendQueue, &next_pkt,pdMS_TO_TICKS(0));
                if(next_pkt.keypadaddress == pkt.keypadaddress && (next_pkt.size + pkt.size) <= 24)
                {
                    xQueueReceive(this->vistabus_.sendQueue, &next_pkt, 0);
                    memcpy(pkt.payload + pkt.size, next_pkt.payload, next_pkt.size);
                    pkt.size += next_pkt.size;
                }
                else
                {
                    break;
                }
            }
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
                break;
            case UART_BREAK:
                static GpioTaskArgs taskargs;
                taskargs.task_handle = this->vistabus_.rx_tx_task_Handle;
                taskargs.pin = this->vistabus_.rx_pin;
                if (gpio_get_level( this->vistabus_.rx_pin))
                { 
                    gpio_set_intr_type(this->vistabus_.rx_pin, GPIO_INTR_NEGEDGE);
                    gpio_isr_handler_add(this->vistabus_.rx_pin, gpio_isr_handler, (void *) &taskargs );
                    if (xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(535)) == pdPASS)
                    {
                        int64_t start = esp_timer_get_time();
                        gpio_set_intr_type(this->vistabus_.rx_pin, GPIO_INTR_POSEDGE);
                        if (xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(10)) == pdPASS)
                        {
                            int64_t end = esp_timer_get_time();
                            if (end - start > 5700 && end - start < 6300)
                            {
                                uart_flush(this->vistabus_.uart_num);  // flush UART ahead of 2400 preamble
                                uart_read_bytes_event(this->vistabus_.uart_num, buf, 1, pdMS_TO_TICKS(4), this->vistabus_.uartevtQueue); //flush leading zero
                                uart_set_baudrate(this->vistabus_.uart_num,2400);
                                bytes = uart_read_bytes_event(this->vistabus_.uart_num, buf, 1, pdMS_TO_TICKS(10), this->vistabus_.uartevtQueue);
                                uart_set_baudrate(this->vistabus_.uart_num,4800);
                                this->is_2400 = true;
                            }
                        } 
                        else if (this->req_to_send && !this->pulse_marked)
                        {
                            this->pulse_marked = mark_pulse(this->vistabus_.uart_num, pkt_to_send.keypadaddress);
                            this->pulse_mark_time = esp_timer_get_time();
                            bytes = uart_read_bytes_event(this->vistabus_.uart_num, buf, 1, pdMS_TO_TICKS(10), this->vistabus_.uartevtQueue);
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

    int monitor_task_sync_impl(uint8_t * buf, uint32_t &val, ReceivedPacket &rcvd_extPkt)
    {
        int bytes = uart_read_bytes(this->vistabus_.ext_uart_num, buf, 1, portMAX_DELAY);
        if (val == 0)
            xTaskNotifyWait(0,0xFFFFFFFF,&val,pdMS_TO_TICKS(400));  //data of interest incoming according to RX_TX Task
        return bytes;
    }
    

    void processFA_impl(const char * cbuf)
    {
        // For timing , must handle 0xFA packet here if emulating rather than through queues in vistaalarm process. 
        char type = cbuf[4];
        char seq = cbuf[3];
        char lcbuf[5];
        bool valid_address = false;
        uint8_t address = 0;
        uint8_t exp_sequence = (seq == 0x20 || seq == 0x21 ? 0x34 : 0x31);
        for (int index = 1; index <= 5; index++)
        {
            if (cbuf[2] == 0x01 << index)
            {
                address = 6+index;
                valid_address = true;
                break;
            }
        }
        if (vistabus_.emulated_expanders.size() && valid_address)  //check if any emulated expanders present
        {
            VistaBus::EmulatedExpander *expander = vistabus_.getExpander(address);
            // we use zone to either | or & bits depending if in fault or reset
            // 0xF1 - response to request, 0xf7 - poll, 0x80 - retry
            if (expander != NULL)
            {
                if (type == 0xF1)
                {  
                    DeviceMsg expMsg;
                    xQueueReceive(vistabus_.deviceMsgQueue,&expMsg,portMAX_DELAY);
                    lcbuf[0] = address;
                    lcbuf[1] = exp_sequence;
                    uint8_t z = expMsg.source & 0x07;
                    lcbuf[2] = z ? 0 : 0x01;
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
        }
    }

private:
    uint8_t last_sequence = 0;
    uint8_t last_address = 99;
};
