// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include "vista_bus.h"
#include "partition_manager.h"
#include <string>
#include <cstdint>

namespace esphome
{
    namespace alarm_panel
    {
        // -----------------------------------------------------------------------
        // CommandWriter
        //
        // Owns all outbound panel commands: arming, disarming, zone bypass, and
        // raw keypress forwarding.  This is the only class that calls
        // VistaBus::write — all other classes interact with the bus read-side
        // only, or through VistaBus::writedirect (used by AUIManager).
        //
        // Lifecycle:
        //   Constructed once during VistaESPHome::setup(), after VistaBus and
        //   PartitionManager are initialised.  set_access_code() and
        //   set_quick_arm() must be called before any arm/disarm method is used.
        //
        // Thread safety:
        //   All public methods are intended to be called from the
        //   processReceiveQueue FreeRTOS task or from ESPHome service callbacks,
        //   both of which run on the same core.  If you introduce multi-core
        //   calling patterns, add a mutex around bus_.write().
        //
        // The "W" alias:
        //   The original set_alarm_state() accepted both "A" and "W" as
        //   away-arm commands.  This alias is intentionally not present in
        //   keypress() — map it at the VistaESPHome service-registration layer
        //   if your YAML configuration relies on it.
        // -----------------------------------------------------------------------
        class CommandWriter
        {
        public:

            // -------------------------------------------------------------------
            // Construction
            // -------------------------------------------------------------------

            // Both references must outlive this object.
            // PartitionManager is held as const& because CommandWriter only
            // reads partition state (address, sequence, armed flag); it never
            // modifies it.
            CommandWriter(VistaBus &bus,
                          const PartitionManager &partitions)
                : bus_(bus), partitions_(partitions) {}

            // Non-copyable, non-movable — holds references.
            CommandWriter(const CommandWriter &)            = delete;
            CommandWriter &operator=(const CommandWriter &) = delete;

            // -------------------------------------------------------------------
            // Configuration — call during setup before any command method
            // -------------------------------------------------------------------

            // Stored access code used as fallback when the caller does not
            // supply one, or supplies an invalid one.  Must point to a
            // null-terminated string that remains valid for the lifetime of
            // this object (typically a string literal or a member of
            // VistaESPHome).
            void set_access_code(const char *code) { access_code_ = code; }

            // When true, arming commands are sent as "#N" (quick-arm) without
            // an access code prefix.  Disarm always requires a code regardless
            // of this setting.
            void set_quick_arm(bool enabled) { quick_arm_ = enabled; }

            // -------------------------------------------------------------------
            // Arm commands
            //
            // partition_id — logical partition number (1–8).
            // code         — 4-digit numeric access code.  If empty or invalid,
            //                the stored access_code_ is used instead.  If
            //                quick_arm_ is true the code is not needed and may
            //                be omitted.
            //
            // All arm methods silently return false and log a warning if the
            // partition is already armed, to avoid re-arming a live panel.
            // Returns true if the command was enqueued successfully on the bus.
            // -------------------------------------------------------------------

            bool arm_away   (int partition_id, const std::string &code = "");
            bool arm_stay   (int partition_id, const std::string &code = "");
            bool arm_night  (int partition_id, const std::string &code = "");
            bool arm_instant(int partition_id, const std::string &code = "");

            // -------------------------------------------------------------------
            // Disarm
            //
            // Always requires a valid 4-digit code.  quick_arm_ has no effect.
            // Returns true if the command was enqueued successfully.
            // -------------------------------------------------------------------

            bool disarm(int partition_id, const std::string &code);

            // -------------------------------------------------------------------
            // Bypass commands
            //
            // bypass_zone  — sends code + "6#", entering bypass mode so the
            //                user can select which zones to bypass interactively.
            // quick_bypass — sends code + "600", bypassing all currently open
            //                zones at once.
            // Both require a valid 4-digit code.
            // -------------------------------------------------------------------

            bool bypass_zone  (int partition_id, const std::string &code);
            bool quick_bypass (int partition_id, const std::string &code);

            // -------------------------------------------------------------------
            // Emergency triggers
            //
            // These do not require an access code on Honeywell Vista panels.
            // The code parameter is accepted for API symmetry with ESPHome
            // service callbacks but is ignored.
            //
            // trigger_fire  — sends the two-key fire sequence ("AF").
            // trigger_panic — sends the two-key panic sequence ("AM").
            // -------------------------------------------------------------------

            bool trigger_fire (int partition_id, const std::string &code = "");
            bool trigger_panic(int partition_id, const std::string &code = "");

            // -------------------------------------------------------------------
            // Raw keypress
            //
            // Forwards an arbitrary key string to the specified partition's
            // keypad address.  Single-character command aliases are intercepted
            // and routed to the appropriate named method:
            //
            //   "A" → arm_away      "S" → arm_stay     "N" → arm_night
            //   "I" → arm_instant   "D" → disarm        "B" → bypass_zone
            //   "Y" → quick_bypass
            //
            // All other strings are sent verbatim.  Maximum payload is 24 bytes
            // (the VistaBus send-packet limit).
            //
            // code is forwarded to the intercepted methods when applicable; it
            // is ignored for verbatim sends.
            // Returns true if the command was enqueued successfully.
            // -------------------------------------------------------------------

            bool keypress(const std::string &keys,
                          int partition_id,
                          const std::string &code = "");

        private:

            // -------------------------------------------------------------------
            // Internal helpers
            // -------------------------------------------------------------------

            // Validate and resolve the effective 4-digit code into resolved_out.
            // Tries supplied first, falls back to access_code_.
            // resolved_out receives exactly 4 chars with NO null terminator.
            // Returns false and logs an error if neither source is valid.
            bool resolve_code(const std::string &supplied,
                              char resolved_out[4]) const;

            // Validate the partition id and look up its keypad address and
            // current sequence counter.  Returns false and logs if the partition
            // is unknown or its address is out of the valid range (1–23, 31).
            bool resolve_partition(int partition_id,
                                   uint8_t &addr_out,
                                   uint8_t &seq_out) const;

            // Write raw bytes to the bus and log the outcome.
            // data is not null-terminated; size is the exact byte count.
            bool send_keys(const char *data, int size,
                           int partition_id,
                           uint8_t addr, uint8_t seq) const;

            // Assemble a 4-byte code + suffix into a single buffer and send it.
            // code[4] has no null terminator.  suffix is suffix_len bytes long.
            // Fixes the strncpy/null-terminator hazard present in the original.
            bool send_code_plus_keys(const char code[4],
                                     const char *suffix,
                                     int suffix_len,
                                     int partition_id,
                                     uint8_t addr, uint8_t seq) const;

            // -------------------------------------------------------------------
            // Member data
            // -------------------------------------------------------------------

            VistaBus               &bus_;         // sole writer to the panel bus
            const PartitionManager &partitions_;  // read-only: address, sequence, armed state

            const char *access_code_ {nullptr};  // fallback code; not owned
            bool        quick_arm_   {false};

        };

    } // namespace alarm_panel
} // namespace esphome
