// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#include "command_writer.h"
#include "helper_funcs.h"
#include "esp_log.h"
#include <cstring>
#include <cstdio>

namespace esphome
{
    namespace alarm_panel
    {
        static const char * const TAG = "cmd_writer";

        // ---------------------------------------------------------------------------
        // Internal helpers
        // ---------------------------------------------------------------------------

        // Resolve and validate the effective access code.
        //
        // Priority: supplied code > stored access_code_.
        // Returns true and populates resolved_out (exactly 4 chars, no null
        // terminator — callers are responsible for their own buffer sizing) when a
        // valid 4-digit numeric code is available.  Returns false otherwise.

        bool CommandWriter::resolve_code(const std::string &supplied,
                                         char resolved_out[4]) const
        {
            const std::string *effective = nullptr;

            if (supplied.length() == 4 && isInt(supplied, 10))
                effective = &supplied;
            else if (access_code_ != nullptr)
            {
                static std::string stored_str; 
                stored_str = access_code_;
                if (stored_str.length() == 4 && isInt(stored_str, 10))
                    effective = &stored_str;
            }

            if (effective == nullptr)
            {
                ESP_LOGE(TAG, "No valid 4-digit numeric access code available.");
                return false;
            }

            memcpy(resolved_out, effective->c_str(), 4);
            return true;
        }

        // Look up keypad address and current sequence for a logical partition id.
        // Logs and returns false if the partition is not found or its keypad
        // address is out of the valid range accepted by VistaBus::write (1–23 or 31).
        bool CommandWriter::resolve_partition(int partition_id,
                                              uint8_t &addr_out,
                                              uint8_t &seq_out) const
        {
            if (partition_id < 1 || partition_id > 8)
            {
                ESP_LOGE(TAG, "Partition %d is out of range (1–8).", partition_id);
                return false;
            }

            if (!partitions_.get_partition_info(partition_id, addr_out, seq_out))
            {
                ESP_LOGE(TAG, "Partition %d is not configured.", partition_id);
                return false;
            }

            // Address 31 (0x1F) is the broadcast address used by some configurations.
            const bool valid_addr = (addr_out >= 1 && addr_out <= 23) || addr_out == 31;
            if (!valid_addr)
            {
                ESP_LOGE(TAG, "Partition %d has invalid keypad address %d.", partition_id, addr_out);
                return false;
            }

            return true;
        }

        // Write a key string to the bus and log the result.
        // data must remain valid for the duration of this call (it is not modified).
        bool CommandWriter::send_keys(const char *data, int size,
                                      int partition_id,
                                      uint8_t addr, uint8_t seq) const
        {
            const bool ok = bus_.write(data, size, addr, seq);
            if (ok)
                ESP_LOGD(TAG, "Wrote %d byte(s) to partition %d (addr 0x%02X).",
                         size, partition_id, addr);
            else
                ESP_LOGE(TAG, "Failed to write to partition %d — send queue full.", partition_id);
            return ok;
        }

        // Build and send a code + suffix sequence,
        // suffix     — the key(s) appended after the 4-digit code, e.g. "3" or "33"
        // suffix_len — byte count of suffix, not including any terminator
        bool CommandWriter::send_code_plus_keys(const char code[4],
                                                const char *suffix,
                                                int suffix_len,
                                                int partition_id,
                                                uint8_t addr, uint8_t seq) const
        {
            // Maximum payload the bus accepts is 24 bytes; a code+suffix is at most 7.
            char buf[24];
            memcpy(buf, code, 4);
            memcpy(buf + 4, suffix, suffix_len);
            return send_keys(buf, 4 + suffix_len, partition_id, addr, seq);
        }

        // ---------------------------------------------------------------------------
        // Public API — arm / disarm / keypress
        // ---------------------------------------------------------------------------

        bool CommandWriter::arm_away(int partition_id, const std::string &code)
        {
            if (partitions_.is_armed(partition_id))
            {
                ESP_LOGW(TAG, "Partition %d is already armed; ignoring arm_away.", partition_id);
                return false;
            }

            uint8_t addr, seq;
            if (!resolve_partition(partition_id, addr, seq))
                return false;

            if (quick_arm_)
                return send_keys("#2", 2, partition_id, addr, seq);

            char resolved[4];
            if (!resolve_code(code, resolved))
                return false;

            return send_code_plus_keys(resolved, "2", 1, partition_id, addr, seq);
        }

        bool CommandWriter::arm_stay(int partition_id, const std::string &code)
        {
            if (partitions_.is_armed(partition_id))
            {
                ESP_LOGW(TAG, "Partition %d is already armed; ignoring arm_stay.", partition_id);
                return false;
            }

            uint8_t addr, seq;
            if (!resolve_partition(partition_id, addr, seq))
                return false;

            if (quick_arm_)
                return send_keys("#3", 2, partition_id, addr, seq);

            char resolved[4];
            if (!resolve_code(code, resolved))
                return false;

            return send_code_plus_keys(resolved, "3", 1, partition_id, addr, seq);
        }

