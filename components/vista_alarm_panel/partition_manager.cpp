// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#include "partition_manager.h"
#include "sensor_interfaces.h"
#include "panel_text.h"
#include "translation.h"
#include "constants.h"
#include <cstring>
#include <string>

namespace esphome::alarm_panel {
static constexpr const char *TAG = "vista-partition";

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void PartitionManager::add_partition(uint8_t partition_id, uint8_t keypad_addr) {
  Partition p;
  p.partition = partition_id;
  p.assigned_keypad = keypad_addr;
  p.keypad_sequence = (keypad_addr & 0x0F) | 0x10;
  partitions_.push_back(p);
}

void PartitionManager::initialize_sensor_vectors() {
  // Must be called after all add_partition() calls, before any registration.
  for (size_t i = 0; i < partitions_.size(); i++) {
    text_sensors_.push_back(TextSensors{});
    status_sensors_.push_back(StatusSensors{});
  }
}

int PartitionManager::index_for_partition_(uint8_t partition_id) const {
  for (size_t i = 0; i < partitions_.size(); i++) {
    if (partitions_[i].partition == partition_id)
      return static_cast<int>(i);
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Sensor registration
// ---------------------------------------------------------------------------

void PartitionManager::register_text_sensor(VistaEcpTextSensor *sensor, uint8_t partition_number, const char *type) {
  // Resolve to the vector index rather than assuming partition_number - 1.
  const int found = index_for_partition_(partition_number);
  if (found < 0) {
    ESP_LOGE(TAG, "No partition %d configured. Aborting %s registration.", partition_number, type);
    return;
  }

  const size_t idx = static_cast<size_t>(found);

  if (strcmp(type, "SYSTEM_STATUS") == 0) {
    text_sensors_[idx].system_status = sensor;
  } else if (strcmp(type, "LINE1") == 0) {
    text_sensors_[idx].line1 = sensor;
  } else if (strcmp(type, "LINE2") == 0) {
    text_sensors_[idx].line2 = sensor;
  } else if (strcmp(type, "BEEPS") == 0) {
    text_sensors_[idx].beeps = sensor;
  } else {
    ESP_LOGW(TAG, "Unknown text sensor type '%s' for partition %d.", type, partition_number);
  }

  ESP_LOGI(TAG, "Registered text sensor '%s' for partition %d.", type, partition_number);
}

void PartitionManager::register_status_sensor(VistaEcpBinarySensor *sensor, uint8_t partition_number,
                                              const char *type) {
  const int found = index_for_partition_(partition_number);
  if (found < 0) {
    ESP_LOGE(TAG, "No partition %d configured. Aborting %s registration.", partition_number, type);
    return;
  }

  const size_t idx = static_cast<size_t>(found);

  if (strcmp(type, "READY") == 0) {
    status_sensors_[idx].rdy = sensor;
  } else if (strcmp(type, "TROUBLE") == 0) {
    status_sensors_[idx].trbl = sensor;
  } else if (strcmp(type, "BYPASS") == 0) {
    status_sensors_[idx].byp = sensor;
  } else if (strcmp(type, "ARMED_AWAY") == 0) {
    status_sensors_[idx].arma = sensor;
  } else if (strcmp(type, "ARMED_STAY") == 0) {
    status_sensors_[idx].arms = sensor;
  } else if (strcmp(type, "ARMED_INSTANT") == 0) {
    status_sensors_[idx].armi = sensor;
  } else if (strcmp(type, "ARMED_NIGHT") == 0) {
    status_sensors_[idx].armn = sensor;
  } else if (strcmp(type, "ARMED") == 0) {
    status_sensors_[idx].arm = sensor;
  } else if (strcmp(type, "CHIME") == 0) {
    status_sensors_[idx].chm = sensor;
  } else if (strcmp(type, "ALARM") == 0) {
    status_sensors_[idx].alm = sensor;
  } else if (strcmp(type, "FIRE") == 0) {
    status_sensors_[idx].fire = sensor;
  } else {
    ESP_LOGW(TAG, "Unknown status sensor type '%s' for partition %d.", type, partition_number);
  }

  ESP_LOGI(TAG, "Registered status sensor '%s' for partition %d.", type, partition_number);
}

// ---------------------------------------------------------------------------
// Initial state broadcast
// ---------------------------------------------------------------------------

void PartitionManager::publish_initial_states() {
  const LightStates zero{};
  for (size_t kpi = 0; kpi < partitions_.size(); kpi++) {
    publish_system_state_(kpi, SysState::UNAVAILABLE);
    publish_light_states_(kpi, zero, zero, /*force=*/true, /*include_armed_states=*/true);
    if (text_sensors_[kpi].beeps != nullptr)
      text_sensors_[kpi].beeps->process("0");
  }

  if (ac_sensor_ != nullptr)
    ac_sensor_->process(false);
  if (bat_sensor_ != nullptr)
    bat_sensor_->process(false);
}

// ---------------------------------------------------------------------------
// Sequence tracking
// ---------------------------------------------------------------------------

void PartitionManager::on_keypad_ack(uint8_t raw_f6_addr_byte) {
  // The raw F6 address byte encodes the keypad in the lower nibble.
  const uint8_t kp_addr = (raw_f6_addr_byte & 0x0F) | 0x10;
  for (auto &p : partitions_) {
    if (kp_addr == p.assigned_keypad) {
      p.keypad_sequence += 0x40;
      break;
    }
  }
}

bool PartitionManager::get_partition_info(int partition_id, uint8_t &keypad_addr, uint8_t &sequence) const {
  for (const auto &p : partitions_) {
    if (p.partition == static_cast<uint8_t>(partition_id)) {
      keypad_addr = p.assigned_keypad;
      sequence = p.keypad_sequence;
      return true;
    }
  }
  return false;
}

bool PartitionManager::is_armed(int partition_id) const {
  for (const auto &p : partitions_) {
    if (p.partition == static_cast<uint8_t>(partition_id))
      return p.partition_state.previous_light_states.armed;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Core update path — called by PacketDispatcher for every F7 (or assembled
// legacy SE equivalent). Returns the kpi of the updated partition, or -1.
// ---------------------------------------------------------------------------

int PartitionManager::process_status_flags(const StatusFlags &flags, ZoneManager &zones, int64_t ttl) {
  if (flags.partition == 0) {
    ESP_LOGW(TAG, "No keypad associated with received partition. Skipping.");
    return -1;
  }

  // Locate the partition index.
  size_t kpi = 0;
  bool found = false;
  for (size_t i = 0; i < partitions_.size(); i++) {
    if (partitions_[i].partition == flags.partition) {
      kpi = i;
      found = true;
      break;
    }
  }
  if (!found) {
    ESP_LOGW(TAG, "Received flags for unknown partition %d.", flags.partition);
    return -1;
  }

  Partition &part = partitions_[kpi];

  // --- Periodic force-refresh ---
  const int64_t now = esp_timer_get_time();
  bool force_refresh = false;
  if (now - last_force_refresh_ > 300LL * 1000 * 1000) {
    force_refresh = true;
    last_force_refresh_ = now;
  }

  // Cache program_mode for in_program_mode() accessor used by the
  // receive task to suppress AUI writes during panel programming.
  last_program_mode_ = flags.program_mode;

  // --- Display lines and beeps ---
  update_display_lines_(kpi, flags);

  if (part.partition_state.last_beeps != flags.beeps && text_sensors_[kpi].beeps != nullptr) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%u", flags.beeps);
    text_sensors_[kpi].beeps->process(buf);
  }
  part.partition_state.last_beeps = flags.beeps;

  // --- Zone state updates via ZoneManager ---
  //
  // All mutation goes through ZoneManager's own accessors, each of
  // which takes zone_mutex_ internally.  Writing through a raw Zone*
  // returned by get_zone() would leave half of these updates outside
  // the lock that serialises zones_ against rf_direct_task.
  const uint8_t zone_no = static_cast<uint8_t>(flags.zone);

  // Check status
  if (flags.check) {
    zones.set_zone_check(zone_no, true);
    // Assign partition to zone if not yet known.
    zones.assign_partition(zone_no, part.partition);
    // Refresh the TTL timestamp every cycle the zone is still reported
    // checked — not just on the initial transition — so the zone does
    // not expire while the panel is actively reporting it.
    zones.touch_zone_time(zone_no);
  }

  // Fault status (only when no complicating flags are active)
  const bool armed = flags.instant || flags.armed_away || flags.armed_stay || flags.night;
  if (!flags.system_flag && !flags.check && !flags.bypass && !flags.alarm && !flags.program_mode && !armed) {
    zones.set_zone_open(zone_no, true);
    // Refresh the TTL timestamp every cycle the zone is still
    // reported faulted — not just on the initial transition.
    zones.touch_zone_time(zone_no);
  }

  // Bypass status
  if (!flags.system_flag && !flags.check && flags.bypass && !flags.alarm && !armed) {
    zones.set_zone_bypass(zone_no, true);
    zones.touch_zone_time(zone_no);
  }

  // --- Derive current light and system states ---
  const LightStates current = decode_light_states_(flags);
  const SysState sys = decode_system_state_(flags, current);

  // Refresh zone sensors (clears stale open/bypass/alarm states)
  zones.refresh(current, ttl);

  // --- Battery / low-battery time tracking ---
  if (flags.low_battery && (flags.system_flag || flags.check)) {
    current_bat_state_ = true;
    low_battery_time_ = now;
  }

  // --- Publish system state if changed ---
  if (flags.system_flag || current.ready || current.armed) {
    if (sys != part.partition_state.previous_system_states)
      publish_system_state_(kpi, sys);
    part.partition_state.previous_system_states = sys;
    part.partition_state.refresh_status = false;
  }

  // --- Publish light-state binary sensors ---
  const LightStates &prev = part.partition_state.previous_light_states;
  publish_light_states_(kpi, current, prev, force_refresh, flags.system_flag || current.ready || current.armed);

  part.partition_state.previous_light_states = current;

  return static_cast<int>(kpi);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

LightStates PartitionManager::decode_light_states_(const StatusFlags &flags) const {
  LightStates s;

  // Start from a known baseline each call — nothing carries over from last
  // tick except bat, which has its own decay path in refreshSensors.
  s.ac = flags.ac_power;
  s.ready = flags.ready;
  s.chime = flags.chime;
  s.bypass = flags.bypass;
  s.check = flags.check;
  s.instant = flags.instant;
  s.alarm = flags.alarm || flags.in_alarm;
  s.fire = flags.fire;

  if (flags.armed_away || flags.armed_stay) {
    s.armed = true;
    if (flags.night) {
      s.night = true;
      s.stay = true;
    } else if (flags.armed_away) {
      s.away = true;
    } else {
      s.stay = true;
    }
  }

  // Trouble is a derived flag: no AC, or battery issue, or device check.
  s.trouble = !s.ac || s.check;

  // Battery state has decay handled externally; preserve last known value
  // here so publish_light_states can compare consistently.
  // Callers set bat_ directly via set_bat_state() when low_battery_time expires.
  s.bat = current_bat_state_;

  return s;
}

SysState PartitionManager::decode_system_state_(const StatusFlags &flags, const LightStates &lights) const {
  if (flags.fire || flags.in_alarm)
    return SysState::TRIGGERED;

  if (lights.armed) {
    if (lights.night)
      return SysState::ARMED_NIGHT;
    if (lights.away)
      return SysState::ARMED_AWAY;
    return SysState::ARMED_STAY;
  }

  if (flags.ready)
    return SysState::DISARMED;

  return SysState::UNAVAILABLE;
}

void PartitionManager::publish_system_state_(size_t kpi, SysState state) {
  // kpi is already the vector index — see index_for_partition().
  if (kpi >= text_sensors_.size())
    return;

  VistaEcpTextSensor *sensor = text_sensors_[kpi].system_status;
  if (sensor == nullptr)
    return;

  switch (state) {
    case SysState::TRIGGERED:
      sensor->process(STATUS_TRIGGERED);
      break;
    case SysState::ARMED_AWAY:
      sensor->process(STATUS_ARMED);
      break;
    case SysState::ARMED_NIGHT:
      sensor->process(STATUS_NIGHT);
      break;
    case SysState::ARMED_STAY:
      sensor->process(STATUS_STAY);
      break;
    case SysState::DISARMED:
      sensor->process(STATUS_OFF);
      break;
    default:
      sensor->process(STATUS_NOT_READY);
      break;
  }
}

// Macro-like helper to reduce the sensor-publish repetition.
// Only fires when the value changed OR a force-refresh was requested.
#define PUBLISH_BIN(sensor_ptr, cur_val, prev_val, force) \
  do { \
    if (((cur_val) != (prev_val) || (force)) && (sensor_ptr) != nullptr) \
      (sensor_ptr)->process(cur_val); \
  } while (0)

void PartitionManager::publish_light_states_(size_t kpi, const LightStates &cur, const LightStates &prev, bool force,
                                             bool include_armed_states) {
  if (kpi >= status_sensors_.size())
    return;
  StatusSensors &ss = status_sensors_[kpi];

  PUBLISH_BIN(ss.fire, cur.fire, prev.fire, force);
  PUBLISH_BIN(ss.alm, cur.alarm, prev.alarm, force);
  PUBLISH_BIN(ss.trbl, cur.trouble, prev.trouble, force);
  PUBLISH_BIN(ss.chm, cur.chime, prev.chime, force);
  PUBLISH_BIN(ss.byp, cur.bypass, prev.bypass, force);
  PUBLISH_BIN(ss.rdy, cur.ready, prev.ready, force);

  // Armed-state sensors only update when we have system_flag or a state change
  if (include_armed_states) {
    PUBLISH_BIN(ss.arma, cur.away, prev.away, force);
    PUBLISH_BIN(ss.arms, cur.stay, prev.stay, force);
    PUBLISH_BIN(ss.armn, cur.night, prev.night, force);
    PUBLISH_BIN(ss.armi, cur.instant, prev.instant, force);
    PUBLISH_BIN(ss.arm, cur.armed, prev.armed, force);
  }

  // Bug fix from original: ac_sensor fires on AC change, not bat change.
  if ((cur.ac != prev.ac || force) && ac_sensor_ != nullptr)
    ac_sensor_->process(cur.ac);

  PUBLISH_BIN(bat_sensor_, cur.bat, prev.bat, force);
}

#undef PUBLISH_BIN

void PartitionManager::update_display_lines_(size_t kpi, const StatusFlags &flags) {
  // Bounds-check the partition sensor index before use.
  if (kpi >= text_sensors_.size())
    return;

  const uint8_t pos = flags.promptPos;
  std::string p1 = flags.prompt1;
  std::string p2 = flags.prompt2;

  // Insert cursor bracket at the cursor position reported by the panel.
  if (pos > 0) {
    char buf[10];
    std::string sub1, sub2;

    if (pos > 15) {
      // Cursor is on line 2
      const uint8_t line2_pos = pos - 16;
      if (line2_pos < p2.size()) {
        sub1 = p2.substr(0, line2_pos);
        if (line2_pos + 1 < p2.size())
          sub2 = p2.substr(line2_pos + 1);
        snprintf(buf, sizeof(buf), "[%c]", p2[line2_pos]);
        p2 = sub1 + std::string(buf) + sub2;
      }
    } else {
      // Cursor is on line 1
      if (pos < p1.size()) {
        sub1 = p1.substr(0, pos);
        if (pos + 1 < p1.size())
          sub2 = p1.substr(pos + 1);
        snprintf(buf, sizeof(buf), "[%c]", p1[pos]);
        p1 = sub1 + std::string(buf) + sub2;
      }
    }
  }

  if (text_sensors_[kpi].line1 != nullptr)
    text_sensors_[kpi].line1->process(p1);
  if (text_sensors_[kpi].line2 != nullptr)
    text_sensors_[kpi].line2->process(p2);
}

// ---------------------------------------------------------------------------
// Battery decay — called from the main sensor-refresh path (formerly
// refreshSensors). When the low-battery event has aged past TTL, clear it.
// ---------------------------------------------------------------------------

void PartitionManager::tick_battery_decay(int64_t ttl) {
  if (current_bat_state_ && (esp_timer_get_time() - low_battery_time_) > ttl)
    current_bat_state_ = false;
}

}  // namespace esphome::alarm_panel
