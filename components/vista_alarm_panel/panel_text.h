#pragma once

// EN
// The six legacy zone-string externs (FAULT, BYPAS, ALARM, FIRE, CHECK, TRBL)
// have been removed.  They were declared here but never defined anywhere, which
// would produce a linker error if referenced.  Zone status is now communicated
// via flag-based text in ZoneManager::publish_zone() rather than string lookups.

// Looks for the <space>*<space> found in the "Hit * to view messages".
extern const char *HITSTAR;

// System status strings published to the Home Assistant alarm panel sensor.
extern const char *STATUS_ARMED;
extern const char *STATUS_STAY;
extern const char *STATUS_NIGHT;
extern const char *STATUS_OFF;
extern const char *STATUS_ONLINE;
extern const char *STATUS_OFFLINE;
extern const char *STATUS_TRIGGERED;
extern const char *STATUS_READY;
extern const char *STATUS_ARMING;
extern const char *STATUS_PENDING;

// The default HA alarm panel card expects "unavailable" instead of "Not_Ready"
// when the system cannot be armed.  Swap the definition in panel_text.cpp if
// your dashboard uses that convention.
extern const char *STATUS_NOT_READY;

extern const char *MSG_ZONE_BYPASS;
extern const char *MSG_ARMED_BYPASS;
extern const char *MSG_NO_ENTRY_DELAY;
extern const char *MSG_NONE;
