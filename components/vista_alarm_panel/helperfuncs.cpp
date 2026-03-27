#include "helperfuncs.h"

void IRAM_ATTR gpio_isr_handler(void * args)
{
    gpioTaskArgs * taskargs = (gpioTaskArgs *) args; 
    BaseType_t xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;
    int val = gpio_get_level(static_cast<gpio_num_t>(taskargs->pin));
    xTaskNotifyFromISR(taskargs->task_handle,val, eSetValueWithOverwrite,&xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

void IRAM_ATTR timer_isr_handler(void * task_handle)
{
    TaskHandle_t th = (TaskHandle_t) task_handle;
    BaseType_t xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(th,0xFFFFFFFF, eSetValueWithOverwrite,&xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
} 

int uart_read_bytes_event(uart_port_t uart_num, uint8_t * rxbuf, int len, int timeout, QueueHandle_t queue)
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

bool mark_pulse(int uartNum, uint8_t address)
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

int get_Packet_event(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, 
        int len, uart_port_t uart_num, int timeout, QueueHandle_t queue)
{
    const int rxBytes = uart_read_bytes_event(uart_num, rxbuf, len, timeout, queue);
    memcpy(received_packet->payload+start,rxbuf,rxBytes);
    received_packet->payload[rxBytes+start] = '\0';
    received_packet->size = rxBytes+start;
    return rxBytes;
}

int get_Packet(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, 
        int len, uart_port_t uart_num, int timeout)
{
    const int rxBytes = uart_read_bytes(uart_num, rxbuf, len, timeout);
    memcpy(received_packet->payload+start,rxbuf,rxBytes);
    received_packet->payload[rxBytes+start] = '\0';
    received_packet->size = rxBytes+start;
    return rxBytes;
}

bool validChksum(const char * cbuf, int start, int len)
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

int keypad_write(const uart_port_t uart_n, const SendPacket &pkt_to_send)
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