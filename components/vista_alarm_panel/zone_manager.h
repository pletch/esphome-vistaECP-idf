// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include "vista_bus.h"       // VistaBus (needed for send_emulated_fault,
                             //           handle_rf_heartbeats)
#include "helper_structs.h"  // LightStates (needed by refresh())
#include "constants.h"       // kRFZoneMessageLength
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <vector>
#include <string>
#include <cstdint>

// Forward-declare sensor interfaces to avoid pulling in the full ESPHome
// component tree into every translation unit that includes this header.
namespace esphome {
namespace alarm_panel {
class vistaECPBinarySensor;
class vistaECPTextSensor;

// -----------------------------------------------------------------------
// ZoneManager
//
// Owns the registration, state, and sensor publishing for all alarm zones
// (hardwired, wireless RF, and emulated).  It is the single source of
// truth for zone open/bypass/alarm/check/lowbat state.
//
// Responsibilities:
//   - Zone registration (binary sensor, text sensor, RF serial/loop).
//   - Per-zone state transitions: set_zone_*() methods called by
//     PartitionManager when it decodes F7 status flags.
//   - FA expander packet decode: on_expander_zone_packet().
//   - FB RF receiver packet decode: on_rf_zone_packet().
//   - Periodic state reconciliation against partition light flags: refresh().
//   - RF supervision heartbeat emission: handle_rf_heartbeats().
//   - Emulated zone fault forwarding to VistaBus.
//
// Does NOT own:
//   - The rf_messages text sensor (held by PacketDispatcher::Config and
//     passed the return value of on_rf_zone_packet()).
//   - Battery decay (belongs to PartitionManager — not zone-specific).
//   - The VistaBus instance (received by reference only where needed).
//
// Lifecycle:
//   1. Call register_zone() and/or register_zone_text() during setup for
//      every configured zone.
//   2. Optionally call register_zone_status_sensor() to publish the
//      combined zone-status string.
//   3. Call init_rf_heartbeat_timers() once at the start of the receive
//      task (after all zones are registered) to stagger initial heartbeats.
//   4. Call handle_rf_heartbeats() each iteration of the receive task loop.
//   5. Call refresh() from PartitionManager::process_status_flags() once
//      per F7 cycle, after individual set_zone_*() calls have been made.
//
// Thread safety:
//   on_rf_direct() is called from VistaESPHome::rf_direct_task().
//   All other methods are called from the processReceiveQueue task.
//   zone_mutex_ serialises access between the two tasks.
// -----------------------------------------------------------------------
class ZoneManager {
 public:
  ZoneManager();
  ~ZoneManager();

  // -------------------------------------------------------------------
  // Zone data
  //
  // Exposed publicly so that PartitionManager can update zone.partition
  // and zone.time directly after calling set_zone_check() and
  // set_zone_open() — matching the pattern in the original code where
  // the zone struct was accessed inline within processStatus().
  // All other mutations must go through the set_zone_*() methods to
  // ensure sensors are published on every state change.
  // -------------------------------------------------------------------
  struct Zone {
    vistaECPBinarySensor *binary_sensor{nullptr};
    vistaECPTextSensor *text_sensor{nullptr};

    uint8_t zone{0};              // logical zone number (1-based)
    uint8_t partition{0};         // owning partition id
    uint32_t rfserial{0};         // RF device serial; 0 = hardwired
    uint8_t rfloop{0};            // RF loop number (1–4)
    int64_t rfnext_hb{0};         // next heartbeat time (µs); 0 = RF disabled;
                                  // non-zero sentinel on new registration
    int64_t time{0};              // timestamp of last state change (µs)
    int64_t last_direct_time{0};  // µs timestamp of last on_rf_direct() update;
                                  // used by on_rf_zone_packet() to suppress
                                  // redundant ECP-path publishes

