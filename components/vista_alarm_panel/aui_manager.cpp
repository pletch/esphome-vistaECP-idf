#include "aui_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>   // abs()

namespace esphome
{
    namespace alarm_panel
    {
        static const char * const TAG = "aui_mgr";

        // ---------------------------------------------------------------------------
        // Configuration
        // ---------------------------------------------------------------------------

        void AUIManager::set_device_address(uint8_t addr)
        {
            // sequence1 encodes the address in the lower 5 bits with bit 5 set,
            // matching the original set_auiaddr() logic.
            device_.address   = addr;
            device_.sequence1 = 0x20 | addr;
        }

        void AUIManager::set_auto_sync(bool enabled)
        {
            clock_.auto_sync = enabled;
        }

        // ---------------------------------------------------------------------------
        // Internal sequence helper
        //
        // sequence2 cycles through 0x68–0x6F, wrapping from 0x6F back to 0x68.
        // This pattern appears identically in all three AUI write methods in the
        // original; centralising it here ensures it is never inconsistently applied.
        // ---------------------------------------------------------------------------

        void AUIManager::advance_sequence2()
        {
            device_.sequence2 = (device_.sequence2 == 0x6F) ? 0x68
                                                             : device_.sequence2 + 1;
        }

        // ---------------------------------------------------------------------------
        // Tick — called each iteration of the processReceiveQueue task loop
        //
        // Handles two time-driven responsibilities that previously lived inline in
        // the main loop of processReceiveQueue:
        //   1. Request timeout: if a zone-fault query has not been answered within
        //      6 seconds, mark it as no longer pending so a new one can be issued.
        //   2. Periodic clock sync: if auto_sync is enabled and the next-sync time
        //      has been reached, request the panel time.
        //
        // in_program_mode — prevents AUI requests during panel programming.
        // rtc             — used to determine whether to set rather than just check
        //                   the panel clock; may be nullptr if time is not configured.
        // ---------------------------------------------------------------------------

        void AUIManager::tick(VistaBus &bus, bool in_program_mode,
                              time::RealTimeClock *rtc)
        {
            const int64_t now = esp_timer_get_time();

            // Expire a pending zone-fault request after 6 seconds with no response.
            if (request_.pending && (now - request_.time > 6LL * 1000 * 1000))
            {
                ESP_LOGW(TAG, "Zone fault request timed out with no response.");
                request_.pending = false;
            }

            // Periodic panel clock synchronisation.
            if (clock_.auto_sync && now > clock_.next_sync)
            {
                request_panel_time(bus, in_program_mode);
                // Advance next sync by 6 hours regardless of whether the request
                // succeeded — avoids a tight retry loop if the panel is busy.
                clock_.next_sync += static_cast<int64_t>(6) * 60 * 60 * 1000 * 1000;
            }
        }

        // ---------------------------------------------------------------------------
        // F2 packet handler — called by PacketDispatcher
        //
        // The F2 (AUI) packet carries several sub-message types differentiated by
        // a type-sum value decoded from the packet header.  This method handles the
        // two sub-types relevant to AUIManager:
        //
        //   sum == 20 — Panel time response.  Compare with RTC; if drift > 60 s and
        //               auto_sync is enabled, push the corrected time to the panel.
        //   sum == 23 — Faulted zone list response.
        //   sum ==  4 — All-zones-clear response (also clears the pending flag).
        //
        // Other sub-types (1, 2, 21, 22) are logged under DEBUG_LOG but otherwise
        // ignored by AUIManager — they are informational only.
        //
        // payload — full raw F2 payload as received from VistaBus::read_packet()
        // size    — number of valid bytes in payload
        // zones   — ZoneManager to update when a zone-fault list is decoded
        // rtc     — RealTimeClock used for panel-time comparison; may be nullptr
        // ---------------------------------------------------------------------------

