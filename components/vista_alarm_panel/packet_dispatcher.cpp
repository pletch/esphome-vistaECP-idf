// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

/*
 * PacketDispatcher — reads packets from VistaBus and routes them to collaborators.
 *
 *   - Packet framing and type dispatch.
 *   - StatusFlags decoding (F7 and assembled legacy-SE frames).
 *   - LRR Contact-ID string formatting (F9).
 *   - Legacy-SE multi-packet assembly state machine.
 */

#include "packet_dispatcher.h"
#include "sensor_interfaces.h"
#include "panel_text.h"
#include "translation.h"
#include "helper_funcs.h"
#include "LRR_strings.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <string>
#include <sys/time.h>

namespace esphome
{
    namespace alarm_panel
    {
        static constexpr const char *TAG = "vista-pkt";

        // ---------------------------------------------------------------------------
        // Construction
        // ---------------------------------------------------------------------------

        PacketDispatcher::PacketDispatcher(VistaBus &bus, const Config &cfg)
            : bus_(bus)
            , cfg_(cfg)
        {
        }

        // ---------------------------------------------------------------------------
        // Main dispatch loop body
        //
        // Called each iteration of VistaESPHome::processReceiveQueue().
        // Reads one packet from the bus (blocks in read_packet until data or timeout)
        // and routes it.
        // ---------------------------------------------------------------------------

        void PacketDispatcher::dispatch_one()
        {
            char payload[48];
            int  size = 0, type = 0, src = 0;
            memset(payload, '\0', sizeof(payload));

            if (!bus_.read_packet(payload, size, type, src, true))
                return;

            print_packet(payload, type, src, size);

            // Yellow-wire packets (type 0) are the primary panel-to-keypad stream.
            if (type == 0)
            {
                if (src == 0xF7)
                {
                    // Standard status frame — decode and update partition/zone state.
                    StatusFlags flags = decode_status_flags(payload, size);
                    ESP_LOGI(TAG, "Prompt: %s", flags.prompt1);
                    ESP_LOGI(TAG, "Prompt: %s", flags.prompt2);
                    ESP_LOGI(TAG, "Beeps: %d", flags.beeps);
                    if (cfg_.partitions != nullptr && cfg_.zones != nullptr)
                        cfg_.partitions->process_status_flags(flags, *cfg_.zones, cfg_.ttl);
                    // HITSTAR: panel requests acknowledgement via a "*" keypress.
                    if (flags.system_flag && strstr(flags.prompt2, HITSTAR) && cfg_.cmd != nullptr)
                        cfg_.cmd->keypress("*", flags.partition);
                }
                else if (src == 0xDD)
                {
                    // Legacy Vista20 SE protocol: assemble multi-packet frame, then
                    // treat the assembled buffer exactly like an F7 frame.
                    if (assemble_legacy_se(payload, size))
                    {
                        StatusFlags flags = decode_status_flags(
                                legacy_cmd_buffer_, static_cast<int>(sizeof(legacy_cmd_buffer_)));
                        ESP_LOGI(TAG, "Prompt: %s", flags.prompt1);
                        ESP_LOGI(TAG, "Prompt: %s", flags.prompt2);
                        ESP_LOGI(TAG, "Beeps: %d", flags.beeps);
                        if (cfg_.partitions != nullptr && cfg_.zones != nullptr)
                            cfg_.partitions->process_status_flags(flags, *cfg_.zones, cfg_.ttl);
                        // HITSTAR: panel requests acknowledgement via a "*" keypress.
                        if (flags.system_flag && strstr(flags.prompt1, HITSTAR) && cfg_.cmd != nullptr)
                            cfg_.cmd->keypress("*", flags.partition);
                    }
                }
                else if (src == 0xF2)
                {
                    // AUI (Advanced User Interface) packet.
                    if (cfg_.aui != nullptr && cfg_.zones != nullptr)
                        cfg_.aui->on_f2_packet_with_bus(payload, size,
                                                        *cfg_.zones, cfg_.rtc, bus_);
                }
                else if (src == 0xF9)
                {
                    // LRR (Long-Range Radio) / Contact-ID event.
                    handle_lrr_packet(payload, size);
                }
                else if (payload[0] != 0 && src == static_cast<int>(PacketType::KeypadAck))
                {
                    // F6 keypad-ACK: advance the outgoing sequence counter.
                    if (cfg_.partitions != nullptr)
                        cfg_.partitions->on_keypad_ack(static_cast<uint8_t>(payload[0]));
                }
                else if (src == 0xCF)
                {
                    // 0xCF marks a checksum failure on the primary (yellow-wire) UART.
                    chksum_failures_++;
                }
            }
            // Green-wire packets (type 1) come from expansion devices and RF receivers.
            else if (type == 1)
            {
                if (src == 0xFA && size == kFALegacyMessageLength)
                {
                    // Wired expansion module zone packet.
                    if (cfg_.zones != nullptr)
                        cfg_.zones->on_expander_zone_packet(payload, size);
                }
                else if (src == 0xFB && size == kRFZoneMessageLength)
                {
                    // RF receiver zone packet — empty return signals checksum failure.
                    if (cfg_.zones != nullptr)
                    {
                        std::string serial = cfg_.zones->on_rf_zone_packet(payload, size);
                        if (serial.empty())
                            chksum_failures_++;
                        else if (cfg_.rf_sensor != nullptr)
                            cfg_.rf_sensor->process(serial);
                    }
                }
            }

            // Publish updated failure count to the diagnostic sensor if it changed.
            if (cfg_.chksum_fail_sensor != nullptr
                    && chksum_failures_ != chksum_last_reported_)
            {
                chksum_last_reported_ = chksum_failures_;
                char buf[12];
                snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(chksum_failures_));
                cfg_.chksum_fail_sensor->publish_state(buf);
            }
        }

