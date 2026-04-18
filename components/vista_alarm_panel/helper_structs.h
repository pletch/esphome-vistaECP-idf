// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "helper_enums.h"

struct ReceivedPacket  
{
    int type;           // 0 = yellow wire, 1 = green wire
    int source {0};
    char payload[48];
    int size; 
};

struct SendPacket  
{
    int type {-1};      // 0 = hex, 1 = text, 2 = no_ack_expected
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
    uint8_t  address {255};
    uint32_t source  {0};  // either zone number or RF serial
    uint8_t  msg     {0};
    int64_t  time    {0};
};

// Payload posted to rf_direct_queue by CC1101Receiver when a valid, non-duplicate,
// non-heartbeat RF packet is decoded.  Consumed by VistaESPHome::rf_direct_task()
// to publish zone state directly to Home Assistant without waiting for the ECP
// panel round-trip (F1 poll → FA/FB exchange → packet dispatcher).
struct RfDirectMsg
{
    uint32_t serial;      // 20-bit Honeywell sensor serial number
    uint8_t  ecp_status;  // status byte: loop bits [7,6,5,4], lowbat [1]
    int8_t   rssi;        // CC1101 RSSI at time of decode (dBm); 0 if unavailable
};

// Note: EmulatedExpander is defined as a nested struct inside VistaBus
// (vistabus.h) — the top-level definition that previously appeared here
// was a duplicate and has been removed.  All code should use
// VistaBus::EmulatedExpander.

struct StatusFlags
{
    uint8_t beeps         {0}; 
    bool armed_stay       {false};
    bool armed_away       {false};
    bool night            {false};
    bool instant          {false};
    bool chime            {false};
    bool ac_power         {false};
    bool ready            {false};
    bool program_mode     {false};
    bool zone_bypass      {false};
    bool zone_alarm       {false};
    bool alarm            {false};
    bool check            {false};
    bool system_flag      {false};
    bool low_battery      {false};
    bool system_trouble   {false};
    bool fire             {false};
    bool fire_zone        {false};
    bool backlight        {false};
    bool armed            {false};
    bool away             {false};
    bool bypass           {false};
    bool in_alarm         {false};
    bool fault            {false};
    bool panicAlarm       {false};
    char    keypad[4]     {};
    uint8_t partition     {0};
    int     zone          {0};
    char    prompt1[32]   {0};
    char    prompt2[32]   {0};
    char    promptPos     {0};
};

struct LrrStatusFlags
{
    int     code      {0};
    uint8_t qual      {0};
    int     data      {0};
    uint8_t partition {0};
};

struct PanelClock
{
    int64_t next_sync {2LL * 60 * 1000 * 1000}; // first sync 2 min after boot
    bool    auto_sync {false};
};

struct AUIDevice
{
    uint8_t address   {0};
    uint8_t sequence1 {0};
    uint8_t sequence2 {0x6F};
};

struct AUIRequest
{
    bool    pending {false};
    int64_t time    {0};
};

struct EmulatedRFReceiver  
{   
    uint8_t address {0};
};

struct LightStates
{
    bool away    {false};
    bool stay    {false};
    bool night   {false};
    bool instant {false};
    bool bypass  {false};
    bool ready   {false};
    bool ac      {false};
    bool chime   {false};
    bool bat     {false};
    bool alarm   {false};
    bool check   {false};
    bool fire    {false};
    bool trouble {false};
    bool armed   {false};
};

struct PartitionState
{
    SysState    previous_system_states;
    LightStates previous_light_states;
    int         last_beeps     {0};
    bool        refresh_status {false};
};

struct Partition
{
    uint8_t        partition       {0};
    uint8_t        assigned_keypad {0};
    uint8_t        keypad_sequence {0};
    PartitionState partition_state;
};
