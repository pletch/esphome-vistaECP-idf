// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#include "helper_funcs.h"

// Sum bytes cbuf[start..len) and return true if the total is 0 modulo 256.
// The ECP protocol appends a checksum byte such that the whole frame (data +
// checksum) sums to exactly 0x00 mod 256, so a zero sum means the frame is valid.
bool valid_chksum(const char *cbuf, int start, int len) {
  // Loop counter must be int: a uint8_t counter never reaches a len of 256
  // or more and would spin forever.
  uint16_t chksum = 0;
  for (int x = start; x < len; x++) {
    chksum += static_cast<uint8_t>(cbuf[x]);
  }
  return (chksum % 256) == 0;
}

// Calculate a two's-complement checksum over cbuf[start..start+len).
// ~sum + 1 produces the byte that, when appended to the frame, makes the
// entire frame sum to 0x00 modulo 256.
uint8_t calc_chksum_two(const char *cbuf, int start, int len) {
  // 'start' is honoured: the loop previously began at 0 regardless, which was
  // harmless only because every caller passes start == 0.
  uint8_t chksum = 0;
  for (int x = start; x < start + len; x++) {
    chksum += static_cast<uint8_t>(cbuf[x]);
  }
  chksum = ~chksum + 1;
  return chksum;
}

// Validate a frame whose final byte is a two's-complement checksum.
// Sums the body bytes cbuf[start..len-1) and checks whether ~sum+1 equals
// the stored checksum at cbuf[len-1].
bool valid_chksum_two(const char *cbuf, int start, int len)  // 2's complement negation
{
  // Both sides are compared as uint8_t.  Without the casts the comparison
  // relied on 'char' being signed, which is true on Xtensa but not on the
  // RISC-V ESP32 targets (C3/C6/H2/P4) where GCC defaults char to unsigned —
  // there, every checksum with the high bit set would have been rejected.
  uint8_t sum = 0;
  for (int x = start; x < len - 1; x++) {
    sum += static_cast<uint8_t>(cbuf[x]);
  }
  return static_cast<uint8_t>(~sum + 1) == static_cast<uint8_t>(cbuf[len - 1]);
}

// Return true if s is a non-empty string with no leading whitespace that can
// be fully parsed as an integer in the given base.  strtol() sets *p to the
// first character it could not convert; if *p == '\0' the entire string was valid.
bool isInt(const std::string &s, int base) {
  if (s.empty() || std::isspace(s[0]))
    return false;
  char *p;
  strtol(s.c_str(), &p, base);
  return (*p == 0);
}

// Convert a 12-bit BCD value packed into an int to a plain decimal integer.
// Bits [15:8] hold the hundreds digit, bits [7:4] hold the tens digit, and
// bits [3:0] hold the units digit.
// Example: n = 0x0042 → (0)*100 + (4)*10 + 2 = 42.
int toDec(int n) { return ((n >> 8) * 100) + (((n & 0xFF) >> 4) * 10) + (n & 0x0F); }

// Parse a string as an integer in the given base.
// Returns 0 for empty or whitespace-only strings.
int toInt(const std::string &s, int base) {
  if (s.empty() || std::isspace(s[0]))
    return 0;
  char *p;
  int li = strtol(s.c_str(), &p, base);
  return li;
}

// Return true if the first 'len' bytes of a1 and a2 are identical.
// Short-circuits on the first mismatch for efficiency.
bool areEqual(char *a1, char *a2, uint8_t len) {
  for (int x = 0; x < len; x++) {
    if (a1[x] != a2[x])
      return false;
  }
  return true;
}