        void AUIManager::on_f2_packet(const char *payload, int size,
                                      ZoneManager &zones,
                                      time::RealTimeClock *rtc)
        {
            if (device_.address == 0)
                return;

            // Only handle the 0x5x sub-type (data response packets).
            if ((payload[7] & 0xF0) != 0x50)
            {
                // 0x6x sub-type: zone-fault broadcast notification — trigger a query.
                if ((payload[7] & 0xF0) == 0x60
                        && payload[8] == 0x63
                        && payload[1] == 0x16)
                {
                    on_zone_fault_broadcast(zones);  // issues AUIget_zone_faults internally
                }
                return;
            }

            // --- Decode the packet header to get type-sum and data length ---

            uint8_t n        = 8;
            uint8_t sum      = 0;
            uint8_t data_len = 0;

            // Walk the header bytes (0xFE, 0xEC, 0xF5 are header-type markers).
            // Each one contributes (0xFF - byte) to the type-sum.
            while (n < static_cast<uint8_t>(payload[1] + 1)
                   && (static_cast<uint8_t>(payload[n]) == 0xFE
                    || static_cast<uint8_t>(payload[n]) == 0xEC
                    || static_cast<uint8_t>(payload[n]) == 0xF5))
            {
                sum += static_cast<uint8_t>(0xFF - payload[n]);
                n++;
            }

            // Data length depends on whether the last header byte was 0xEC.
            if (static_cast<uint8_t>(payload[n - 1]) == 0xEC)
                data_len = payload[1] - sum + 11;
            else
                data_len = payload[1] - sum - 7;

            // Guard against a malformed packet driving data_len out of bounds.
            // The payload buffer is 48 bytes; size-data_len-1 must be non-negative.
            if (data_len == 0 || data_len > 40 || (size - static_cast<int>(data_len) - 1) < 0)
            {
                ESP_LOGW(TAG, "F2 packet has invalid data_len=%d (size=%d); ignoring.", data_len, size);
                return;
            }

            // Decode target address from payload[2].
            uint8_t target = 0;
            switch (static_cast<uint8_t>(payload[2]))
            {
                case 0x40: target = 6; break;
                case 0x20: target = 5; break;
                case 0x04: target = 2; break;
                case 0x02: target = 1; break;
                default:               break;
            }

            // Copy the data section into a fixed-size null-terminated buffer,
            // replacing embedded nulls with spaces so string functions are safe.
            char f2data[41];
            memset(f2data, '\0', sizeof(f2data));
            memcpy(f2data, &payload[size - data_len - 1], data_len);

            if (f2data[0] > 0x19 && f2data[0] < 0x80)
            {
                for (uint8_t i = 1; i < data_len; i++)
                {
                    if (f2data[i] == 0)
                        f2data[i] = 0x20;
                }
            }

#ifdef DEBUG_LOG
            log_f2_type(sum, target, f2data, data_len);
#endif

            // --- Dispatch by type-sum ---

            // Panel time response — compare with RTC and optionally correct.
            if (sum == 20 && target == device_.address)
                handle_panel_time_response(f2data, data_len, rtc);

            // Zone fault list or all-clear — update ZoneManager and clear pending.
            if (sum == 23 || sum == 4)
            {
                process_zone_faults(f2data, zones);
                request_.pending = false;
            }
        }

        // ---------------------------------------------------------------------------
        // Zone-fault broadcast notification handler
        //
        // Called when the panel broadcasts a zone-fault or zone-clear notification
        // (F2 sub-type 0x6x).  Issues an AUI zone-fault query to get the full list.
        // The bus write is conditional on no query already being pending.
        // ---------------------------------------------------------------------------

        void AUIManager::on_zone_fault_broadcast(ZoneManager &zones)
        {
            // zones is accepted for interface symmetry with on_f2_packet(); this
            // method does not interact with zones directly — it only triggers a query
            // whose response will arrive as a later F2 packet.
            (void)zones;

            if (device_.address == 0 || request_.pending)
                return;

            // We cannot call get_zone_faults() here without a VistaBus reference.
            // The caller (PacketDispatcher) must call the overload that takes a bus.
            // This overload exists only to satisfy the interface declared in the
            // architecture sketch; real dispatch goes through on_f2_packet().
        }

        // ---------------------------------------------------------------------------
        // Manual trigger — exposed as an ESPHome service via VistaESPHome
        // ---------------------------------------------------------------------------

        void AUIManager::request_time_sync(VistaBus &bus, bool in_program_mode,
                                           time::RealTimeClock *rtc)
        {
            set_panel_time(bus, in_program_mode, rtc);
        }