        // ---------------------------------------------------------------------------
        // F7 / assembled legacy-SE status frame decode
        //
        // Byte layout (offsets into cbuf):
        //   [1..4]  — keypad address bits (4 bytes × 8 bits = 32 keypad addresses)
        //   [5]     — faulted / checked / bypassed zone number (BCD)
        //   [6]     — beep count (low 3 bits), night flag (bit 4)
        //   [7]     — fire, system_flag, ready, armed_stay, low_battery, check, fire_zone
        //   [8]     — in_alarm, ac_power, chime, bypass, program_mode, instant, armed_away, zone_alarm
        //   [10]    — cursor position in prompt
        //   [12..27]— prompt line 1 (16 bytes, extended-char encoded)
        //   [28..43]— prompt line 2 (16 bytes, extended-char encoded)
        // ---------------------------------------------------------------------------

        StatusFlags PacketDispatcher::decode_status_flags(const char *cbuf, int size)
        {
            StatusFlags flags;

            // The layout below indexes up to cbuf[43] unconditionally.  Reject
            // anything shorter rather than relying on the caller having zeroed a
            // larger buffer first.
            if (size < 44)
            {
                ESP_LOGW(TAG, "Status frame too short (%d bytes, need 44); ignoring.", size);
                return flags;
            }

            // Keypad activity bytes — used to resolve the active partition.
            flags.keypad[0] = cbuf[1];
            flags.keypad[1] = cbuf[2];
            flags.keypad[2] = cbuf[3];
            flags.keypad[3] = cbuf[4];

            // Identify the active partition by matching keypad address bits.
            flags.partition = 0;
            if (cfg_.partitions != nullptr)
            {
                for (const auto &p : cfg_.partitions->partitions())
                {
                    // Each keypad address maps to a specific bit in cbuf[1..4].
                    // byte index = (addr >> 3) + 1, bit index = addr & 0x07
                    if (cbuf[(p.assigned_keypad >> 3) + 1]
                            & (0x01 << (p.assigned_keypad & 0x07)))
                    {
                        flags.partition = p.partition;
                        break;
                    }
                }
            }

            flags.zone          = static_cast<int>(toDec(cbuf[5]));
            flags.beeps         = cbuf[6] & kBitMaskByte1Beep;

            flags.fire          = ((cbuf[7] & kBitMaskByte2Fire)       > 0);
            flags.system_flag   = ((cbuf[7] & kBitMaskByte2SystemFlag)  > 0);
            flags.ready         = ((cbuf[7] & kBitMaskByte2Ready)       > 0);
            flags.night         = ((cbuf[6] & kBitMaskByte1Night)       > 0);
            flags.armed_stay    = ((cbuf[7] & kBitMaskByte2ArmedStay)   > 0);
            flags.low_battery   = ((cbuf[7] & kBitMaskByte2LowBat)      > 0);
            flags.check         = ((cbuf[7] & kBitMaskByte2CheckFlag)   > 0);
            flags.fire_zone     = ((cbuf[7] & kBitMaskByte2AlarmZone)   > 0);

            flags.in_alarm      = ((cbuf[8] & kBitMaskByte3InAlarm)     > 0);
            flags.ac_power      = ((cbuf[8] & kBitMaskByte3ACPower)     > 0);
            flags.chime         = ((cbuf[8] & kBitMaskByte3ChimeMode)   > 0);
            flags.bypass        = ((cbuf[8] & kBitMaskByte3Bypass)      > 0);
            flags.program_mode  = static_cast<bool>(cbuf[8] & kBitMaskByte3Program);
            flags.instant       = ((cbuf[8] & kBitMaskByte3Instant)     > 0);
            flags.armed_away    = ((cbuf[8] & kBitMaskByte3ArmedAway)   > 0);

            if (!flags.system_flag)
                flags.alarm = ((cbuf[8] & kBitMaskByte3ZoneAlarm) > 0);

            flags.promptPos = cbuf[10];
            flags.backlight = ((cbuf[12] & 0x80) > 0);

            // The backlight bit is encoded in the MSB of the first prompt byte;
            // strip it before processing characters.
            char stripped_byte12 = cbuf[12] & 0x7F;

            translate_prompt(&cbuf[12], stripped_byte12, flags.prompt1);
            translate_prompt(&cbuf[28], cbuf[28],         flags.prompt2);

            return flags;
        }