    bool active{false};    // zone is registered and participating
    bool open{false};      // zone is faulted / open
    bool bypass{false};    // zone is bypassed
    bool alarm{false};     // zone is in alarm
    bool check{false};     // zone has a trouble/check condition
    bool rflowbat{false};  // RF sensor is reporting low battery
    bool emulated{false};  // zone is software-emulated (no physical sensor)
  };

  // -------------------------------------------------------------------
  // Registration — call during VistaESPHome::setup()
  // -------------------------------------------------------------------

  // Register or update a zone's binary sensor and RF parameters.
  // If the zone already exists (e.g. previously registered by
  // register_zone_text()), the binary sensor and RF fields are updated
  // in place rather than creating a duplicate entry.
  void register_zone(vistaECPBinarySensor *sensor, uint8_t partition, uint8_t zone_number, uint32_t rf_serial,
                     uint8_t rf_loop, bool emulated);

  // Register or update a zone's text sensor.
  // If the zone already exists, only the text sensor pointer is updated.
  void register_zone_text(vistaECPTextSensor *sensor, uint8_t partition, uint8_t zone_number);

  // Register the combined zone-status text sensor that receives a
  // comma-separated summary string on every refresh() call.
  // Format: "OP:3,BY:7,CK:12" etc.  An empty string means all clear.
  void register_zone_status_sensor(vistaECPTextSensor *sensor);

  // -------------------------------------------------------------------
  // Lookup — non-const because callers may update fields via the pointer
  // -------------------------------------------------------------------

  // Returns a pointer into the internal zone list, or nullptr if not found.
  Zone *get_zone(uint16_t zone_number);

  // Returns the first zone whose rfserial matches, or nullptr.
  Zone *get_zone_by_rf_serial(uint32_t serial);

  // -------------------------------------------------------------------
  // Individual state setters
  //
  // Called by PartitionManager::process_status_flags() when F7 status
  // bytes indicate a zone-level event.  Each setter is a no-op when the
  // zone is not registered, not active, or already in the requested
  // state.  Sensors are published immediately on any real transition.
  //
  // Mutual exclusions enforced:
  //   set_zone_open(true)  clears check and bypass.
  //   set_zone_check(true) clears open and alarm.
  // -------------------------------------------------------------------

  // True when this zone's state is maintained by a direct path: the
  // CC1101 receiver through on_rf_direct(), or a Home Assistant service
  // call through on_zone_direct() for an emulated zone.  Both stamp
  // last_direct_time; a zone that has never been touched by either
  // reports false.
  //
  // Such a zone must not be driven by the AUI fault list at all.  That
  // answer is a poll cycle stale by construction -- see the note on
  // AUIManager::apply_zone_fault_masks() -- and the direct path already
  // knows the truth sooner.  Takes zone_mutex_.
  bool zone_has_direct_path(uint8_t zone_number) const;

  // Refresh a zone's last-activity timestamp (used to defer TTL expiry
  // while the panel is still reporting the zone).  Takes zone_mutex_ —
  // callers must not hold a raw Zone* across this call.
  void touch_zone_time(uint8_t zone_number);

  // Assign a zone's owning partition if it does not already have one.
  // No-op for unregistered, inactive, or already-assigned zones.
  void assign_partition(uint8_t zone_number, uint8_t partition);

  void set_zone_open(uint8_t zone_number, bool open);
  void set_zone_bypass(uint8_t zone_number, bool bypass);
  void set_zone_alarm(uint8_t zone_number, bool alarm);
  void set_zone_check(uint8_t zone_number, bool check);
  void set_zone_lowbat(uint8_t zone_number, bool low);

  // -------------------------------------------------------------------
  // Packet decode — called by PacketDispatcher
  // -------------------------------------------------------------------

  // Decode a 4-byte FA expander packet (green-wire monitor, type==1,
  // src==0xFA, size==4).  Updates the open state of the resolved zone
  // and publishes its sensors.
  void on_expander_zone_packet(const char *payload, int size);