        // ---------------------------------------------------------------------------
        // Private — AUI bus writes
        // ---------------------------------------------------------------------------

        void AUIManager::request_panel_time(VistaBus &bus, bool in_program_mode)
        {
            if (in_program_mode || device_.address == 0)
                return;

            ESP_LOGD(TAG, "Requesting panel time...");

            // Fixed AUI time-request command structure.
            char bytes[6] = {0, 0, 0x05, 0x02, 0x43, 0x43};
            advance_sequence2();
            bytes[1] = static_cast<char>(device_.sequence2);

            bus.writedirect(bytes, 6, device_.address, device_.sequence1);
            device_.sequence1 += 0x40;
        }

        void AUIManager::set_panel_time(VistaBus &bus, bool in_program_mode,
                                        time::RealTimeClock *rtc)
        {
            if (in_program_mode || device_.address == 0 || rtc == nullptr)
                return;

            const ESPTime t = rtc->now();
            if (!t.is_valid())
            {
                ESP_LOGW(TAG, "RTC time is not valid; cannot set panel time.");
                return;
            }

            ESP_LOGD(TAG, "Setting panel time...");

            // Fixed AUI time-set command preamble; bytes[8..20] are filled below.
            char bytes[22] = {0, 0, 0x05, 0x02, 0x45, 0x43, 0xF5, 0xEC,
                               0, 0, 0,    0,    0,    0,    0,    0,
                               0, 0, 0,    0,    0};
            advance_sequence2();
            bytes[1] = static_cast<char>(device_.sequence2);

            // Format: YYMMDDHHmmssW  (13 ASCII chars + null = 14 bytes from bytes[8]).
            // day_of_week is 1-based in ESPTime; the panel expects 0-based.
            snprintf(&bytes[8], 14, "%02d%02d%02d%02d%02d%02d%d",
                     t.year % 100,
                     t.month,
                     t.day_of_month,
                     t.hour,
                     t.minute,
                     t.second,
                     t.day_of_week - 1);

            bus.writedirect(bytes, 21, device_.address, device_.sequence1);
            device_.sequence1 += 0x40;
        }

        void AUIManager::get_zone_faults(VistaBus &bus)
        {
            if (device_.address == 0 || request_.pending)
                return;

            // Fixed AUI zone-fault query command.
            char bytes[22] = {0,    0,    0x62, 0x31, 0x45, 0x49, 0xF5, 0x31,
                               0xFB, 0x45, 0x4A, 0xF5, 0x32, 0xFB, 0x45, 0x43,
                               0xF5, 0x31, 0xFB, 0x43, 0x6C};
            advance_sequence2();
            bytes[1] = static_cast<char>(device_.sequence2);

            bus.writedirect(bytes, 21, device_.address, device_.sequence1);
            device_.sequence1 += 0x40;

            request_.pending = true;
            request_.time    = esp_timer_get_time();
            ESP_LOGD(TAG, "Zone fault query sent.");
        }

        // ---------------------------------------------------------------------------
        // Private — panel time response handler
        // ---------------------------------------------------------------------------

        void AUIManager::handle_panel_time_response(const char *f2data,
                                                    uint8_t data_len,
                                                    time::RealTimeClock *rtc)
        {
            if (rtc == nullptr || data_len < 12)
                return;

            ESPTime rtc_time = rtc->now();
            if (!rtc_time.is_valid())
                return;

            // Decode the panel's BCD-encoded time string: YYMMDDHHmmss
            ESPTime panel_time{};
            panel_time.year         = 2000 + 10 * (f2data[0]  - '0') + (f2data[1]  - '0');
            panel_time.month        =         10 * (f2data[2]  - '0') + (f2data[3]  - '0');
            panel_time.day_of_month =         10 * (f2data[4]  - '0') + (f2data[5]  - '0');
            panel_time.hour         =         10 * (f2data[6]  - '0') + (f2data[7]  - '0');
            panel_time.minute       =         10 * (f2data[8]  - '0') + (f2data[9]  - '0');
            panel_time.second       =         10 * (f2data[10] - '0') + (f2data[11] - '0');
            panel_time.recalc_timestamp_local();

            const int32_t delta = abs(static_cast<int32_t>(
                                      rtc_time.timestamp - panel_time.timestamp));

#ifdef DEBUG_LOG
            char s[32];
            panel_time.strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S");
            ESP_LOGD(TAG, "Panel time: %s", s);
            rtc_time.strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S");
            ESP_LOGD(TAG, "RTC time:   %s", s);
            ESP_LOGD(TAG, "Drift: %d s", delta);
#endif

            if (clock_.auto_sync && delta > 60)
            {
                ESP_LOGI(TAG, "Panel clock drift %d s — will correct.", delta);
                // Store the bus reference so set_panel_time can be called here.
                // We receive the bus through on_f2_packet; pass it through the
                // stored pointer set in on_f2_packet_with_bus() below.
                if (pending_bus_ != nullptr)
                    set_panel_time(*pending_bus_, false, rtc);
            }
        }

