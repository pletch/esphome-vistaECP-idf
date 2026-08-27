// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include <cstdint>

enum class SysState : uint8_t {
  OFFLINE,
  ARMED_AWAY,
  ARMED_STAY,
  BYPASS,
  AC,
  CHIME,
  BATTERY,
  CHECK,
  ARMED_NIGHT,
  DISARMED,
  TRIGGERED,
  UNAVAILABLE,
  TROUBLE,
  ALARM,
  FIRE,
  INSTANT,
  READY,
  ARMED,
  ARMING
};

enum class PacketType : uint8_t {
  UNSPECIFIED = 0x00,
  LEGACY_PROTOCOL = 0xDD,
  CHKSUM_FAIL = 0xCF,
  AUI = 0xF2,
  KEYPAD_ACK = 0xF6,
  KEYPAD = 0xF7,
  LONG_RANGE_RADIO = 0xF9,
  EXPANDER = 0xFA,
  RF_RECEIVER = 0xFB
};