        // Translate 16 bytes of panel prompt text into a 16-char null-terminated
        // UTF-8 string in 'out' (out must be at least 17 bytes).  Extended
        // characters (>0x7F) are expanded in place to their two-byte UTF-8 form
        // via the panel's custom mapping (shift_extended_char).
        void PacketDispatcher::translate_prompt(const char *src, char first_byte, char *out)
        {
            char in[17] = {};
            memcpy(in, src, 16);
            in[0] = first_byte;

            // Two-cursor expansion: read from 'in', write to 'out'.  The previous
            // in-place shifting version wrote past the end of its own buffer for
            // any extended char, and silently dropped the last character on every
            // expansion.  Here the output is bounded explicitly and the string is
            // terminated at its real length.
            size_t o = 0;
            const size_t cap = kPromptOutChars;
            for (size_t i = 0; i < 16 && o < cap; i++)
            {
                const uint8_t c = static_cast<uint8_t>(in[i]);
                if (c == 0)
                    break;
                if (c > 0x7F)
                {
                    // Extended char: expand to its 2-byte UTF-8 form.  Stop rather
                    // than emit a truncated (invalid) sequence if only one byte fits.
                    const uint8_t u = static_cast<uint8_t>(shift_extended_char(in[i]));
                    if (o + 2 > cap)
                        break;
                    out[o++] = static_cast<char>(0xC0 | ((u >> 6) & 0x1F));
                    out[o++] = static_cast<char>(0x80 |  (u       & 0x3F));
                }
                else
                {
                    out[o++] = static_cast<char>(c);
                }
            }
            out[o] = '\0';
        }

        // ---------------------------------------------------------------------------
        // Legacy Vista20 SE multi-packet assembly
        //
        // The SE protocol sends the equivalent of one F7 frame as a sequence of
        // small 5-byte packets with varying first bytes:
        //   - First packet  : payload[0] != 0xFE/0xFF, size == 5
        //   - 0xFE packet   : first continuation, index 1
        //   - 0xFF packets  : further continuations, index 2..7
        //   - 0xF8 packet   : 33-byte final chunk, index 8 (frame complete)
        //
        // Returns true only when legacy_packet_index_ reaches 8 and the assembled
        // frame in legacy_cmd_buffer_ is complete and ready for decode_status_flags().
        // ---------------------------------------------------------------------------

