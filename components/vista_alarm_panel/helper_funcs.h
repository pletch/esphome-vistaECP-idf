// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

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

// ECP bus checksum validation.
// Sums bytes cbuf[start..len) and returns true if the result is divisible by
// 256 (i.e. the sum wraps to 0x00 modulo 256).  The ECP protocol appends a
// checksum byte such that the whole frame including the checksum sums to zero.
bool valid_chksum(const char *cbuf, int start, int len);

// Compute a two's-complement checksum over cbuf[start..start+len).
// Returns ~(sum of bytes) + 1, which when appended to a frame makes the full
// frame sum to 0x00 modulo 256.
uint8_t calc_chksum_two(const char *cbuf, int start, int len);

// Validate a frame whose last byte is a two's-complement checksum.
// Sums bytes cbuf[start..len-1) and checks whether ~sum+1 equals cbuf[len-1].
bool valid_chksum_two(const char *cbuf, int start, int len);

// Returns true if string s represents a valid integer in the given base.
// Rejects empty strings and strings with leading whitespace.
bool isInt(const std::string &s, int base);

// Convert a 12-bit BCD-encoded integer (stored as a plain int) to decimal.
// Layout: bits [15:8] → hundreds digit, bits [7:4] → tens digit, bits [3:0] → units digit.
// Example: 0x0123 → (1*100) + (2*10) + 3 = 123.
int toDec(int n);

// Parse a string as an integer in the given base.
// Returns 0 for empty or whitespace-only strings; otherwise equivalent to strtol().
int toInt(const std::string &s, int base);

// Return true if the first 'len' bytes of a1 and a2 are identical.
// Equivalent to memcmp(a1, a2, len) == 0 but avoids a dependency on <string.h>
// at call sites that only need equality (not ordering).
bool areEqual(char *a1, char *a2, uint8_t len);
