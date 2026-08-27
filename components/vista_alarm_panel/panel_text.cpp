// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

// EN
#include "panel_text.h"

// Looks for the <space>*<space> found in the "Hit * to view messages".
#ifndef LEGACY_SE_PROTOCOL
const char *HITSTAR = " * ";
#else
const char *HITSTAR = "Press *";
#endif

// messages to display to home assistant

const char *STATUS_ARMED = "Armed_Away";
const char *STATUS_STAY = "Armed_Stay";
const char *STATUS_NIGHT = "Armed_Night";
const char *STATUS_OFF = "Disarmed";
const char *STATUS_ONLINE = "Online";
const char *STATUS_OFFLINE = "Offline";
const char *STATUS_TRIGGERED = "Triggered";
const char *STATUS_READY = "Ready";
const char *STATUS_ARMING = "Arming";
const char *STATUS_PENDING = "Pending";

// the default ha alarm panel card likes to see "unavailable" instead of not_ready when the system can't be armed
const char *STATUS_NOT_READY = "Not_Ready";
// const char * STATUS_NOT_READY = "unavailable";
const char *MSG_ZONE_BYPASS = "Zone_Bypass_Entered";
const char *MSG_ARMED_BYPASS = "Armed_Custom_Bypass";
const char *MSG_NO_ENTRY_DELAY = "No_Entry_Delay";
const char *MSG_NONE = "No_Messages";
