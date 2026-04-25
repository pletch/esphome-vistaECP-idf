// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

inline constexpr uint8_t kBitMaskByte1Beep = 0x07;
inline constexpr uint8_t kBitMaskByte1Night = 0x10;

inline constexpr uint8_t kBitMaskByte2ArmedStay = 0x80;
inline constexpr uint8_t kBitMaskByte2LowBat = 0x40;
inline constexpr uint8_t kBitMaskByte2AlarmZone = 0x20;
inline constexpr uint8_t kBitMaskByte2Ready = 0x10;
inline constexpr uint8_t kBitMaskByte2Unknown = 0x08;
inline constexpr uint8_t kBitMaskByte2SystemFlag = 0x04;
inline constexpr uint8_t kBitMaskByte2CheckFlag = 0x02;
inline constexpr uint8_t kBitMaskByte2Fire = 0x01;

inline constexpr uint8_t kBitMaskByte3Instant = 0x80;
inline constexpr uint8_t kBitMaskByte3Program = 0x40;
inline constexpr uint8_t kBitMaskByte3ChimeMode = 0x20;
inline constexpr uint8_t kBitMaskByte3Bypass = 0x10;
inline constexpr uint8_t kBitMaskByte3ACPower = 0x08;
inline constexpr uint8_t kBitMaskByte3ArmedAway = 0x04;
inline constexpr uint8_t kBitMaskByte3ZoneAlarm = 0x02;
inline constexpr uint8_t kBitMaskByte3InAlarm = 0x01;

inline constexpr uint8_t kF6AckMessageLength = 4;
inline constexpr uint8_t kF7MessageLength = 48;
inline constexpr uint8_t kF9MessageLength = 12;
inline constexpr uint8_t kF9ExtMessageLength = 8;
inline constexpr uint8_t kRFZoneMessageLength = 7;
inline constexpr uint8_t kFAMessageLength = 6;
inline constexpr uint8_t kFALegacyMessageLength = 5;
inline constexpr uint8_t kFBMessageLength = 5;

inline constexpr uint8_t kRXBufSize = 128;
inline constexpr uint16_t kUartRxTxTaskStackSize = 4096;
inline constexpr uint16_t kUartMonitorTaskStackSize = 3072;
inline constexpr uint8_t kUartDelay = 15;

inline constexpr uint16_t kPulseCyclePeriod = 550; // maximum period of long low pulse cycle on yellow wire in ms. sets maximum delay of rx_tx_task looping.
                                            // Vista-20p pulse cycle is 330 ms but older panel such as 4140XMPT2 cycle is 525 ms.

// ECP-bus baud rates.  Standard ECP framing is 4800 8E2; the 2400 rate is used
// for SE-series legacy packets and for 2400-baud FA/FB expander transmissions.
inline constexpr uint32_t kEcpBaudStandard = 4800;
inline constexpr uint32_t kEcpBaudLegacy   = 2400;

// Timeout between a mark_pulse and the expected F6 ACK from the panel.
inline constexpr int64_t kPulseAckTimeoutUs = 1'200'000;  // 1.2 s

// Panel "baud switch" mark: panel holds the yellow wire high for ~6 ms when it
// is about to transmit at 2400 baud.  The protocol code detects the mark by
// checking that the high period falls within this window.
inline constexpr int64_t kBaudSwitchMarkMinUs = 5'700;
inline constexpr int64_t kBaudSwitchMarkMaxUs = 6'300;

// SE write-window bounds: the keypad may transmit a character when the yellow
// wire high-time falls outside the [min,max] range (i.e. the panel has opened
// the bus for keypad input).
inline constexpr int64_t kSEWriteWindowBelowUs = 60'000;
inline constexpr int64_t kSEWriteWindowAboveUs = 150'000;

// RF supervision heartbeat scheduling (all in microseconds).
inline constexpr int64_t kUsPerMinute                    = 60'000'000LL;
inline constexpr int64_t kRfHeartbeatInitialMinMinutes   = 1;   // earliest first heartbeat
inline constexpr int64_t kRfHeartbeatInitialJitterMinutes = 10; // added random jitter
inline constexpr int64_t kRfHeartbeatPeriodMinMinutes    = 70;  // baseline interval
inline constexpr int64_t kRfHeartbeatPeriodJitterMinutes = 20;  // added random jitter
