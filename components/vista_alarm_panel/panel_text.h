// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include "esphome/core/defines.h"

// EN
// Zone status is now communicated via flag-based text in
// ZoneManager::publish_zone() rather than string lookups.

// Looks for the <space>*<space> found in the "Hit * to view messages".
extern const char *const HITSTAR;

// System status strings published to the Home Assistant alarm panel sensor.
extern const char *const STATUS_ARMED;
extern const char *const STATUS_STAY;
extern const char *const STATUS_NIGHT;
extern const char *const STATUS_OFF;
extern const char *const STATUS_ONLINE;
extern const char *const STATUS_OFFLINE;
extern const char *const STATUS_TRIGGERED;
extern const char *const STATUS_READY;
extern const char *const STATUS_ARMING;
extern const char *const STATUS_PENDING;

// The default HA alarm panel card expects "unavailable" instead of "Not_Ready"
// when the system cannot be armed.  Swap the definition in panel_text.cpp if
// your dashboard uses that convention.
extern const char *const STATUS_NOT_READY;

extern const char *const MSG_ZONE_BYPASS;
extern const char *const MSG_ARMED_BYPASS;
extern const char *const MSG_NO_ENTRY_DELAY;
extern const char *const MSG_NONE;
