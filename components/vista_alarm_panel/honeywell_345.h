// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf.
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once
#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#ifdef CC1101_RECEIVER

#include <cstdint>
#include <cstdio>
#include "driver/rmt_rx.h"

// ---------------------------------------------------------------------------
// Honeywell 5800-series 345 MHz wireless sensor packet decoder.
//
// Takes an array of RMT pulse-width symbols captured from CC1101 GDO0 and
// decodes them into the fields required by VistaBus::sendRFmsg():
//   serial  — 20-bit device serial number matching the panel's enrollment
//   status  — ECP FB status byte (loop bits, lowbat, supervision)
//
// The decoder converts RMT symbol durations into a chip bitstream using an
// adaptive clock derived from the preamble, then performs a sliding-window
// software Manchester decode + CRC-16 validation.
//
// Protocol reference: rtl_433 src/devices/honeywell.c
//   Modulation: OOK_PULSE_MANCHESTER_ZEROBIT
//   Preamble:   12+ alternating chips (~136 µs/chip nominal)
//   Packet:     4 data bytes + 2 CRC bytes (48 bits; 96 chips)
//   Sync:       'E' nibble (1110) precedes payload — pattern 01 01 01 10
//   CRC:        CRC-16 (poly 0x8005 for standard, 0x8050 for 2GIG)
//
// Decoded packet byte layout (6 bytes after software Manchester decode):
//   Byte 0 [7:4] : 4-bit channel
//   Byte 0 [3:0] : device serial bits 19-16   )
//   Byte 1       : device serial bits 15-8      > 20-bit serial
//   Byte 2       : device serial bits 7-0       )
//   Byte 3       : event byte — bit layout:
//                    bit 7 : loop 1 (contact)  → ECP 0x80
//                    bit 6 : loop 4            → ECP 0x40
//                    bit 5 : loop 2            → ECP 0x20
//                            Note: many sensors set bit 7 + bit 5 during a loop 2 
//                            fault; the real 5881 receiver may strip bit 5.
//                    bit 4 : loop 3            → ECP 0x10
//                    bit 3 : battery_low       → ECP 0x02
//                    bit 2 : heartbeat         → ECP 0x04
//                    bit 1 : (reserved)
//                    bit 0 : (reserved)
//   Bytes 4-5    : CRC-16
//
// ECP FB status byte bit layout (as used by dispatch_extFB / on_rf_zone_packet):
//   bit 7 (0x80) — loop 1 fault
//   bit 6 (0x40) — loop 4 fault
//   bit 5 (0x20) — loop 2 fault
//   bit 4 (0x10) — loop 3 fault
//   bit 2 (0x04) — supervision / heartbeat
//   bit 1 (0x02) — low battery
// ---------------------------------------------------------------------------

struct HoneywellPacket {
    uint32_t serial;      // 20-bit device serial number (max 0xFFFFF = 1,048,575)
    uint8_t  ecp_status;  // translated ECP FB status byte
    bool     heartbeat;   // true if this is a supervision / heartbeat packet
    bool     valid;       // false if no valid packet was found in the chip buffer
};

// Scan an array of RMT pulse-width symbols for a Honeywell 5800-series packet.
// Converts RMT durations to a chip bitstream using an adaptive preamble clock,
// then performs a sliding-window software Manchester decode and CRC-16 validation.
// rssi is included only for diagnostic logging.
// Returns a HoneywellPacket with valid=false if no packet was found.
HoneywellPacket honeywell345_parse(const rmt_symbol_word_t *symbols, size_t num_symbols);

#endif // CC1101_RECEIVER
