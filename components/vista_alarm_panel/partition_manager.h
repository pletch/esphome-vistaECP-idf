#pragma once

#include "helper_structs.h"   // StatusFlags, LightStates, Partition, PartitionState
#include "helper_enums.h"     // SysState
#include "zone_manager.h"     // ZoneManager, ZoneManager::Zone
#include <vector>
#include <cstdint>
#include <cstddef>            // size_t

// Forward-declare ESPHome sensor base interfaces so this header does not
// pull in the full ESPHome component tree. Callers that actually register
// sensors will include vistaalarm.h (which defines the concrete types).
namespace esphome
{
    namespace alarm_panel
    {
        class vistaECPBinarySensor;
        class vistaECPTextSensor;

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
        //   5. Call tick_battery_decay() from the main sensor-refresh loop.
        // -----------------------------------------------------------------------
        class PartitionManager
        {
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
            void register_text_sensor(vistaECPTextSensor *sensor,
                                      uint8_t partition_number,
                                      const char *type);

            // Binary (on/off) sensors per partition.
            // Valid type strings: "READY", "TROUBLE", "BYPASS", "ARMED",
            //   "ARMED_AWAY", "ARMED_STAY", "ARMED_INSTANT", "ARMED_NIGHT",
            //   "CHIME", "ALARM", "FIRE".
            void register_status_sensor(vistaECPBinarySensor *sensor,
                                        uint8_t partition_number,
                                        const char *type);

            // System-wide (non-partition) binary sensors for AC power and
            // low battery. These are registered once without a partition number.
            void register_ac(vistaECPBinarySensor *sensor)  { ac_sensor_  = sensor; }
            void register_bat(vistaECPBinarySensor *sensor) { bat_sensor_ = sensor; }

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
            int process_status_flags(const StatusFlags &flags,
                                     ZoneManager &zones,
                                     int64_t ttl);

            // -------------------------------------------------------------------
            // Incremental updates from other packet types
            // -------------------------------------------------------------------

            // Called by PacketDispatcher when an F6 keypad-ACK packet arrives.
            // Advances the sequence counter for the matching partition.
            void on_keypad_ack(uint8_t raw_f6_addr_byte);

            // -------------------------------------------------------------------
            // Battery decay
            // -------------------------------------------------------------------

            // Call from the main sensor-refresh loop (replacing the battery
            // section formerly in VistaESPHome::refreshSensors).
            // Clears the low-battery flag once the event has aged past ttl
            // microseconds and publishes the change to bat_sensor_.
            void tick_battery_decay(int64_t ttl);

            // -------------------------------------------------------------------
            // Read accessors used by CommandWriter and VistaESPHome
            // -------------------------------------------------------------------

            // Fills keypad_addr and sequence for the given logical partition id.
            // Returns false if the partition is not known.
            bool get_partition_info(int partition_id,
                                    uint8_t &keypad_addr,
                                    uint8_t &sequence) const;

            // Returns true if the most recently published state for the given
            // partition has the armed flag set.
            bool is_armed(int partition_id) const;

            // Read-only access to the partition list (used by PacketDispatcher
            // for keypad-address lookups and by VistaESPHome for iteration).
            const std::vector<Partition> &partitions() const { return partitions_; }

            // Returns true if the most recently decoded F7 packet had the
            // program-mode bit set.  Used by the receive task to suppress AUI
            // writes during panel programming.  Closes the workaround noted in
            // vistaalarm.cpp where false was passed to aui_.tick().
            bool in_program_mode() const { return last_program_mode_; }

        private:

            // -------------------------------------------------------------------
            // Internal per-partition sensor-pointer bundles
            // -------------------------------------------------------------------

            struct TextSensors
            {
                vistaECPTextSensor *system_status {nullptr};
                vistaECPTextSensor *line1         {nullptr};
                vistaECPTextSensor *line2         {nullptr};
                vistaECPTextSensor *beeps         {nullptr};
            };

            struct StatusSensors
            {
                vistaECPBinarySensor *rdy  {nullptr};  // Ready
                vistaECPBinarySensor *trbl {nullptr};  // Trouble
                vistaECPBinarySensor *byp  {nullptr};  // Bypass
                vistaECPBinarySensor *arm  {nullptr};  // Armed (any)
                vistaECPBinarySensor *arma {nullptr};  // Armed Away
                vistaECPBinarySensor *arms {nullptr};  // Armed Stay
                vistaECPBinarySensor *armi {nullptr};  // Armed Instant
                vistaECPBinarySensor *armn {nullptr};  // Armed Night
                vistaECPBinarySensor *chm  {nullptr};  // Chime
                vistaECPBinarySensor *alm  {nullptr};  // Alarm
                vistaECPBinarySensor *fire {nullptr};  // Fire
            };

            // -------------------------------------------------------------------
            // Private helpers called only from process_status_flags()
            // -------------------------------------------------------------------

            // Derive the full LightStates from a decoded StatusFlags packet.
            // Pure function — no side effects, safe to call from tests.
            LightStates decode_light_states(const StatusFlags &flags) const;

            // Derive the SysState from already-decoded flags and lights.
            // Pure function — no side effects, safe to call from tests.
            SysState decode_system_state(const StatusFlags &flags,
                                         const LightStates &lights) const;

            // Publish a system state string change to the system_status text sensor.
            void publish_system_state(size_t kpi, SysState state);

            // Publish all binary light-state sensors that have changed.
            // include_armed_states gates the armed-subtype sensors (away/stay/etc.)
            // behind the same system_flag / updateSystemState condition as the original.
            void publish_light_states(size_t kpi,
                                      const LightStates &current,
                                      const LightStates &previous,
                                      bool force,
                                      bool include_armed_states);

            // Format and publish the two keypad display lines, inserting a
            // cursor-position bracket if promptPos > 0.
            void update_display_lines(size_t kpi, const StatusFlags &flags);

            // -------------------------------------------------------------------
            // Member data
            // -------------------------------------------------------------------

            // Partition configuration and rolling state. Indexed by kpi (0-based).
            std::vector<Partition>     partitions_;

            // Per-partition sensor pointers. Parallel to partitions_ after
            // initialize_sensor_vectors() is called.
            std::vector<TextSensors>   text_sensors_;
            std::vector<StatusSensors> status_sensors_;

            // System-wide sensors (not per-partition).
            vistaECPBinarySensor *ac_sensor_  {nullptr};
            vistaECPBinarySensor *bat_sensor_ {nullptr};

            // Battery state is managed here rather than inside LightStates
            // because it has independent decay timing separate from the F7 cycle.
            bool    current_bat_state_ {false};
            int64_t low_battery_time_  {0};

            // Timestamp of the last force-refresh cycle (every 5 minutes).
            int64_t last_force_refresh_ {0};

            // Cached program-mode flag from the most recently decoded F7 packet.
            // Exposed via in_program_mode() so the receive task can suppress AUI
            // writes without accessing StatusFlags directly.
            bool last_program_mode_ {false};
        };

    } // namespace alarm_panel
} // namespace esphome
