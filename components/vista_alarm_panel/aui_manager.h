#pragma once

#include "vista_bus.h"           // VistaBus
#include "zone_manager.h"       // ZoneManager
#include "helper_structs.h"     // AUIDevice, AUIRequest, PanelClock
#include "esphome/components/time/real_time_clock.h"  // time::RealTimeClock, ESPTime
#include <cstdint>

namespace esphome
{
    namespace alarm_panel
    {
        // -----------------------------------------------------------------------
        // AUIManager
        //
        // Owns all AUI (Advanced User Interface) protocol interactions with the
        // Honeywell Vista panel: panel clock synchronisation and zone-fault
        // queries via the F2 packet sub-protocol.
        //
        // The AUI bus uses a separate addressing and sequencing scheme from the
        // standard ECP keypad protocol.  All writes go through VistaBus::writedirect
        // (raw hex, no key translation) rather than VistaBus::write.
        //
        // Responsibilities:
        //   - Periodic panel clock request (every 6 hours when auto_sync is on).
        //   - Panel time response decode: compare with RTC; push correction if
        //     drift exceeds 60 seconds.
        //   - Zone-fault query on broadcast notification or on request.
        //   - Zone-fault response parse: update ZoneManager with open/clear state
        //     for up to 128 zones.
        //   - Request timeout: auto-clear pending flag after 6 seconds.
        //
        // Does NOT own:
        //   - VistaBus (received by reference per call).
        //   - ZoneManager (received by reference per call).
        //   - RealTimeClock (received by pointer per call; may be nullptr).
        //
        // Lifecycle:
        //   1. Call set_device_address() and set_auto_sync() during setup.
        //   2. Call tick() each iteration of the receive task loop.
        //   3. Call on_f2_packet_with_bus() from PacketDispatcher when an F2
        //      packet arrives.
        //   4. Call on_zone_fault_broadcast_with_bus() from PacketDispatcher
        //      when a zone-fault broadcast is detected (F2 sub-type 0x6x).
        //   5. Optionally call request_time_sync() as an ESPHome service.
        //
        // Thread safety:
        //   All methods must be called from the processReceiveQueue FreeRTOS task.
        //   pending_bus_ is a call-scoped temporary; it is set and cleared within
        //   a single call to on_f2_packet_with_bus() and is never accessed from
        //   another context.
        // -----------------------------------------------------------------------
        class AUIManager
        {
        public:

            // -------------------------------------------------------------------
            // Configuration — call during VistaESPHome::setup()
            // -------------------------------------------------------------------

            // Set the AUI device address and initialize sequence1 to
            // (0x20 | addr). Call before any other method.  A zero address 
            // disables all AUIoperations (every method guards on device_.address != 0).
            void set_device_address(uint8_t addr);

            // Enable or disable automatic panel clock synchronisation.
            // When true, the panel time is requested every 6 hours and
            // corrected if the drift exceeds 60 seconds.
            void set_auto_sync(bool enabled);

            // -------------------------------------------------------------------
            // Main loop — call each iteration of the receive task
            // -------------------------------------------------------------------

            // Expire timed-out zone-fault requests and trigger periodic clock
            // synchronisation when due.
            //
            // in_program_mode — suppresses AUI writes while the panel is in
            //                   programming mode.
            // rtc             — used to validate the RTC before requesting a
            //                   clock sync; pass nullptr if time is not configured.
            void tick(VistaBus &bus, bool in_program_mode,
                      time::RealTimeClock *rtc);

            // -------------------------------------------------------------------
            // Packet handling — called by PacketDispatcher
            // -------------------------------------------------------------------

            // Primary F2 packet handler.  Carries the bus reference so that a
            // panel-time response can trigger set_panel_time() if needed.
            //
            // payload — full raw F2 payload from VistaBus::read_packet()
            // size    — number of valid bytes in payload
            // zones   — ZoneManager to update when a zone-fault list is decoded
            // rtc     — RealTimeClock for panel-time comparison; may be nullptr
            // bus     — used if set_panel_time() must be called in response
            void on_f2_packet_with_bus(const char *payload, int size,
                                       ZoneManager &zones,
                                       time::RealTimeClock *rtc,
                                       VistaBus &bus);

