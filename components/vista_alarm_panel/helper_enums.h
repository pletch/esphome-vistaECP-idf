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