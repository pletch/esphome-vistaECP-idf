// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include "helper_structs.h"  // StatusFlags, LightStates, Partition, PartitionState
#include "helper_enums.h"    // SysState
#include "zone_manager.h"    // ZoneManager, ZoneManager::Zone
#include "esphome/core/log.h"
#include <vector>
#include <cstdint>
#include <cstddef>  // size_t

// Forward-declare ESPHome sensor base interfaces so this header does not
// pull in the full ESPHome component tree. Callers that actually register
// sensors will include vistaalarm.h (which defines the concrete types).

namespace esphome::alarm_panel {
class VistaEcpBinarySensor;
class VistaEcpTextSensor;

// -----------------------------------------------------------------------
// PartitionManager
//
// Owns all per-partition configuration, sensor registration, and the
// partition state machine (system state + light states).
//
// Lifecycle:
//   1. Call add_partition() once per configured partition.
//   2. Call initialize_sensor_vectors() after all partitions are added.
//   3. Call register_*() for each sensor during ESPHome setup.
//   4. Call process_status_flags() from PacketDispatcher on every F7
//      (or assembled legacy SE equivalent).
//   5. tick_battery_decay() is available for a sensor-refresh loop but is
//      intentionally not wired up — see its declaration below.
// -----------------------------------------------------------------------
class PartitionManager {
 public:
  // -------------------------------------------------------------------
  // Setup — must be called before any other method
  // -------------------------------------------------------------------

  // Register a partition with its assigned keypad address.
  // Call once per partition, before initialize_sensor_vectors().
  void add_partition(uint8_t partition_id, uint8_t keypad_addr);

  // Allocate internal sensor-pointer vectors to match the number of
  // partitions registered via add_partition(). Must be called after
  // all add_partition() calls and before any register_*() call.
  void initialize_sensor_vectors();

  // -------------------------------------------------------------------
  // Sensor registration
  // -------------------------------------------------------------------

  // Text sensors per partition.
  // Valid type strings: "SYSTEM_STATUS", "LINE1", "LINE2", "BEEPS".
  void register_text_sensor(VistaEcpTextSensor *sensor, uint8_t partition_number, const char *type);

  // Binary (on/off) sensors per partition.
  // Valid type strings: "READY", "TROUBLE", "BYPASS", "ARMED",
  //   "ARMED_AWAY", "ARMED_STAY", "ARMED_INSTANT", "ARMED_NIGHT",
  //   "CHIME", "ALARM", "FIRE".
  void register_status_sensor(VistaEcpBinarySensor *sensor, uint8_t partition_number, const char *type);

  // System-wide (non-partition) binary sensors for AC power and
  // low battery. These are registered once without a partition number.
  void register_ac(VistaEcpBinarySensor *sensor) { ac_sensor_ = sensor; }
  void register_bat(VistaEcpBinarySensor *sensor) { bat_sensor_ = sensor; }

  // -------------------------------------------------------------------
  // Core update path
  // -------------------------------------------------------------------

  // Called by PacketDispatcher on every decoded F7 packet (or
  // equivalent legacy SE assembled frame). Drives the full state
  // machine: decodes light/system states, notifies zone manager of
  // zone-level events, and publishes all changed sensors.
  //
  // zones  — ZoneManager to apply zone check/fault/bypass events to.
  // ttl    — microsecond TTL for stale zone-open/check timeout
  //          (passed through to ZoneManager::refresh).
  //
  // Returns the internal partition index (kpi) that was updated,
  // or -1 if the flags did not match any known partition.
  int process_status_flags(const StatusFlags &flags, ZoneManager &zones, int64_t ttl);

  // -------------------------------------------------------------------
  // Incremental updates from other packet types
  // -------------------------------------------------------------------

  // Called by PacketDispatcher when an F6 keypad-ACK packet arrives.
  // Advances the sequence counter for the matching partition.
  void on_keypad_ack(uint8_t raw_f6_addr_byte);

  // -------------------------------------------------------------------
  // Battery decay
  // -------------------------------------------------------------------

  // Clears the low-battery flag once the event has aged past ttl
  // microseconds; the change reaches bat_sensor_ on the next
  // publish_light_states() comparison.
  //
  // NOT currently called by anything — see the note in
  // VistaESPHome::update().  Wiring it to the zone ttl would flap the
  // sensor against the panel's rotating low-battery system message.
  void tick_battery_decay(int64_t ttl);

  // -------------------------------------------------------------------
  // Read accessors used by CommandWriter and VistaESPHome
  // -------------------------------------------------------------------