            // Called when the PacketDispatcher detects a zone-fault broadcast
            // notification (F2 sub-type 0x6x, payload[7] & 0xF0 == 0x60).
            // Issues a zone-fault query if no query is already pending.
            void on_zone_fault_broadcast_with_bus(ZoneManager &zones,
                                                  VistaBus &bus);

            // -------------------------------------------------------------------
            // Manual triggers — expose as ESPHome services via VistaESPHome
            // -------------------------------------------------------------------

            // Request a panel clock correction immediately.  Equivalent to the
            // original AUIset_panel_time() service.
            void request_time_sync(VistaBus &bus, bool in_program_mode,
                                   time::RealTimeClock *rtc);

            // -------------------------------------------------------------------
            // Accessors
            // -------------------------------------------------------------------

            // True while a zone-fault query has been issued and no response has
            // arrived yet.  Checked by tick() for timeout handling.
            bool request_pending() const { return request_.pending; }

            // Read back configuration for dump_config().
            uint8_t device_address() const { return device_.address; }
            bool    auto_sync()      const { return clock_.auto_sync; }

        private:

            // -------------------------------------------------------------------
            // AUI bus writes
            // -------------------------------------------------------------------

            // Request the current panel time (send query; response arrives as F2).
            void request_panel_time(VistaBus &bus, bool in_program_mode);

            // Push the current RTC time to the panel.
            void set_panel_time(VistaBus &bus, bool in_program_mode,
                                time::RealTimeClock *rtc);

            // Issue a zone-fault list query to the panel.
            void get_zone_faults(VistaBus &bus);

            // -------------------------------------------------------------------
            // Packet sub-handlers
            // -------------------------------------------------------------------

            // Interface-compatible overload used by PacketDispatcher when a bus
            // reference is not needed.  Calls on_f2_packet_with_bus() indirectly
            // via the pending_bus_ pointer mechanism.
            void on_f2_packet(const char *payload, int size,
                              ZoneManager &zones,
                              time::RealTimeClock *rtc);

            // Called internally from on_f2_packet() when a zone-fault broadcast
            // sub-type is detected.  Cannot issue a bus write without a bus
            // reference; the real work is done by on_zone_fault_broadcast_with_bus().
            void on_zone_fault_broadcast(ZoneManager &zones);

            // Decode a panel-time response from an F2 data string and compare
            // with the RTC.  Calls set_panel_time() if drift > 60 s and
            // auto_sync is enabled.
            void handle_panel_time_response(const char *f2data,
                                            uint8_t data_len,
                                            time::RealTimeClock *rtc);

            // Parse a space-separated zone-number / range string into bitmasks
            // and update ZoneManager accordingly.
            void process_zone_faults(const char *list, ZoneManager &zones);

            // Apply four 32-bit zone bitmasks (zones 1-32, 33-64, 65-96, 97-128)
            // to ZoneManager, calling set_zone_open() for each zone whose state
            // has changed.
            void apply_zone_fault_masks(const uint32_t mask[4], ZoneManager &zones);

            // Advance the AUI sequence2 counter through its 0x68–0x6F cycle.
            // Called before every bus write to maintain correct packet sequencing.
            void advance_sequence2();

            // -------------------------------------------------------------------
            // Debug logging (compiled out unless DEBUG_LOG is defined)
            // -------------------------------------------------------------------

#ifdef DEBUG_LOG
            void log_f2_type(uint8_t sum, uint8_t target,
                             const char *f2data, uint8_t data_len) const;
#endif

            // -------------------------------------------------------------------
            // Member data
            // -------------------------------------------------------------------

            AUIDevice  device_;   // address, sequence1, sequence2
            PanelClock clock_;    // next_sync timestamp, auto_sync flag
            AUIRequest request_;  // pending flag, issue timestamp

            // Temporary bus pointer valid only during on_f2_packet_with_bus().
            // Allows handle_panel_time_response() to call set_panel_time() without
            // requiring a permanent stored bus reference.  Always nullptr outside
            // of a call to on_f2_packet_with_bus().
            VistaBus *pending_bus_ {nullptr};
        };

    } // namespace alarm_panel
} // namespace esphome
