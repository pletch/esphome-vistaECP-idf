#pragma once

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "hal/uart_ll.h"
#include "helperstructs.h"

bool mark_pulse(int uartNum, uint8_t address);
void gpio_isr_handler(void * args);
void timer_isr_handler(void * task_handle);
int uart_read_bytes_event(uart_port_t uart_num, uint8_t * rxbuf, int len, int timeout, QueueHandle_t queue);
int get_Packet_event(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, int len, uart_port_t uart_num, int timeout, QueueHandle_t queue);
int get_Packet(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, int len, uart_port_t uart_num, int timeout);
bool validChksum(const char * cbuf, int start, int len);
int keypad_write(const uart_port_t uart_n, const SendPacket &pkt_to_send);