        bool CommandWriter::arm_night(int partition_id, const std::string &code)
        {
            if (partitions_.is_armed(partition_id))
            {
                ESP_LOGW(TAG, "Partition %d is already armed; ignoring arm_night.", partition_id);
                return false;
            }

            uint8_t addr, seq;
            if (!resolve_partition(partition_id, addr, seq))
                return false;

            if (quick_arm_)
                return send_keys("#33", 3, partition_id, addr, seq);

            char resolved[4];
            if (!resolve_code(code, resolved))
                return false;

            return send_code_plus_keys(resolved, "33", 2, partition_id, addr, seq);
        }

        bool CommandWriter::arm_instant(int partition_id, const std::string &code)
        {
            if (partitions_.is_armed(partition_id))
            {
                ESP_LOGW(TAG, "Partition %d is already armed; ignoring arm_instant.", partition_id);
                return false;
            }

            uint8_t addr, seq;
            if (!resolve_partition(partition_id, addr, seq))
                return false;

            if (quick_arm_)
                return send_keys("#7", 2, partition_id, addr, seq);

            char resolved[4];
            if (!resolve_code(code, resolved))
                return false;

            return send_code_plus_keys(resolved, "7", 1, partition_id, addr, seq);
        }

        bool CommandWriter::disarm(int partition_id, const std::string &code)
        {
            uint8_t addr, seq;
            if (!resolve_partition(partition_id, addr, seq))
                return false;

            // Disarm always requires a real code — quick arm is not applicable.
            char resolved[4];
            if (!resolve_code(code, resolved))
                return false;

            // Panel command: code + "1"
            return send_code_plus_keys(resolved, "1", 1, partition_id, addr, seq);
        }

        bool CommandWriter::bypass_zone(int partition_id, const std::string &code)
        {
            uint8_t addr, seq;
            if (!resolve_partition(partition_id, addr, seq))
                return false;

            char resolved[4];
            if (!resolve_code(code, resolved))
                return false;

            // Panel command: code + "6#"
            return send_code_plus_keys(resolved, "6#", 2, partition_id, addr, seq);
        }

        bool CommandWriter::quick_bypass(int partition_id, const std::string &code)
        {
            // Panel command: code + "600" — bypasses all open zones at once.
            uint8_t addr, seq;
            if (!resolve_partition(partition_id, addr, seq))
                return false;

            char resolved[4];
            if (!resolve_code(code, resolved))
                return false;

            return send_code_plus_keys(resolved, "600", 3, partition_id, addr, seq);
        }

        bool CommandWriter::trigger_fire(int partition_id, const std::string &code)
        {
            // Honeywell Vista fire alarm: hold A and # simultaneously.
            // On the ECP bus this is sent as the key sequence "AF" per the protocol.
            // This implements the standard two-key simultaneous panic sequence.
            uint8_t addr, seq;
            if (!resolve_partition(partition_id, addr, seq))
                return false;

            (void)code; // Fire does not require a code on Vista panels
            return send_keys("AF", 2, partition_id, addr, seq);
        }

        bool CommandWriter::trigger_panic(int partition_id, const std::string &code)
        {
            // Honeywell Vista panic: hold * and # simultaneously → "AM" on the bus.
            uint8_t addr, seq;
            if (!resolve_partition(partition_id, addr, seq))
                return false;

            (void)code; // Panic does not require a code on Vista panels
            return send_keys("AM", 2, partition_id, addr, seq);
        }

        // ---------------------------------------------------------------------------
        // Raw keypress — pass arbitrary key strings directly to a partition's keypad.
        //
        // Single-letter convenience strings ("A", "S", "N", "I", "B", "Y", "D")
        // are intercepted and routed to the appropriate arm/disarm/bypass method
        // so that the armed-state guard and code logic apply consistently.
        // Any other string is forwarded verbatim to the bus.
        // ---------------------------------------------------------------------------

        bool CommandWriter::keypress(const std::string &keys,
                                     int partition_id,
                                     const std::string &code)
        {
            // Intercept single-character command strings that map to named operations.
            if (keys == "A") return arm_away    (partition_id, code);
            if (keys == "S") return arm_stay    (partition_id, code);
            if (keys == "N") return arm_night   (partition_id, code);
            if (keys == "I") return arm_instant (partition_id, code);
            if (keys == "D") return disarm      (partition_id, code);
            if (keys == "B") return bypass_zone (partition_id, code);
            if (keys == "Y") return quick_bypass(partition_id, code);

            // Arbitrary key sequence — send verbatim.
            uint8_t addr, seq;
            if (!resolve_partition(partition_id, addr, seq))
                return false;

            // Clamp to the maximum payload VistaBus::write accepts.
            const int len = static_cast<int>(keys.length());
            if (len == 0 || len > 24)
            {
                ESP_LOGE(TAG, "keypress: string length %d is invalid (must be 1–24).", len);
                return false;
            }

            return send_keys(keys.c_str(), len, partition_id, addr, seq);
        }

    } // namespace alarm_panel
} // namespace esphome
