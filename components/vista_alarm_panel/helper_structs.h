#pragma once

#include <string>
#include "helper_enums.h"

struct ReceivedPacket  
{
    int type; //0 = yellow wire, 1 = green wire
    int source{0};
    char payload[48];
    int size; 
};

struct SendPacket  
{
    int type{-1}; //0 = hex, 1 = text, 2 = no_ack_expected
    char payload[24];
    int keypadaddress;
    int size;
    char sequence;
};

struct GpioTaskArgs  
{
    TaskHandle_t task_handle;
    int pin;
};

struct DeviceMsg
{
    uint8_t address{255};
    uint32_t source{0}; //either zone or serial
    uint8_t msg{0};
};

struct EmulatedExpander  
{   
    uint8_t address{0};
    char fault_NO_Bits{0};
    char fault_NC_Bits{0xFF};
};

struct StatusFlags
{
    uint8_t beeps {0}; 
    bool armed_stay {false};
    bool armed_away {false};
    bool night {false};
    bool instant {false};
    bool chime {false};
    bool ac_power {false};
    bool ready {false};
    bool program_mode {false};
    bool zone_bypass {false};
    bool zone_alarm {false};
    bool alarm {false};
    bool check {false};
    bool system_flag {false};
    bool low_battery {false};
    bool system_trouble {false};
    bool fire {false};
    bool fire_zone {false};
    bool backlight {false};
    bool armed {false};
    bool away {false};
    bool bypass {false};
    bool in_alarm {false};
    bool fault {false};
    bool panicAlarm {false};
    char keypad[4] {};
    uint8_t partition {0};
    int zone {0};
    char prompt1[32] {0};
    char prompt2[32] {0};
    char promptPos {0};
};

struct LrrStatusFlags
{
    int code {0};
    uint8_t qual {0};
    int data {0};
    uint8_t partition {0};
};

struct PanelClock
{
    int64_t next_sync {2*60*1000*1000}; //first sync 2 min after boot
    bool auto_sync {false};
};

struct AUIDevice
{
    uint8_t address {0};
    uint8_t sequence1 {0};
    uint8_t sequence2 {0x6F};
};

struct AUIRequest
{
    bool pending {false};
    int64_t time {0};
};

struct EmulatedRFReceiver  
{   
    uint8_t address{0};
};

struct LightStates
{
    bool away {false};
    bool stay {false};
    bool night {false};
    bool instant {false};
    bool bypass {false};
    bool ready {false};
    bool ac {false};
    bool chime {false};
    bool bat {false};
    bool alarm {false};
    bool check {false};
    bool fire {false};
    bool trouble {false};
    bool armed {false};
};

struct PartitionState
{
    SysState previous_system_states;
    LightStates previous_light_states;
    int last_beeps {0};
    bool refresh_status {false};
};

struct Partition
{
    uint8_t partition {0};
    uint8_t assigned_keypad = {0};
    uint8_t keypad_sequence = {0};
    PartitionState partition_state;
};

