#pragma once

#include "helper_structs.h"
#include "helper_funcs.h"
#include "constants.h"
#include "vistabus.h"

class VistaBus;

class VistaECP 
{
public:
    VistaECP(VistaBus& vistabus_);
    int64_t pulse_mark_time = 0;
    bool pulse_marked = false;
    bool req_to_send = false;
    bool is_2400 = false;
    bool legacy_programmode = false;
    int get_packet_event(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, int len, 
            uart_port_t uart_num, int timeout, QueueHandle_t queue);
    int get_packet(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, int len, 
            uart_port_t uart_num, int timeout);
    int keypad_write(const uart_port_t uart_n, const SendPacket &pkt_to_send);
    int uart_read_bytes_event(uart_port_t uart_num, uint8_t * rxbuf, int len, int timeout, QueueHandle_t queue);
    

protected:
    VistaBus &vistabus_;
    bool mark_pulse(int uartNum, uint8_t address);
    static void gpio_isr_handler(void * args);
    static void timer_isr_handler(void * task_handle);
    
};