        bool PacketDispatcher::assemble_legacy_se(const char *payload, int size)
        {
            const uint8_t b0 = static_cast<uint8_t>(payload[0]);

            if (b0 != 0xFE && b0 != 0xFF && size == 5)
            {
                legacy_packet_index_ = 0;
                memset(legacy_cmd_buffer_, '\0', sizeof(legacy_cmd_buffer_));
                legacy_cmd_buffer_[4] = static_cast<char>(0x80);
                for (int i = 0; i < 5; i++)
                    legacy_cmd_buffer_[i + 5] = payload[i];
            }
            else if (b0 == 0xFE && size == 5)
            {
                legacy_packet_index_ = 1;
                for (int i = 1; i < 5; i++)
                    legacy_cmd_buffer_[i + 11] = payload[i];
            }
            else if (b0 == 0xFF && size == 5
                     && legacy_packet_index_ > 0 && legacy_packet_index_ < 8)
            {
                legacy_packet_index_++;
                for (int i = 1; i < 5; i++)
                    legacy_cmd_buffer_[i + 11 + (legacy_packet_index_ - 1) * 4] = payload[i];
            }
            else if (b0 == 0xF8 && size == 33)
            {
                legacy_packet_index_ = 8;
                memcpy(&legacy_cmd_buffer_[12], &payload[1], 32);
            }
            else
            {
                legacy_packet_index_ = 0;
            }

            if (legacy_packet_index_ == 8)
            {
                legacy_cmd_buffer_[44] = '\0';
                return true;
            }
            return false;
        }

        // ---------------------------------------------------------------------------
        // F9 LRR (Contact-ID) packet handler
        //
        // Decodes the Contact-ID event fields from the raw F9 bytes and publishes
        // a human-readable string to cfg_.lrr_sensor.
        //
        // F9 payload layout (partial):
        //   [2]  — must be non-zero (message present)
        //   [3]  — message type; 0x58 = Contact-ID
        //   [8]  — high nibble = qualifier (1=event/open, 3=restore/close)
        //          low nibble + [9] = CID code (BCD)
        //   [10] — partition number
        //   [11] — data high nibble (zone/user, shifted)
        //   [12] — data low nibble (zone/user)
        // ---------------------------------------------------------------------------

        void PacketDispatcher::handle_lrr_packet(const char *payload, int size)
        {
            if (size < 13)
                return;

            if (payload[2] == 0 || static_cast<uint8_t>(payload[3]) != 0x58)
                return;

            // Decode Contact-ID fields.
            const int c_raw = (static_cast<int>(0x0F & payload[8]) << 8)
                              | static_cast<uint8_t>(payload[9]);
            const int c         = toDec(c_raw);
            const uint8_t qual  = static_cast<uint8_t>(0xF0 & payload[8]) >> 4;
            const int data      = toDec((static_cast<uint8_t>(payload[12]) >> 4)
                                        | (static_cast<uint8_t>(payload[11]) << 4));
            const uint8_t partition = static_cast<uint8_t>(payload[10]);

            if (c == 0)
                return;

            // Qualifier suffix.
            const char *qual_str;
            if (c < 400)
                qual_str = (qual == 3) ? " is Cleared" : "";
            else if (c == 570)
                qual_str = (qual == 1) ? " is Active" : " is Cleared";
            else
                qual_str = (qual == 1) ? " is Restored" : "";

            const char *lrr_str = lrr_msg_lookup(c);

            // lrr_str[0] is 'Z' for zone events, otherwise user events.
            const char *uf = (lrr_str[0] == 'Z') ? "on" : "by user";
            const std::string zn = std::to_string(data);

            char msg[100];
            if (partition)
                snprintf(msg, sizeof(msg), "CID_%d%03d: %s %s %s%s, Partition %d",
                         qual, c, &lrr_str[1], uf, zn.c_str(), qual_str, partition);
            else
                snprintf(msg, sizeof(msg), "CID_%d%03d: %s %s %s%s",
                         qual, c, &lrr_str[1], uf, zn.c_str(), qual_str);

            if (cfg_.lrr_sensor != nullptr)
                cfg_.lrr_sensor->process(msg);
        }

