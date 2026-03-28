#pragma once

#include "vistaprotocol.h"
#include "helperstructs.h"
#include "helperfuncs.h"

class VistaSE : public VistaProtocol<VistaSE> {
public:
    bool legacy_protocol_impl()
    {
        return true;
    }

    void check_send_Q_impl(const QueueHandle_t sendQueue, SendPacket &pkt)
    {
        while (uxQueueMessagesWaiting(sendQueue))
        {
            if (!this->req_to_send)
            {
                xQueueReceive(sendQueue,&pkt,0);
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
                    xQueueSend(sendQueue,&next_char,pdMS_TO_TICKS(0));
                }
                xQueueReceive(sendQueue,&pkt,0); 
                break;
            }
            else
                break;
        }   
        return;
    }

    int handle_UART_events_impl(const QueueHandle_t uartevtQueue, const TaskHandle_t rx_tx_task_Handle, 
            const TaskHandle_t monitor_rx_task_Handle, int uartNum, int rxPin, const SendPacket &pkt_to_send, uint8_t * buf)
    {
        int bytes = 0;
        uart_event_t event;
        xQueueReceive(uartevtQueue, (void *)&event, pdMS_TO_TICKS(PULSE_CYCLE_PERIOD));
        switch (event.type)
        {
            case UART_DATA:
                bytes = uart_read_bytes(static_cast<uart_port_t>(uartNum), buf, 1, 0);
                if (buf[0] == 0)
                    bytes = 0;
                break;
            case UART_BREAK:
                gpioTaskArgs taskargs;
                taskargs.task_handle = rx_tx_task_Handle;
                taskargs.pin = rxPin;
                if(monitor_rx_task_Handle != NULL)                     
                    //notify break detected to sync tx_monitor task to allow reading of 2400/5/1 bits
                {
                    uint32_t val = 0x11 << 8;
                    xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                } 
                if (gpio_get_level( static_cast<gpio_num_t>(rxPin)))
                {                      
                    int64_t high_start = esp_timer_get_time();
                    gpio_set_intr_type(static_cast<gpio_num_t>(rxPin), GPIO_INTR_NEGEDGE);
                    gpio_isr_handler_add(static_cast<gpio_num_t>(rxPin), gpio_isr_handler, (void *) &taskargs );
                    if (xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(535)) == pdPASS) //notify again that 4ms window is started
                    {
                        if(monitor_rx_task_Handle != NULL)                        
                        {
                            uint32_t val = 0x12 << 8;
                            xTaskNotify(monitor_rx_task_Handle,val, eSetValueWithOverwrite);
                        }            
                        int64_t start = esp_timer_get_time();
                        int64_t high_time = start - high_start; 
                        if ((high_time > 20000))
                        {
                            //ESP_LOGE("TAG", "start - high_start: %lld", start-high_start);                
                            gpio_set_intr_type(static_cast<gpio_num_t>(rxPin), GPIO_INTR_POSEDGE);
                            if (this->req_to_send && pkt_to_send.type == 1 && (high_time < 60000 || high_time > 150000)) //try to write
                            {
                                const esp_timer_create_args_t oneshot_timer_args = 
                                {
                                    .callback = &timer_isr_handler,
                                    .arg = (void*) rx_tx_task_Handle,
                                    .name = "one-shot"
                                };
                                esp_timer_handle_t oneshot_timer;
                                esp_timer_create(&oneshot_timer_args, &oneshot_timer);
                                esp_timer_start_once(oneshot_timer, 3000); //delay 3ms before sending

                                xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(10));
                                gpio_set_intr_type(static_cast<gpio_num_t>(rxPin), GPIO_INTR_ANYEDGE);    
                                bool data_written = true;
                                uart_set_baudrate(static_cast<uart_port_t>(uartNum),2400);
                                uart_set_stop_bits(static_cast<uart_port_t>(uartNum), UART_STOP_BITS_1);
                                uart_set_word_length(static_cast<uart_port_t>(uartNum), UART_DATA_5_BITS);
                                keypad_write_SE(static_cast<uart_port_t>(uartNum),pkt_to_send.payload);
                                if (xTaskNotifyWait(0,0xFFFFFFFF,NULL,0) != pdTRUE)
                                {
                                    keypad_write_SE(static_cast<uart_port_t>(uartNum),pkt_to_send.payload);
                                    if (xTaskNotifyWait(0,0xFFFFFFFF,NULL,0) != pdTRUE)
                                        keypad_write_SE(static_cast<uart_port_t>(uartNum),pkt_to_send.payload);
                                    else
                                        data_written = false;
                                }
                                else
                                    data_written = false;
                                if (data_written)
                                {
                                    this->req_to_send = false;
                                }
                                uart_set_word_length(static_cast<uart_port_t>(uartNum), UART_DATA_8_BITS);
                                uart_set_stop_bits(static_cast<uart_port_t>(uartNum), UART_STOP_BITS_2);
                                uart_set_baudrate(static_cast<uart_port_t>(uartNum),4800);

                                esp_timer_delete(oneshot_timer);
                            }
                            else if (xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(10)) == pdPASS)
                            {
                                int64_t end = esp_timer_get_time();
                                if (end - start > 5700 && end - start < 6300)
                                {
                                    uart_flush(static_cast<uart_port_t>(uartNum));  // flush UART to ensure sync of read against 2400 preamble
                                    uart_read_bytes_event(static_cast<uart_port_t>(uartNum), buf, 1, pdMS_TO_TICKS(4), uartevtQueue); //flush leading zero
                                    uart_set_baudrate(static_cast<uart_port_t>(uartNum),2400);
                                    bytes = uart_read_bytes_event(static_cast<uart_port_t>(uartNum), buf, 1, pdMS_TO_TICKS(15), uartevtQueue);
                                    uart_set_baudrate(static_cast<uart_port_t>(uartNum),4800);
                                    this->is_2400 = true;
                                }
                            }
                            else if (this->req_to_send && !this->pulse_marked && pkt_to_send.type == 2)
                            {
                                this->pulse_marked = mark_pulse(uartNum, pkt_to_send.keypadaddress);
                                this->pulse_mark_time = esp_timer_get_time();
                                bytes = uart_read_bytes_event(static_cast<uart_port_t>(uartNum), buf, 1, pdMS_TO_TICKS(10), uartevtQueue);
                            }
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
        int bytes = 0;

        xTaskNotifyWait(0xFFFFFFFF,0xFFFFFFFF,&val,pdMS_TO_TICKS(portMAX_DELAY));
        if (static_cast<uint8_t>(val >> 8) == 0x11) //in a break
        {
            uart_set_baudrate(static_cast<uart_port_t>(extuartNum),2400);
            uart_set_stop_bits(static_cast<uart_port_t>(extuartNum), UART_STOP_BITS_1);
            uart_set_word_length(static_cast<uart_port_t>(extuartNum), UART_DATA_5_BITS);
            buf[0] = 0;
            memset(rcvd_extPkt.payload,'\0',sizeof(rcvd_extPkt.payload));
            xTaskNotifyWait(0xFFFFFFFF,0,&val,pdMS_TO_TICKS(portMAX_DELAY)); //start of low time after break
            if (static_cast<uint8_t>(val >> 8) == 0x12)
            {
                rcvd_extPkt.source = 0xDD; //only VistaSE protocol writes here
                bytes = get_Packet(&rcvd_extPkt, buf, 0, 3, static_cast<uart_port_t>(extuartNum), pdMS_TO_TICKS(8)); //wait minimum of 4ms for any data to start arriving
            }
            uart_set_word_length(static_cast<uart_port_t>(extuartNum), UART_DATA_8_BITS);
            uart_set_stop_bits(static_cast<uart_port_t>(extuartNum), UART_STOP_BITS_2);
            uart_set_baudrate(static_cast<uart_port_t>(extuartNum),4800);
            if (bytes)
            {
                uart_flush(static_cast<uart_port_t>(extuartNum));
                xQueueSend(receiveQueue, &rcvd_extPkt,pdMS_TO_TICKS(0));
                bytes = 0;
            }            
        }
        else
        {
            bytes = uart_read_bytes(static_cast<uart_port_t>(extuartNum), buf, 1, pdMS_TO_TICKS(20));
        }
        return bytes;
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