  // Decode a kRFZoneMessageLength-byte FB RF receiver packet (green-wire
  // monitor, type==1, src==0xFB).  Validates the two's-complement
  // checksum, updates the matched zone's open and rflowbat state, and
  // publishes its sensors.
  //
  // Returns the formatted device serial string (e.g. "12340001") so that
  // PacketDispatcher can forward it to the rf_messages text sensor, or an
  // empty string if the checksum failed or the packet was a heartbeat with
  // no zone-state component.
  //
  // If on_rf_direct() updated this zone within the last kDirectSuppressUs,
  // the state is still updated for consistency but the sensor publish is
  // skipped to prevent duplicate / flapping HA state changes.
  std::string on_rf_zone_packet(const char *payload, int size);

  // Direct fast-path update called by VistaESPHome::rf_direct_task()
  // immediately after CC1101Receiver decodes a valid, non-heartbeat packet.
  // Uses the raw ecp_status byte (same bit layout as the FB status byte)
  // to update zone open/lowbat and publish to Home Assistant without
  // waiting for the panel's F1 poll → FA/FB round-trip.
  // Heartbeat packets (ecp_status & 0x05) are silently ignored.
  // Must only be called from rf_direct_task; protected by zone_mutex_.
  void on_rf_direct(uint32_t serial, uint8_t ecp_status, int8_t rssi = 0);

  // Direct fast-path update called by VistaESPHome::svc_set_zone_fault()
  // when an emulated zone's fault state is changed via the Home Assistant
  // service.  Publishes the new open state immediately without waiting for
  // the panel's ECP echo (FA expander packet or FB RF receiver packet).
  // The ECP-path publish is suppressed for kDirectSuppressUs afterwards to
  // prevent duplicate / flapping HA state changes.
  // Protected by zone_mutex_; safe to call from the ESPHome main loop task.
  void on_zone_direct(uint8_t zone_number, bool fault);

  // -------------------------------------------------------------------
  // Emulated zone control — called from VistaESPHome service callbacks
  // -------------------------------------------------------------------

  // For RF zones: encodes the fault state into a VistaBus RF message
  // and sends it.  For hardwired zones: sets the expander zone-status
  // bit directly via VistaBus::setZoneStatusBit().
  void send_emulated_fault(uint8_t zone_number, bool fault, VistaBus &bus);

  // -------------------------------------------------------------------
  // Refresh — called by PartitionManager::process_status_flags()
  // -------------------------------------------------------------------

  // Reconcile all zone states against the current partition light flags,
  // apply TTL-based expiry to stale open/check states, publish per-zone
  // sensors, and update the combined zone-status sensor if its string
  // has changed.
  //
  // partition_lights — the LightStates decoded from the current F7 packet
  //                    for the zone's owning partition.
  // ttl              — stale-state expiry in microseconds (from VistaESPHome).
  void refresh(const LightStates &partition_lights, int64_t ttl);

  // -------------------------------------------------------------------
  // RF heartbeat emulation
  // -------------------------------------------------------------------

  // Stagger initial heartbeat timestamps across 1–10 minutes for all
  // registered RF zones whose heartbeats are managed internally.
  // Must be called once at the start of the processReceiveQueue task,
  // after all zones are registered.
  void init_rf_heartbeat_timers();

  // Check all RF zones and send a supervision (heartbeat) message to
  // any whose internal timer has expired.  Reschedules the next heartbeat
  // at 70–90 minutes with random jitter.  No-op for zones whose heartbeats
  // are managed externally (external_heartbeat_mode_ == true).
  // Call each iteration of the receive task loop.
  void handle_rf_heartbeats(VistaBus &bus);

