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
inline constexpr uint8_t kFALegacyMessageLength = 4;
inline constexpr uint8_t kFBMessageLength = 5;

inline constexpr uint8_t kRXBufSize = 128;
inline constexpr uint16_t kUartRxTxTaskStackSize = 4096;
inline constexpr uint16_t kUartMonitorTaskStackSize = 3072;
inline constexpr uint8_t kUartDelay = 15;

inline constexpr uint16_t kPulseCyclePeriod = 550; // maximum period of long low pulse cycle on yellow wire in ms. sets maximum delay of rx_tx_task looping.
                                            // Vista-20p pulse cycle is 330 ms but older panel such as 4140XMPT2 cycle is 525 ms.
