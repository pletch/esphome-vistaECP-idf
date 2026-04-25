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

enum class SysState : uint8_t
{
    Offline,
    ArmedAway,
    ArmedStay,
    Bypass,
    AC,
    Chime,
    Battery,
    Check,
    ArmedNight,
    Disarmed,
    Triggered,
    Unavailable,
    Trouble,
    Alarm,
    Fire,
    Instant,
    Ready,
    Armed,
    Arming
};

enum class PacketType : uint8_t
{
    Unspecified    = 0x00,
    LegacyProtocol = 0xDD,
    ChksumFail     = 0xCF,
    AUI            = 0xF2,
    KeypadAck      = 0xF6,
    Keypad         = 0xF7,
    LongRangeRadio = 0xF9,
    Expander       = 0xFA,
    RFReceiver     = 0xFB
};