  // Send an immediate supervision heartbeat for the specified zone and
  // reset its internal timer (extending it by 70–90 minutes from now).
  // Called by the set_rf_zone_heartbeat HA service.  Safe to call
  // regardless of external_heartbeat_mode_.
  //
  // fault — current open/fault state of the zone; encodes the zone's
  //         loop-fault bit alongside the supervision bit (0x04) so the
  //         panel receives a fully-formed status byte rather than just
  //         the supervision bit with all loops cleared.
  void send_rf_heartbeat(uint8_t zone_number, bool fault, VistaBus &bus);

  // Select heartbeat management mode for virtual RF zones.
  // When true, internal timer-based emission is suppressed and heartbeats
  // must be driven externally via the set_rf_zone_heartbeat service.
  void set_external_heartbeat_mode(bool external) { external_heartbeat_mode_ = external; }

  // -------------------------------------------------------------------
  // Snapshot accessor
  // -------------------------------------------------------------------

  // Build and return the combined zone-status string from current state
  // without triggering any sensor publish.  Useful for an initial publish
  // at startup before the first refresh() call.
  std::string build_zone_status_string() const;

  void publish_initial_states();

  // True if any registered zone is a physical RF sensor (rfserial != 0
  // and not emulated).  Used to decide whether the CC1101 packet-absence
  // watchdog should be active — pointless if no physical sensors exist.
  bool has_physical_rf_zones() const {
    for (const auto &z : zones_)
      if (z.active && z.rfserial != 0 && !z.emulated)
        return true;
    return false;
  }

 private:
  // -------------------------------------------------------------------
  // Internal helpers
  // -------------------------------------------------------------------

  // Publish both the binary sensor (open || check) and the text sensor
  // (formatted state string) for a single zone.  Does nothing if both
  // sensor pointers are null.  Publishes unconditionally — see the
  // note on the definition before adding any change-detection here.
  void publish_zone(Zone *zt);

  // Build the combined zone-status string.  Caller must hold zone_mutex_.
  std::string build_zone_status_string_locked() const;

  // Publish the combined zone-status string if it differs from the last
  // one sent.  Caller must hold zone_mutex_.
  void publish_zone_status_if_changed();

  // Guard+lookup helper for set_zone_* methods.  Caller must hold
  // zone_mutex_.  Returns z only when the flag at 'field' needs to
  // change; returns nullptr on missing/inactive/unchanged zone.
  Zone *zone_if_flag_differs(uint8_t zone_number, bool Zone::*field, bool value);

#ifdef CC1101_RECEIVER
  // Small serial→RSSI cache populated by on_rf_direct() for every
  // decoded packet (including unregistered serials).  Queried by
  // on_rf_zone_packet() ~50–500 ms later to append RSSI to the
  // rf_messages string regardless of zone registration status.
  struct RssiCacheEntry {
    uint32_t serial{0};
    int8_t rssi{0};
  };
  RssiCacheEntry rssi_cache_[kRssiCacheSize]{};
  uint8_t rssi_cache_idx_{0};

  void store_rssi(uint32_t serial, int8_t rssi);
  int8_t fetch_rssi(uint32_t serial) const;
#endif

  // -------------------------------------------------------------------
  // Member data
  // -------------------------------------------------------------------

  // When true, handle_rf_heartbeats() skips all zones so that the caller
  // (Home Assistant automation) drives heartbeat timing via
  // send_rf_heartbeat().  Defaults to false (internal timer mode).
  bool external_heartbeat_mode_{false};

  // Mutex serialising access to zones_ between rf_direct_task and
  // processReceiveQueue.  Created in the constructor; must be taken
  // before any read or write of zones_ from either task.
  SemaphoreHandle_t zone_mutex_{nullptr};

  std::vector<Zone> zones_;

  // Combined zone-status text sensor — one sensor reports all zones.
  // Distinct from the per-zone text_sensor fields inside Zone.
  vistaECPTextSensor *zone_status_sensor_{nullptr};

  // Previous value of the combined status string; used to suppress
  // redundant publishes when nothing has changed.
  std::string previous_zone_status_;
};

}  // namespace alarm_panel
}  // namespace esphome
