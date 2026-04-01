#pragma once

#include "vistaprotocol.h"

class Vista20P : public VistaProtocol<Vista20P> 
{
public:
    bool legacy_protocol_impl()
    {
        return false;
    }
    
    void check_send_Q_impl(const QueueHandle_t sendQueue, SendPacket &pkt)
    {
        while (uxQueueMessagesWaiting(sendQueue))
        {
            if (!req_to_send)
            {
                xQueueReceive(sendQueue,&pkt,0);
                req_to_send = true;
                if (pkt.sequence == last_sequence && pkt.keypadaddress == last_address)
                    pkt.sequence += 0x40; // Iterate sequencing if needed when consolidation occurs
                this->last_sequence = pkt.sequence;
                this->last_address = pkt.keypadaddress;
            }
            else
            {
                SendPacket next_pkt;
                xQueuePeek(sendQueue, &next_pkt,pdMS_TO_TICKS(0));
                if(next_pkt.keypadaddress == pkt.keypadaddress && (next_pkt.size + pkt.size) <= 24)
                {
                    xQueueReceive(sendQueue, &next_pkt, 0);
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

    int handle_UART_events_impl(const QueueHandle_t uartevtQueue, const TaskHandle_t rx_tx_task_Handle, 
            const TaskHandle_t monitor_rx_task_Handle, int uartNum, int rxPin, const SendPacket &pkt_to_send, uint8_t * buf)
    {
        int bytes = 0;
        uart_event_t event;
        xQueueReceive(uartevtQueue, (void *)&event, pdMS_TO_TICKS(kPulseCyclePeriod));
        switch (event.type)
        {
            case UART_DATA:
                bytes = uart_read_bytes(static_cast<uart_port_t>(uartNum), buf, 1, 0);
                break;
            case UART_BREAK:
                static GpioTaskArgs taskargs;
                taskargs.task_handle = rx_tx_task_Handle;
                taskargs.pin = rxPin;

                if (gpio_get_level( static_cast<gpio_num_t>(rxPin)))
                { 
                    gpio_set_intr_type(static_cast<gpio_num_t>(rxPin), GPIO_INTR_NEGEDGE);
                    gpio_isr_handler_add(static_cast<gpio_num_t>(rxPin), gpio_isr_handler, (void *) &taskargs );
                    if (xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(535)) == pdPASS)
                    {
                        int64_t start = esp_timer_get_time();
                        gpio_set_intr_type(static_cast<gpio_num_t>(rxPin), GPIO_INTR_POSEDGE);
                        if (xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(10)) == pdPASS)
                        {
                            int64_t end = esp_timer_get_time();
                            if (end - start > 5700 && end - start < 6300)
                            {
                                uart_flush(static_cast<uart_port_t>(uartNum));  // flush UART ahead of 2400 preamble
                                uart_read_bytes_event(static_cast<uart_port_t>(uartNum), buf, 1, pdMS_TO_TICKS(4), uartevtQueue); //flush leading zero
                                uart_set_baudrate(static_cast<uart_port_t>(uartNum),2400);
                                bytes = uart_read_bytes_event(static_cast<uart_port_t>(uartNum), buf, 1, pdMS_TO_TICKS(10), uartevtQueue);
                                uart_set_baudrate(static_cast<uart_port_t>(uartNum),4800);
                                this->is_2400 = true;
                            }
                        } 
                        else if (this->req_to_send && !this->pulse_marked)
                        {
                            this->pulse_marked = mark_pulse(uartNum, pkt_to_send.keypadaddress);
                            this->pulse_mark_time = esp_timer_get_time();
                            bytes = uart_read_bytes_event(static_cast<uart_port_t>(uartNum), buf, 1, pdMS_TO_TICKS(10), uartevtQueue);
                        }
                    }
                    gpio_isr_handler_remove(static_cast<gpio_num_t>(rxPin));
                }
                break;
            default:
                break;
        }
        return bytes;
    }

    int monitor_task_sync_impl(int extuartNum, uint8_t * buf, uint32_t &val, 
            const QueueHandle_t receiveQueue, ReceivedPacket &rcvd_extPkt)
    {
        int bytes = uart_read_bytes(static_cast<uart_port_t>(extuartNum), buf, 1, portMAX_DELAY);
        if (val == 0)
            xTaskNotifyWait(0,0xFFFFFFFF,&val,pdMS_TO_TICKS(400));  //data of interest incoming according to RX_TX Task
        return bytes;
    }
    

private:
    uint8_t last_sequence = 0;
    uint8_t last_address = 99;
};
