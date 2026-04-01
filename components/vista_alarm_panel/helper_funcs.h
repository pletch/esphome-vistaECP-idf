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

bool valid_chksum(const char * cbuf, int start, int len);
bool valid_chksum_two(const char * cbuf, int start, int len);  //two's complement
bool isInt(std::string s, int base);
int toDec(int n);
int toInt(std::string s, int base);
bool areEqual(char *a1, char *a2, uint8_t len);