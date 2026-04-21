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
        static const char * const TAG = "pkt_disp";

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
                else if (payload[0] != 0 && src == static_cast<int>(kKeypadAck))
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
                cfg_.chksum_fail_sensor->publish_state(std::to_string(chksum_failures_));
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

            // Prompt line 1 (bytes 12..27): translate extended (>0x7F) characters
            // to their UTF-8 two-byte encoding using the panel's custom mapping.
            {
                char tempbuf[18];
                memset(tempbuf, 0, sizeof(tempbuf));
                memcpy(tempbuf, &cbuf[12], 16);
                tempbuf[0] = stripped_byte12;

                for (int i = 1; i < 18; i++)
                {
                    if (!tempbuf[i])
                        break;
                    if (static_cast<uint8_t>(tempbuf[i]) > 0x7F)
                    {
                        tempbuf[i] = shift_extended_char(tempbuf[i]);
                        char buf[32];
                        memcpy(buf, &tempbuf[i+1], 32 - i - 1);
                        tempbuf[i+1] = static_cast<char>(0x80 | (tempbuf[i] & 0x3F));
                        tempbuf[i]   = static_cast<char>(0xC0 | ((tempbuf[i] >> 6) & 0x1F));
                        memcpy(&tempbuf[i+2], buf, 32 - i - 2);
                        i++;
                    }
                }
                memcpy(flags.prompt1, tempbuf, 16);
                flags.prompt1[16] = '\0';
            }

            // Prompt line 2 (bytes 28..43).
            {
                char tempbuf[18];
                memset(tempbuf, 0, sizeof(tempbuf));
                memcpy(tempbuf, &cbuf[28], 16);

                for (int i = 0; i < 18; i++)
                {
                    if (!tempbuf[i])
                        break;
                    if (static_cast<uint8_t>(tempbuf[i]) > 0x7F)
                    {
                        tempbuf[i] = shift_extended_char(tempbuf[i]);
                        char buf[32];
                        memcpy(buf, &tempbuf[i+1], 32 - i - 1);
                        tempbuf[i+1] = static_cast<char>(0x80 | (tempbuf[i] & 0x3F));
                        tempbuf[i]   = static_cast<char>(0xC0 | ((tempbuf[i] >> 6) & 0x1F));
                        memcpy(&tempbuf[i+2], buf, 32 - i - 2);
                        i++;
                    }
                }
                memcpy(flags.prompt2, tempbuf, 16);
                flags.prompt2[16] = '\0';
            }

            return flags;
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
            char s1[4];
            std::string s;
            char s2[48];
            PacketType source = static_cast<PacketType>(src);
            char device[5];
            switch (source)
            {
                case kUnspecified:      sprintf(device, "EXT");  break;
                case kChksumFail:       sprintf(device, "CHK");  break;
                case kExpander:         sprintf(device, "EXP");  break;
                case kRFReceiver:       sprintf(device, "RFR");  break;
                case kAUI:              sprintf(device, "AUI");  break;
                case kKeypadAck:        sprintf(device, "KPA");  break;
                case kKeypad:           sprintf(device, "KPD");  break;
                case kLegacyProtocol:   sprintf(device, "KPDL"); break;
                case kLongRangeRadio:   sprintf(device, "LRR");  break;
                default:                sprintf(device, "   ");  break;
            }

            struct timeval tv_now;
            gettimeofday(&tv_now, NULL);
            char time_str[16];
            struct tm timeinfo;
            localtime_r(&tv_now.tv_sec, &timeinfo);
            strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
            snprintf(time_str + strlen(time_str), sizeof(time_str) - strlen(time_str),
                     ".%03ld", tv_now.tv_usec / 1000);

            if (type == 0)
                sprintf(s2, "(PANEL-->%s) [%s]", device, time_str);
            else
                sprintf(s2, "(%s-->PANEL) [%s]", device, time_str);

            bool abbr = false;
#ifndef DEBUG_LOG
            if (len > 8)
            {
                len = 8;
                abbr = true;
            }
#endif
            for (int c = 0; c < len; c++)
            {
                sprintf(s1, "%02X ", static_cast<uint8_t>(cbuf[c]));
                s += s1;
            }
            if (abbr)
                s += "...";

            if (source == kChksumFail)
                ESP_LOGE(TAG, "%s %s", s2, s.c_str());
            else
                ESP_LOGD(TAG, "%s %s", s2, s.c_str());
        }

    } // namespace alarm_panel
} // namespace esphome
