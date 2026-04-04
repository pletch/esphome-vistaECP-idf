#pragma once

#include "helper_structs.h"
#include "helper_funcs.h"
#include "constants.h"
#include "vistabus.h"

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
        void dispatchF2();
        void dispatchF6(const SendPacket &pkt_to_send);
        void dispatchF7();
        void dispatchF8();
        void dispatchF9();
        void dispatchFB();
        void dispatch_legacyStatusPacket(uint8_t header);
        void dispatchDebug(uint8_t header, uint8_t type);
        void track_write_attempts();
        void dispatch_extF6(uint32_t val, uint8_t header);
        void dispatch_extF8(uint32_t val, uint8_t header);
        void dispatch_extF9(uint32_t val, uint8_t header);
        void dispatch_extFA(uint32_t val, uint8_t header, bool legacy);
        void dispatch_extFB(uint32_t val, uint8_t header); 

    protected:
        VistaBus &vistabus_;
        const char* const TAG = "ecp_proto";
        void mark_pulse(int uartNum, uint8_t address);
        static void gpio_isr_handler(void * args);
        static void timer_isr_handler(void * task_handle);

    private:
        void quick_decodeF9(const char * cbuf);
        void quick_decodeFB(const char * cbuf);
        uint8_t ack_failures = 0;
        
};