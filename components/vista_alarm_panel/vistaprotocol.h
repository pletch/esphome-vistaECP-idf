#pragma once
#include "helperstructs.h"

static void IRAM_ATTR gpio_isr_handler(void * args)
{
    gpioTaskArgs * taskargs = (gpioTaskArgs *) args; 
    BaseType_t xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;
    int val = gpio_get_level(static_cast<gpio_num_t>(taskargs->pin));
    xTaskNotifyFromISR(taskargs->task_handle,val, eSetValueWithOverwrite,&xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
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

static bool mark_pulse(int uartNum, uint8_t address)
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
    xTaskNotifyWait(0,0xFFFFFFFF,NULL,pdMS_TO_TICKS(5)); //first rising edge
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

template <typename VistaProtocolDeriv>
class VistaProtocol 
{
public:
    bool checkSendQ(QueueHandle_t sendQueue, SendPacket &pkt, bool &req_to_send) 
    {
        return static_cast<VistaProtocolDeriv*>(this)->checkSendQ_impl(sendQueue, pkt, req_to_send);
    }

    int handleUARTevents(QueueHandle_t uartevtQueue, TaskHandle_t rx_tx_task_Handle, int uartNum, int rxPin, 
                bool req_to_send, bool &pulse_marked, uint64_t &pulse_mark_time, uint8_t * buf, int addr)
    {
        return static_cast<VistaProtocolDeriv*>(this)->handleUARTevents_impl(uartevtQueue, rx_tx_task_Handle, uartNum, rxPin, 
                req_to_send, pulse_marked, pulse_mark_time, buf, addr);
    } 

protected:

};