        // ---------------------------------------------------------------------------
        // Packet logging
        //
        // Logs the raw bytes of every received packet.  Without DEBUG_LOG the
        // payload is truncated to 8 bytes to keep the output readable.
        // ---------------------------------------------------------------------------

        void PacketDispatcher::print_packet(const char *cbuf, int type, int src, int len)
        {
            PacketType source = static_cast<PacketType>(src);

            // Checksum failures always log at ERROR.  All other packets log at
            // DEBUG, so skip the expensive string/time formatting when the build
            // is compiled below DEBUG — this runs for every received packet.
#if ESPHOME_LOG_LEVEL < ESPHOME_LOG_LEVEL_DEBUG
            if (source != PacketType::ChksumFail)
                return;
#endif

            // NOTE: do not gate this on esp_log_level_get().  ESPHome #undefs the
            // IDF ESP_LOGx macros and routes them through its own logger, gated on
            // the compile-time ESPHOME_LOG_LEVEL above.  The IDF runtime level is
            // unrelated and is left at CONFIG_LOG_DEFAULT_LEVEL_ERROR, so testing
            // it here would suppress every packet line in every build.
            const bool is_chksum_fail = (source == PacketType::ChksumFail);

            // 8 bytes is the widest tag ("KPDL" plus margin for future additions).
            char device[8];
            switch (source)
            {
                case PacketType::Unspecified:    snprintf(device, sizeof(device), "EXT");  break;
                case PacketType::ChksumFail:     snprintf(device, sizeof(device), "CHK");  break;
                case PacketType::Expander:       snprintf(device, sizeof(device), "EXP");  break;
                case PacketType::RFReceiver:     snprintf(device, sizeof(device), "RFR");  break;
                case PacketType::AUI:            snprintf(device, sizeof(device), "AUI");  break;
                case PacketType::KeypadAck:      snprintf(device, sizeof(device), "KPA");  break;
                case PacketType::Keypad:         snprintf(device, sizeof(device), "KPD");  break;
                case PacketType::LegacyProtocol: snprintf(device, sizeof(device), "KPDL"); break;
                case PacketType::LongRangeRadio: snprintf(device, sizeof(device), "LRR");  break;
                default:                         snprintf(device, sizeof(device), "   ");  break;
            }

            struct timeval tv_now;
            gettimeofday(&tv_now, NULL);
            char time_str[16];
            struct tm timeinfo;
            localtime_r(&tv_now.tv_sec, &timeinfo);
            const size_t tlen = strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
            snprintf(time_str + tlen, sizeof(time_str) - tlen,
                     ".%03ld", tv_now.tv_usec / 1000);

            char s2[48];
            if (type == 0)
                snprintf(s2, sizeof(s2), "(PANEL-->%s) [%s]", device, time_str);
            else
                snprintf(s2, sizeof(s2), "(%s-->PANEL) [%s]", device, time_str);

            bool abbr = false;
#ifndef DEBUG_LOG
            if (len > 8)
            {
                len = 8;
                abbr = true;
            }
#endif
            // Fixed hex buffer instead of an appending std::string — removes the
            // per-packet heap allocations from this task's 4 KB stack budget.
            char hex[3 * 48 + 4];
            size_t pos = 0;
            if (len < 0) len = 0;
            for (int c = 0; c < len && pos + 4 < sizeof(hex); c++)
                pos += static_cast<size_t>(snprintf(hex + pos, sizeof(hex) - pos,
                                                    "%02X ", static_cast<uint8_t>(cbuf[c])));
            if (abbr && pos + 4 < sizeof(hex))
                pos += static_cast<size_t>(snprintf(hex + pos, sizeof(hex) - pos, "..."));
            hex[pos] = '\0';

            if (is_chksum_fail)
                ESP_LOGE(TAG, "%s %s", s2, hex);
            else
                ESP_LOGD(TAG, "%s %s", s2, hex);
        }

    } // namespace alarm_panel
} // namespace esphome