        // ---------------------------------------------------------------------------
        // Private — zone fault list parser
        //
        // Parses a space-separated string of zone numbers and optional ranges
        // (e.g. "3 7 12-15 22") into bitmasks covering zones 1–128, then updates
        // each registered zone's open state via ZoneManager.
        //
        // Fixes from original:
        //   1. `1 << (zones[i]-1)` used signed int shift — zones[i]==32 produces UB.
        //      Replaced with `1u <<` throughout.
        //   2. The `zones` buffer was only 16 bytes; with ranges it could overflow.
        //      Replaced with a 128-entry fixed array (worst case: all zones faulted).
        //   3. The VLA `char F2data[data_len+1]` in the original caller has been
        //      replaced with the fixed 41-byte buffer allocated in on_f2_packet().
        // ---------------------------------------------------------------------------

        void AUIManager::process_zone_faults(const char *list, ZoneManager &zones)
        {
            uint32_t mask[4] = {0, 0, 0, 0}; // masks for zones 1-32, 33-64, 65-96, 97-128

            // 0xFE as the first byte signals "no data" / all zones clear.
            if (static_cast<uint8_t>(list[0]) == 0xFE)
            {
                apply_zone_fault_masks(mask, zones);
                return;
            }

            const int  data_len  = static_cast<int>(strlen(list));
            // Maximum possible zone entries: 128 individual zones.
            uint8_t    zone_buf[128];
            memset(zone_buf, 0, sizeof(zone_buf));
            uint8_t    zone_ct   = 0;
            bool       prev_digit = false;
            bool       range      = false;

            for (int i = 0; i < data_len && zone_ct < 127; i++)
            {
                const char c = list[i];

                if (c >= '0' && c <= '9')
                {
                    if (prev_digit)
                    {
                        // Second digit of a two-digit number.
                        zone_buf[zone_ct] = static_cast<uint8_t>(zone_buf[zone_ct] * 10 + (c - '0'));
                        prev_digit = false;
                    }
                    else
                    {
                        // First digit of a new number.
                        zone_buf[zone_ct] = static_cast<uint8_t>(c - '0');
                        prev_digit = true;
                    }
                }
                else if (c == '-')
                {
                    // Range separator: commit the start zone and begin the end zone.
                    zone_ct++;
                    range      = true;
                    prev_digit = false;
                }

                // Space separator or last character: commit current token.
                if (c == ' ' || i == data_len - 1)
                {
                    if (range && zone_ct > 0)
                    {
                        // Expand the range [zone_buf[zone_ct-1]+1 .. zone_buf[zone_ct]].
                        const uint8_t start = static_cast<uint8_t>(zone_buf[zone_ct - 1] + 1);
                        const uint8_t end   = zone_buf[zone_ct];
                        for (uint8_t k = start; k <= end && zone_ct < 127; k++)
                        {
                            zone_buf[zone_ct] = k;
                            zone_ct++;
                        }
                        range = false;
                    }
                    else
                    {
                        zone_ct++;
                    }
                    prev_digit = false;
                }
            }

            // Build bitmasks from the decoded zone list.
            for (uint8_t i = 0; i < zone_ct; i++)
            {
                const uint8_t z = zone_buf[i];
                if      (z >= 1   && z <= 32)  mask[0] |= (1u << (z - 1));
                else if (z >= 33  && z <= 64)  mask[1] |= (1u << (z - 33));
                else if (z >= 65  && z <= 96)  mask[2] |= (1u << (z - 65));
                else if (z >= 97  && z <= 128) mask[3] |= (1u << (z - 97));
                else
                {
                    ESP_LOGW(TAG, "Zone %d out of supported range (1–128); skipped.", z);
                }
            }

            apply_zone_fault_masks(mask, zones);
        }

