#pragma once

#include "vista_bus.h"           // VistaBus (needed for send_emulated_fault/tamper,
                                //           handle_rf_heartbeats)
#include "helper_structs.h"     // LightStates (needed by refresh())
#include "constants.h"          // kRFZoneMessageLength
#include <vector>
#include <string>
#include <cstdint>

// Forward-declare sensor interfaces to avoid pulling in the full ESPHome
// component tree into every translation unit that includes this header.
namespace esphome
{
    namespace alarm_panel
    {
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
        //   - Emulated zone fault/tamper forwarding to VistaBus.
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
        //   All methods are intended to be called from the processReceiveQueue
        //   FreeRTOS task only.  No internal locking is provided.
        // -----------------------------------------------------------------------
        class ZoneManager
        {
        public:

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
            struct Zone
            {
                vistaECPBinarySensor *binary_sensor {nullptr};
                vistaECPTextSensor   *text_sensor   {nullptr};

                uint8_t  zone      {0};       // logical zone number (1-based)
                uint8_t  partition {0};       // owning partition id
                uint32_t rfserial  {0};       // RF device serial; 0 = hardwired
                uint8_t  rfloop    {0};       // RF loop number (1–4)
                int64_t  rfnext_hb {0};       // next heartbeat time (µs); 0 = RF disabled;
                                              // non-zero sentinel on new registration
                int64_t  time      {0};       // timestamp of last state change (µs)

                bool active   {false};        // zone is registered and participating
                bool open     {false};        // zone is faulted / open
                bool bypass   {false};        // zone is bypassed
                bool alarm    {false};        // zone is in alarm
                bool check    {false};        // zone has a trouble/check condition
                bool rflowbat {false};        // RF sensor is reporting low battery
            };

            // -------------------------------------------------------------------
            // Registration — call during VistaESPHome::setup()
            // -------------------------------------------------------------------

            // Register or update a zone's binary sensor and RF parameters.
            // If the zone already exists (e.g. previously registered by
            // register_zone_text()), the binary sensor and RF fields are updated
            // in place rather than creating a duplicate entry.
            void register_zone(vistaECPBinarySensor *sensor,
                               uint8_t partition,
                               uint8_t zone_number,
                               uint32_t rf_serial,
                               uint8_t rf_loop,
                               bool emulated);

            // Register or update a zone's text sensor.
            // If the zone already exists, only the text sensor pointer is updated.
            void register_zone_text(vistaECPTextSensor *sensor,
                                    uint8_t partition,
                                    uint8_t zone_number);

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

            void set_zone_open  (uint8_t zone_number, bool open);
            void set_zone_bypass(uint8_t zone_number, bool bypass);
            void set_zone_alarm (uint8_t zone_number, bool alarm);
            void set_zone_check (uint8_t zone_number, bool check);
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
            std::string on_rf_zone_packet(const char *payload, int size);

            // -------------------------------------------------------------------
            // Emulated zone control — called from VistaESPHome service callbacks
            // -------------------------------------------------------------------

            // For RF zones: encodes the fault state into a VistaBus RF message
            // and sends it.  For hardwired zones: sets the expander fault bits
            // directly via VistaBus::setExpFaultBits().
            void send_emulated_fault(uint8_t zone_number,
                                     bool fault,
                                     VistaBus &bus);

            // Delegates directly to VistaBus::setExpTamper().  Provided here so
            // VistaESPHome has a single delegation target for all emulated zone
            // operations regardless of type.
            void send_emulated_tamper(uint8_t zone_number,
                                      bool tamper_active,
                                      VistaBus &bus);

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
            // registered RF zones.  Must be called once at the start of the
            // processReceiveQueue task, after all zones are registered.
            void init_rf_heartbeat_timers();

            // Check all RF zones and send a supervision (heartbeat) message to
            // any whose timer has expired.  Reschedules the next heartbeat at
            // 70–90 minutes with random jitter.  Call each iteration of the
            // receive task loop.
            void handle_rf_heartbeats(VistaBus &bus);

            // -------------------------------------------------------------------
            // Snapshot accessor
            // -------------------------------------------------------------------

            // Build and return the combined zone-status string from current state
            // without triggering any sensor publish.  Useful for an initial publish
            // at startup before the first refresh() call.
            std::string build_zone_status_string() const;

            void publish_initial_states();

        private:

            // -------------------------------------------------------------------
            // Internal helper
            // -------------------------------------------------------------------

            // Publish both the binary sensor (open || check) and the text sensor
            // (formatted state string) for a single zone.  Does nothing if both
            // sensor pointers are null.
            void publish_zone(Zone *zt);

            // -------------------------------------------------------------------
            // Member data
            // -------------------------------------------------------------------

            std::vector<Zone> zones_;

            // Combined zone-status text sensor — one sensor reports all zones.
            // Distinct from the per-zone text_sensor fields inside Zone.
            vistaECPTextSensor *zone_status_sensor_ {nullptr};

            // Previous value of the combined status string; used to suppress
            // redundant publishes when nothing has changed.
            std::string previous_zone_status_;
        };

    } // namespace alarm_panel
} // namespace esphome
