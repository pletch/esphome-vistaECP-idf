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

// ===========================================================================
// Runtime tunables
// ---------------------------------------------------------------------------
// Behavioural parameters consolidated here so they can be reviewed and adjusted
// in one place.  Hardware register definitions and protocol bit layouts remain
// in their domain headers (cc1101.h, honeywell_345.h, etc.).
// ===========================================================================

// --- FreeRTOS queue depths ---------------------------------------------------
inline constexpr uint16_t kReceiveQueueDepth    = 15;  // decoded panel frames awaiting dispatch
inline constexpr uint16_t kSendQueueDepth       = 8;   // outbound commands awaiting transmission
inline constexpr uint16_t kDeviceMsgQueueDepth  = 8;   // pending expander/RF responses for next F1 poll
inline constexpr uint16_t kRfDirectQueueDepth   = 8;   // CC1101 → HA fast path

// --- FreeRTOS task stack sizes (bytes) --------------------------------------
inline constexpr uint16_t kRfDirectTaskStackSize  = 4096; // ZoneManager publish chain
inline constexpr uint16_t kProcessRxTaskStackSize = 4096; // PacketDispatcher dispatch chain
inline constexpr uint16_t kCc1101RxTaskStackSize  = 5120; // RMT decode + Honeywell parser

// --- CC1101 receiver health watchdog ----------------------------------------
// Tier 1: cheap MARCSTATE register check cadence.  Also breaks the rx_task's
// notification block if GDO0 stops firing entirely.
inline constexpr uint32_t kCc1101HealthCheckPeriodMs = 60'000;
// Tier 2: full radio_.begin() re-init if no valid packet has been decoded in
// this window.  Sized to comfortably exceed Honeywell sensor heartbeat
// intervals (60–90 min).
inline constexpr int64_t  kCc1101PacketWatchdogUs    = 90LL * 60 * 1000 * 1000;

// --- CC1101 RMT receive capture ---------------------------------------------
// Glitch filter: drop pulses shorter than this (Honeywell chips ≈ 136 µs).
inline constexpr uint32_t kCc1101RmtSignalMinNs   = 3'000;
// End-of-frame: terminate capture after this much idle (inter-burst gap ≥ 30 ms).
inline constexpr uint32_t kCc1101RmtSignalMaxNs   = 2'000'000;
// RMT capture buffer size (symbols).  Larger on chips with DMA-capable RMT.
#ifdef SOC_RMT_SUPPORT_DMA
inline constexpr size_t   kCc1101RmtSymbols       = 512;
#else
inline constexpr size_t   kCc1101RmtSymbols       = 128;
#endif

// --- CC1101 / RF de-duplication ---------------------------------------------
// Ring buffer of recently seen (serial, status) packets.
inline constexpr uint8_t  kDedupeHistorySize       = 16;
// Burst suppressor: any retransmission of the same serial within this window
// is dropped, regardless of status byte.  Catches interleaved fault/clear
// packets that some sensors emit during state transitions.
inline constexpr int64_t  kDedupeBurstWindowUs     = 300'000;
// Long-range exact-match suppressor: same (serial, status) seen within this
// window is dropped.
inline constexpr int64_t  kDedupeLongRangeWindowUs = 2'500'000;

// --- Direct/ECP path coherency ----------------------------------------------
// When ZoneManager::on_rf_direct() runs, ECP-path zone updates for the same
// serial are suppressed for this long so the panel's later FB echo can't
// overwrite the authoritative direct-path state.
inline constexpr int64_t  kDirectSuppressUs        = 3'000'000LL;

// --- RSSI cache --------------------------------------------------------------
// Number of (serial, rssi) entries retained for use by the rf_messages text
// sensor.  8 is enough for typical home deployments without rapid eviction.
inline constexpr uint8_t  kRssiCacheSize           = 8;
