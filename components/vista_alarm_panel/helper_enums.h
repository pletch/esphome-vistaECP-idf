// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

enum SysState
{
    kOffline,
    kArmedAway,
    kArmedStay,
    kBypass,
    kAC,
    kChime,
    kBattery,
    kCheck,
    kArmedNight,
    kDisarmed,
    kTriggered,
    kUnavailable,
    kTrouble,
    kAlarm,
    kFire,
    kInstant,
    kReady,
    kArmed,
    kArming
};

enum ReqStates  //ToDo:  verify if this is needed
{
    rsidle,
    rsopenzones,
    rsbypasszones,
    rszonecount,
    rspartitionlist,
    rspartitionid,
    rszoneinfo,
    rsicode,
    rsdate
};

enum PacketType
{
    kUnspecified = 0,
    kLegacyProtocol = 0xDD,
    kChksumFail = 0xCF,
    kAUI = 0xF2,
    kKeypadAck = 0xF6,
    kKeypad = 0xF7,
    kLongRangeRadio = 0xF9,
    kExpander = 0xFA,
    kRFReceiver = 0xFB
};