  // Fills keypad_addr and sequence for the given logical partition id.
  // Returns false if the partition is not known.
  bool get_partition_info(int partition_id, uint8_t &keypad_addr, uint8_t &sequence) const;

  // Returns true if the most recently published state for the given
  // partition has the armed flag set.
  bool is_armed(int partition_id) const;

  // Read-only access to the partition list (used by PacketDispatcher
  // for keypad-address lookups and by VistaESPHome for iteration).
  const std::vector<Partition> &partitions() const { return partitions_; }

  // Returns true if the most recently decoded F7 packet had the
  // program-mode bit set.  Used by the receive task to suppress AUI
  // writes during panel programming.
  bool in_program_mode() const { return last_program_mode_; }

  void publish_initial_states();

 private:
  // -------------------------------------------------------------------
  // Internal per-partition sensor-pointer bundles
  // -------------------------------------------------------------------

  struct TextSensors {
    VistaEcpTextSensor *system_status{nullptr};
    VistaEcpTextSensor *line1{nullptr};
    VistaEcpTextSensor *line2{nullptr};
    VistaEcpTextSensor *beeps{nullptr};
  };

  struct StatusSensors {
    VistaEcpBinarySensor *rdy{nullptr};   // Ready
    VistaEcpBinarySensor *trbl{nullptr};  // Trouble
    VistaEcpBinarySensor *byp{nullptr};   // Bypass
    VistaEcpBinarySensor *arm{nullptr};   // Armed (any)
    VistaEcpBinarySensor *arma{nullptr};  // Armed Away
    VistaEcpBinarySensor *arms{nullptr};  // Armed Stay
    VistaEcpBinarySensor *armi{nullptr};  // Armed Instant
    VistaEcpBinarySensor *armn{nullptr};  // Armed Night
    VistaEcpBinarySensor *chm{nullptr};   // Chime
    VistaEcpBinarySensor *alm{nullptr};   // Alarm
    VistaEcpBinarySensor *fire{nullptr};  // Fire
  };

  // -------------------------------------------------------------------
  // Private helpers called only from process_status_flags()
  // -------------------------------------------------------------------

  // Derive the full LightStates from a decoded StatusFlags packet.
  // Pure function — no side effects, safe to call from tests.
  LightStates decode_light_states_(const StatusFlags &flags) const;

  // Derive the SysState from already-decoded flags and lights.
  // Pure function — no side effects, safe to call from tests.
  SysState decode_system_state_(const StatusFlags &flags, const LightStates &lights) const;

  // Publish a system state string change to the system_status text sensor.
  void publish_system_state_(size_t kpi, SysState state);

  // Publish all binary light-state sensors that have changed.
  void publish_light_states_(size_t kpi, const LightStates &current, const LightStates &previous, bool force,
                             bool include_armed_states);

  // Format and publish the two keypad display lines, inserting a
  // cursor-position bracket if promptPos > 0.
  void update_display_lines_(size_t kpi, const StatusFlags &flags);

  // Resolve a logical partition id to its index in partitions_ (kpi).
  // Returns -1 if no partition with that id is configured.
  //
  // The sensor vectors are indexed by kpi, NOT by (partition_id - 1):
  // those two only coincide when partitions happen to be registered in
  // the order 1, 2, 3, ….  A single-partition install using partition 2,
  // or a two-partition install using 1 and 3, diverges — and the
  // publish helpers indexed past the end of the vectors as a result.
  int index_for_partition_(uint8_t partition_id) const;

  // -------------------------------------------------------------------
  // Member data
  // -------------------------------------------------------------------

  // Partition configuration and rolling state. Indexed by kpi (0-based).
  std::vector<Partition> partitions_;

  // Per-partition sensor pointers. Parallel to partitions_ after
  // initialize_sensor_vectors() is called.
  std::vector<TextSensors> text_sensors_;
  std::vector<StatusSensors> status_sensors_;

  // System-wide sensors (not per-partition).
  VistaEcpBinarySensor *ac_sensor_{nullptr};
  VistaEcpBinarySensor *bat_sensor_{nullptr};

  // Battery state is managed here rather than inside LightStates
  // because it has independent decay timing separate from the F7 cycle.
  bool current_bat_state_{false};
  int64_t low_battery_time_{0};

  // Timestamp of the last force-refresh cycle (every 5 minutes).
  int64_t last_force_refresh_{0};

  // Cached program-mode flag from the most recently decoded F7 packet.
  // Exposed via in_program_mode() so the receive task can suppress AUI
  // writes without accessing StatusFlags directly.
  bool last_program_mode_{false};
};

}  // namespace esphome::alarm_panel
