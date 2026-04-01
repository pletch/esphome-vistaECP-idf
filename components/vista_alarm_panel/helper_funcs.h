#pragma once

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "hal/uart_ll.h"
#include <sys/time.h>
#include <vector>
#include <ctime>
#include "helper_structs.h"
#include "helper_enums.h"

bool mark_pulse(int uartNum, uint8_t address); // ToDo: move to protocol
void gpio_isr_handler(void * args); // ToDo: move to protocol
void timer_isr_handler(void * task_handle); // ToDo: move to protocol
int uart_read_bytes_event(uart_port_t uart_num, uint8_t * rxbuf, int len, int timeout, QueueHandle_t queue);  // ToDo: move to protocol
int get_packet_event(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, int len, uart_port_t uart_num, int timeout, QueueHandle_t queue); // ToDo: move to protocol
int get_packet(struct ReceivedPacket * received_packet, uint8_t * rxbuf, int start, int len, uart_port_t uart_num, int timeout); // ToDo: move to protocol
int keypad_write(const uart_port_t uart_n, const SendPacket &pkt_to_send);  // ToDo: move to protocol
bool valid_chksum(const char * cbuf, int start, int len);
bool valid_chksum_two(const char * cbuf, int start, int len);  //two's complement
bool isInt(std::string s, int base);
int toDec(int n);
int toInt(std::string s, int base);
bool areEqual(char *a1, char *a2, uint8_t len);