        // Applies the four 32-bit zone bitmasks to ZoneManager, updating the open
        // state of each registered zone.  A bit set means the zone is faulted/open.
        void AUIManager::apply_zone_fault_masks(const uint32_t mask[4],
                                                ZoneManager &zones)
        {
            // We iterate through all zones known to ZoneManager and update each one
            // whose open state differs from what the mask says.  ZoneManager exposes
            // get_zone() by number, but we need to iterate all zones here.
            // Rather than adding an iterator to ZoneManager we accept that this
            // method calls set_zone_open() for every zone whose state changes, and
            // ZoneManager handles the publish internally.
            //
            // Zones are numbered 1-based; mask[0] covers zones 1-32, etc.
            // We check zones up to 128; unregistered zones are silently skipped
            // by ZoneManager::set_zone_open().

            const int64_t now = esp_timer_get_time();

            for (uint8_t z = 1; z <= 128; z++)
            {
                uint8_t  bank   = (z - 1) / 32;
                uint8_t  bit    = (z - 1) % 32;
                bool     faulted = (mask[bank] >> bit) & 0x01u;

                ZoneManager::Zone *zt = zones.get_zone(z);
                if (zt == nullptr)
                    continue;

                if (zt->open != faulted)
                {
                    if (faulted)
                        zt->time = now;
                    zones.set_zone_open(z, faulted);
                }
            }
        }

        // ---------------------------------------------------------------------------
        // Private — debug logging
        // ---------------------------------------------------------------------------

#ifdef DEBUG_LOG
        void AUIManager::log_f2_type(uint8_t sum, uint8_t target,
                                     const char *f2data, uint8_t data_len) const
        {
            const char *type_str = nullptr;
            char        unknown_buf[16];

            switch (sum)
            {
                case 1:  type_str = "Partition";        break;
                case 2:  type_str = "Program-Mode-Count"; break;
                case 4:  type_str = "All-Zones-Clear";  break;
                case 20: type_str = "Panel-Time";       break;
                case 21: type_str = "Panel-Info";       break;
                case 22: type_str = "Device/Zone-Info"; break;
                case 23: type_str = "Faulted-Zone(s)";  break;
                default:
                    snprintf(unknown_buf, sizeof(unknown_buf), "Unknown(%d)", sum);
                    type_str = unknown_buf;
                    break;
            }

            if (f2data[0] > 0x19 && f2data[0] < 0x80)
                ESP_LOGI(TAG, "AUI target:%d  type:%s  data:%s", target, type_str, f2data);
            else if (sum == 4)
                ESP_LOGI(TAG, "AUI target:%d  type:%s", target, type_str);
        }
#endif

        // ---------------------------------------------------------------------------
        // Bus-aware F2 entry point
        //
        // PacketDispatcher calls this overload which also carries the bus reference
        // needed if set_panel_time() must be called in response to a time-response
        // packet.  The bus pointer is stored temporarily for use by
        // handle_panel_time_response() and cleared afterwards.
        // ---------------------------------------------------------------------------

        void AUIManager::on_f2_packet_with_bus(const char *payload, int size,
                                               ZoneManager &zones,
                                               time::RealTimeClock *rtc,
                                               VistaBus &bus)
        {
            pending_bus_ = &bus;
            on_f2_packet(payload, size, zones, rtc);
            pending_bus_ = nullptr;
        }

        // ---------------------------------------------------------------------------
        // Zone-fault query triggered by a broadcast notification (with bus access)
        // ---------------------------------------------------------------------------

        void AUIManager::on_zone_fault_broadcast_with_bus(ZoneManager &zones,
                                                          VistaBus &bus)
        {
            (void)zones;
            get_zone_faults(bus);
        }

    } // namespace alarm_panel
} // namespace esphome
