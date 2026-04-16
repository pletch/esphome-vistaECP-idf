// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#define ESPHOME_LOG_LEVEL ESPHOME_LOG_LEVEL_DEBUG
#include "esphome/core/log.h"
#include "zone_manager.h"
#include "sensor_interfaces.h"
#include "constants.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

namespace esphome
{
    namespace alarm_panel
    {
        static const char * const TAG = "zone_mgr";

        // Suppress ECP-path publish when the direct path updated within this window.
        static constexpr int64_t kDirectSuppressUs = 3'000'000LL;  // 3 seconds

        // RAII mutex guard — takes the semaphore on construction, gives on destruction.
        // Handles all early-return paths in methods that access zones_.
        struct ZoneMutexGuard
        {
            SemaphoreHandle_t sem;
            explicit ZoneMutexGuard(SemaphoreHandle_t s) : sem(s)
            {
                xSemaphoreTake(sem, portMAX_DELAY);
            }
            ~ZoneMutexGuard() { xSemaphoreGive(sem); }
        };

        // ---------------------------------------------------------------------------
        // Construction / destruction
        // ---------------------------------------------------------------------------

        ZoneManager::ZoneManager()
        {
            zone_mutex_ = xSemaphoreCreateMutex();
            ESP_ERROR_CHECK(zone_mutex_ == nullptr ? ESP_ERR_NO_MEM : ESP_OK);
        }

        ZoneManager::~ZoneManager()
        {
            if (zone_mutex_ != nullptr)
            {
                vSemaphoreDelete(zone_mutex_);
                zone_mutex_ = nullptr;
            }
        }

        // ---------------------------------------------------------------------------
        // Internal helpers
        // ---------------------------------------------------------------------------

        // Translate an rfloop number (1–4) into the bit mask used in RF status bytes.
        static uint8_t rfloop_to_mask(uint8_t rfloop)
        {
            switch (rfloop)
            {
                case 1:  return 0x80;
                case 2:  return 0x20;
                case 3:  return 0x10;
                case 4:  return 0x40;
                default: return 0x80;
            }
        }

        // Publish both sensors for a zone based on its current state flags.
        // Text sensor format: [B|A|""][T|O|C][L|""]
        //   B = bypassed, A = alarm, T = trouble/check, O = open, C = closed, L = low battery
        // Binary sensor: true when zone is open OR in check/trouble state.
        void ZoneManager::publish_zone(Zone *zt)
        {
            if (zt->text_sensor != nullptr)
            {
                // Fault/open/closed status character
                const char *zs = zt->check ? "T" : zt->open ? "O" : "C";
                // Prefix: bypass takes priority over alarm
                const char *prefix = zt->bypass ? "B" : zt->alarm ? "A" : "";
                // Suffix: low RF battery
                const char *lb = zt->rflowbat ? "L" : "";

                std::string msg = std::string(prefix) + zs + lb;
                zt->text_sensor->process(msg);
            }

            if (zt->binary_sensor != nullptr)
                zt->binary_sensor->process(zt->open || zt->check);
        }

        // ---------------------------------------------------------------------------
        // Registration
        // ---------------------------------------------------------------------------

        void ZoneManager::register_zone(vistaECPBinarySensor *sensor,
                                        uint8_t partition,
                                        uint8_t zone_number,
                                        uint32_t rf_serial,
                                        uint8_t rf_loop,
                                        bool emulated)
        {
            // If the zone already exists (registered by register_zone_text earlier,
            // or a duplicate call), update the binary sensor and RF fields in place.
            for (auto &z : zones_)
            {
                if (z.zone == zone_number)
                {
                    z.binary_sensor = sensor;
                    z.rfserial      = rf_serial;
                    z.rfloop        = rf_loop;
                    ESP_LOGI(TAG, "Updated binary sensor for zone %d  rfserial:%lu  rfloop:%d",
                             z.zone, z.rfserial, z.rfloop);
                    return;
                }
            }

            // New zone entry.
            Zone z;
            z.binary_sensor = sensor;
            z.partition     = partition;
            z.zone          = zone_number;
            z.rfserial      = rf_serial;
            z.rfloop        = rf_loop;
            z.rfnext_hb     = 1;   // non-zero sentinel: heartbeat timer not yet initialised
            z.active        = true;
            zones_.push_back(z);

            if (rf_serial == 0)
            {
                if (emulated)
                    ESP_LOGI(TAG, "Registered emulated hardwired zone %d", zone_number);
                else
                    ESP_LOGI(TAG, "Registered hardwired zone %d", zone_number);
            }
            else
            {
                if (emulated)
                    ESP_LOGI(TAG, "Registered emulated wireless zone %d  rfserial:%lu  rfloop:%d",
                             zone_number, rf_serial, rf_loop);
                else
                    ESP_LOGI(TAG, "Registered wireless zone %d  rfserial:%lu  rfloop:%d",
                             zone_number, rf_serial, rf_loop);
            }
        }

        void ZoneManager::register_zone_text(vistaECPTextSensor *sensor,
                                             uint8_t partition,
                                             uint8_t zone_number)
        {
            // Update in place if zone already exists.
            for (auto &z : zones_)
            {
                if (z.zone == zone_number)
                {
                    z.text_sensor = sensor;
                    ESP_LOGI(TAG, "Updated text sensor for zone %d", z.zone);
                    return;
                }
            }

            // New zone entry — binary sensor will be attached later if needed.
            Zone z;
            z.text_sensor = sensor;
            z.partition   = partition;
            z.zone        = zone_number;
            z.active      = true;
            zones_.push_back(z);
            ESP_LOGI(TAG, "Registered zone %d (text sensor only)", zone_number);
        }

        // ---------------------------------------------------------------------------
        // Zone text sensor (separate from per-zone sensors)
        // ---------------------------------------------------------------------------

        void ZoneManager::register_zone_status_sensor(vistaECPTextSensor *sensor)
        {
            zone_status_sensor_ = sensor;
        }

        ZoneManager::Zone *ZoneManager::get_zone(uint16_t zone_number)
        {
            for (auto &z : zones_)
            {
                if (z.zone == zone_number)
                    return &z;
            }
            return nullptr;
        }

        ZoneManager::Zone *ZoneManager::get_zone_by_rf_serial(uint32_t serial)
        {
            for (auto &z : zones_)
            {
                if (z.rfserial == serial)
                    return &z;
            }
            return nullptr;
        }

        // ---------------------------------------------------------------------------
        // Individual state setters
        // These are called by PartitionManager::process_status_flags() when it
        // decodes zone-level events from F7 status bytes.  Each setter guards
        // against redundant transitions and publishes immediately on change.
        // ---------------------------------------------------------------------------

        void ZoneManager::set_zone_open(uint8_t zone_number, bool open)
        {
            ZoneMutexGuard guard(zone_mutex_);
            Zone *z = get_zone(zone_number);
            if (z == nullptr || !z->active)
                return;
            if (z->open == open)
                return;
            z->open   = open;
            z->check  = false;  // open and check are mutually exclusive
            z->bypass = false;  // opening clears bypass
            z->time   = esp_timer_get_time();
            publish_zone(z);
        }

        void ZoneManager::set_zone_bypass(uint8_t zone_number, bool bypass)
        {
            ZoneMutexGuard guard(zone_mutex_);
            Zone *z = get_zone(zone_number);
            if (z == nullptr || !z->active)
                return;
            if (z->bypass == bypass)
                return;
            z->bypass = bypass;
            z->time   = esp_timer_get_time();
            publish_zone(z);
        }

        void ZoneManager::set_zone_alarm(uint8_t zone_number, bool alarm)
        {
            ZoneMutexGuard guard(zone_mutex_);
            Zone *z = get_zone(zone_number);
            if (z == nullptr || !z->active)
                return;
            if (z->alarm == alarm)
                return;
            z->alarm = alarm;
            z->time  = esp_timer_get_time();
            publish_zone(z);
        }

        void ZoneManager::set_zone_check(uint8_t zone_number, bool check)
        {
            ZoneMutexGuard guard(zone_mutex_);
            Zone *z = get_zone(zone_number);
            if (z == nullptr || !z->active)
                return;
            if (z->check == check)
                return;
            z->check = check;
            if (check)
            {
                // Check/trouble supersedes open and alarm.
                z->open  = false;
                z->alarm = false;
            }
            z->time = esp_timer_get_time();
            publish_zone(z);
        }

        void ZoneManager::set_zone_lowbat(uint8_t zone_number, bool low)
        {
            ZoneMutexGuard guard(zone_mutex_);
            Zone *z = get_zone(zone_number);
            if (z == nullptr || !z->active)
                return;
            if (z->rflowbat == low)
                return;
            z->rflowbat = low;
            publish_zone(z);
        }

        // ---------------------------------------------------------------------------
        // FA expander packet decode
        //
        // Called by PacketDispatcher when a green-wire (type==1) FA packet of exactly
        // 4 bytes arrives from the monitor task.  Decodes zone number from the
        // expander address byte and zone-within-expander bits, then updates state.
        //
        // Packet layout (from protocol reverse engineering in the original):
        //   payload[0] — expander address (0x07–0x0B)
        //   payload[2] — zone-within-expander index bits
        //   payload[3] — bit 5 = open/closed (1=open), upper bits = zone sub-index
        // ---------------------------------------------------------------------------

        void ZoneManager::on_expander_zone_packet(const char *payload, int size)
        {
            if (size != 4)
                return;

            ZoneMutexGuard guard(zone_mutex_);

            const uint8_t addr = static_cast<uint8_t>(payload[0]);
            int z = payload[3] >> 5;

            switch (addr)
            {
                case 0x07: z +=  8 + (payload[2] << 3); break;
                case 0x08: z += 16 + (payload[2] << 3); break;
                case 0x09: z += 24 + (payload[2] << 3); break;
                case 0x0A: z += 32 + (payload[2] << 3); break;
                case 0x0B: z += 40 + (payload[2] << 3); break;
                default:
                    ESP_LOGW(TAG, "FA packet: unrecognised expander address 0x%02X", addr);
                    return;
            }

            const bool open = (static_cast<uint8_t>(payload[3]) >> 4) & 0x01;
            Zone *zt = get_zone(static_cast<uint16_t>(z));
            if (zt != nullptr && zt->active)
            {
                const int64_t now = esp_timer_get_time();
                zt->open = open;
                zt->time = now;

                // If on_zone_direct() already published this event, suppress the
                // redundant ECP-path publish to prevent HA state flapping.
                if ((now - zt->last_direct_time) < kDirectSuppressUs)
                {
                    ESP_LOGD(TAG, "ECP-path publish suppressed for zone %d (direct path active)", z);
                }
                else
                {
                    publish_zone(zt);
                }
#ifdef DEBUG_LOG
                ESP_LOGD(TAG, "FA expander zone %d: %s", z, open ? "open" : "closed");
#endif
            }
        }

        // ---------------------------------------------------------------------------
        // FB RF receiver packet decode
        //
        // Called by PacketDispatcher when a green-wire (type==1) FB packet of
        // kRFZoneMessageLength bytes arrives from the monitor task.  Validates the
        // two's-complement checksum, extracts the 20-bit device serial (0–0xFFFFF), and updates
        // the matching zone's open/lowbat state.
        //
        // Packet layout:
        //   payload[0]     — RF receiver address byte
        //   payload[1]     — sequence byte
        //   payload[2]     — bit 7 = valid-sensor flag; bits 3:0 = serial bits 19:16
        //   payload[3]     — serial bits 15:8
        //   payload[4]     — serial bits 7:0
        //   payload[5]     — status byte (loop bits, low-bat, supervision flags)
        //   payload[6]     — two's-complement checksum over bytes 0–5
        //
        // Status byte bit meanings (from protocol comments):
        //   bit 7 (0x80) — loop 1
        //   bit 5 (0x20) — loop 2
        //   bit 4 (0x10) — loop 3
        //   bit 6 (0x40) — loop 4
        //   bit 2 (0x04) — supervision/heartbeat flag
        //   bit 1 (0x02) — low battery
        //   bit 0 (0x01) — unknown / additional heartbeat indicator
        //
        // Heartbeats (payload[5] & 0x04 || payload[5] & 0x01) are ignored for
        // zone state but the serial is still published to the rf_messages sensor.
        //
        // Returns the formatted serial string so PacketDispatcher can pass it to
        // the rf_messages text sensor, or an empty string if the checksum failed.
        // ---------------------------------------------------------------------------

        std::string ZoneManager::on_rf_zone_packet(const char *payload, int size)
        {
            if (size != kRFZoneMessageLength)
                return "";

            // Validate two's-complement checksum over bytes 0–5.
            uint8_t chksum = 0;
            for (int i = 0; i < kRFZoneMessageLength - 1; i++)
                chksum += static_cast<uint8_t>(payload[i]);
            chksum = static_cast<uint8_t>(~chksum + 1);

            if (chksum != static_cast<uint8_t>(payload[6]))
            {
                ESP_LOGW(TAG, "FB RF packet checksum failed (got 0x%02X, expected 0x%02X)",
                         static_cast<uint8_t>(payload[6]), chksum);
                return "";
            }

            // Reconstruct 20-bit device serial from bytes 2–4.
            // Byte 2: bit 7 = valid-sensor flag (always 1, masked off); bits 3:0 = serial bits 19:16.
            const uint32_t serial =
                (static_cast<uint32_t>(payload[2] & 0x0F) << 16)
                + (static_cast<uint32_t>(static_cast<uint8_t>(payload[3])) << 8)
                +  static_cast<uint32_t>(static_cast<uint8_t>(payload[4]));

            // Format the display string for the rf_messages sensor.
            // Format: "<serial>:0x<status>"  e.g. "231357:0x80"
            // The status byte encodes loop bits [7,6,5,4], lowbat [1], heartbeat [2,0].
            const uint8_t status_byte = static_cast<uint8_t>(payload[5]);
            char serial_str[20];
            snprintf(serial_str, sizeof(serial_str), "%lu%04lu:0x%02X",
                     serial / 10000, serial % 10000, status_byte);

#ifdef DEBUG_LOG
            ESP_LOGI(TAG, "RFX: %s", serial_str);
#endif

            // Supervision / heartbeat packets: skip zone state update, still report serial.
            if ((status_byte & 0x04) || (status_byte & 0x01))
                return std::string(serial_str);

            ZoneMutexGuard guard(zone_mutex_);

            Zone *zt = get_zone_by_rf_serial(serial);
            if (zt != nullptr && zt->active)
            {
                const uint8_t mask = rfloop_to_mask(zt->rfloop);
                const bool open    = (status_byte & mask) != 0;
                const bool lowbat  = (status_byte & 0x02) != 0;

                const int64_t now = esp_timer_get_time();
                zt->time     = now;
                zt->open     = open;
                zt->rflowbat = lowbat;

                // If the direct fast-path already published this event, skip the
                // redundant publish to prevent flapping / duplicate HA transitions.
                if ((now - zt->last_direct_time) < kDirectSuppressUs)
                {
                    ESP_LOGD(TAG, "ECP-path publish suppressed for serial %lu (direct path active)",
                             (unsigned long)serial);
                }
                else
                {
                    publish_zone(zt);
                }
            }

            return std::string(serial_str);
        }

        // ---------------------------------------------------------------------------
        // Direct fast-path RF update
        //
        // Called by VistaESPHome::rf_direct_task() immediately after CC1101Receiver
        // decodes a valid, non-duplicate, non-heartbeat packet.  Uses the raw
        // ecp_status byte (same bit layout as the FB status byte from the panel)
        // to update zone state and publish to Home Assistant without waiting for the
        // panel's F1 poll → FA/FB round-trip.
        //
        // Records last_direct_time so that on_rf_zone_packet() can recognise and
        // suppress the later ECP-path echo for the same event.
        // ---------------------------------------------------------------------------

        void ZoneManager::on_rf_direct(uint32_t serial, uint8_t ecp_status)
        {
            // Heartbeat packets carry no zone-state information.  The ECP path
            // still needs them for panel supervision, so they are not filtered
            // in CC1101Receiver; guard here as a safety net.
            if ((ecp_status & 0x04) || (ecp_status & 0x01))
                return;

            ZoneMutexGuard guard(zone_mutex_);

            Zone *zt = get_zone_by_rf_serial(serial);
            if (zt == nullptr || !zt->active)
                return;

            const uint8_t mask = rfloop_to_mask(zt->rfloop);
            const bool open    = (ecp_status & mask) != 0;
            const bool lowbat  = (ecp_status & 0x02) != 0;

            const int64_t now    = esp_timer_get_time();
            zt->last_direct_time = now;
            zt->time             = now;
            zt->open             = open;
            zt->rflowbat         = lowbat;

            ESP_LOGD(TAG, "RF direct: serial=%lu  open=%d  lowbat=%d",
                     (unsigned long)serial, open, lowbat);

            publish_zone(zt);
        }

        // ---------------------------------------------------------------------------
        // Direct fast-path for emulated zone fault changes
        //
        // Called by VistaESPHome::svc_set_zone_fault() immediately before it sends
        // the fault state to the panel via send_emulated_fault().  Publishes the new
        // open state to Home Assistant without waiting for the panel's ECP echo
        // (FA expander packet for hardwired zones, FB RF packet for RF zones).
        //
        // Records last_direct_time so that both on_expander_zone_packet() and
        // on_rf_zone_packet() will suppress the later ECP-path echo.
        // ---------------------------------------------------------------------------

        void ZoneManager::on_zone_direct(uint8_t zone_number, bool fault)
        {
            ZoneMutexGuard guard(zone_mutex_);

            Zone *z = get_zone(zone_number);
            if (z == nullptr || !z->active)
                return;

            const int64_t now   = esp_timer_get_time();
            z->last_direct_time = now;
            z->time             = now;
            z->open             = fault;

            ESP_LOGD(TAG, "Zone direct: zone=%d  fault=%d", zone_number, fault);

            publish_zone(z);
        }

        // ---------------------------------------------------------------------------
        // Emulated zone fault / tamper — called from VistaESPHome service callbacks
        // via VistaBus (unchanged path).  
        // ---------------------------------------------------------------------------

        void ZoneManager::send_emulated_fault(uint8_t zone_number,
                                              bool fault,
                                              VistaBus &bus)
        {
            const Zone *z = get_zone(zone_number);
            if (z == nullptr)
            {
                ESP_LOGW(TAG, "send_emulated_fault: zone %d not registered", zone_number);
                return;
            }

            if (z->rfserial != 0)
            {
                const uint8_t mask = rfloop_to_mask(z->rfloop);
                // fault=true:  set the loop bit  (0x80 | mask)
                // fault=false: clear the loop bit (0x80 & mask)
                const uint8_t msg = fault 
                    ? static_cast<uint8_t>(0x80 | mask)
                    : static_cast<uint8_t>(0x80 & mask);
                ESP_LOGI(TAG, "Emulated RF zone %d fault:%d  serial:%lu",
                         zone_number, fault, z->rfserial);
                bus.sendRFmsg(z->rfserial, msg);
            }
            else
            {
                ESP_LOGI(TAG, "Emulated hardwired zone %d fault:%d", zone_number, fault);
                bus.setExpFaultBits(zone_number, fault);
            }
        }

        void ZoneManager::send_emulated_tamper(uint8_t zone_number,
                                               bool tamper_active,
                                               VistaBus &bus)
        {
            bus.setExpTamper(zone_number, tamper_active);
        }

        // ---------------------------------------------------------------------------
        // Refresh — called from PartitionManager::process_status_flags() once per F7
        // cycle after zone events have been applied.  Clears stale zone states that
        // are no longer consistent with the panel's current light-state, then
        // publishes the combined zone-status string if it has changed.
        // ---------------------------------------------------------------------------

        void ZoneManager::refresh(const LightStates &partition_lights, int64_t ttl)
        {
            ZoneMutexGuard guard(zone_mutex_);
            const int64_t now = esp_timer_get_time();
            std::string status_msg;
            // Pre-reserve enough space to avoid repeated small allocations for a
            // typical zone count; exact capacity is not critical.
            status_msg.reserve(64);

            for (auto &z : zones_)
            {
                if (!z.active || !z.partition)
                    continue;

                // --- State reconciliation against panel light flags ---

                // If the panel reports ready, any open (non-bypassed) zone is stale.
                if (!z.bypass && z.open && partition_lights.ready)
                {
                    ESP_LOGE(TAG, "Zone %d: open but panel ready — clearing open state", z.zone);
                    z.open  = false;
                    z.check = false;
                    z.alarm = false;
                }

                // If the bypass light is off, clear any lingering bypass flag.
                if (z.bypass && !partition_lights.bypass)
                    z.bypass = false;

                // If the alarm light is off, clear any lingering alarm flag.
                if (z.alarm && !partition_lights.alarm)
                    z.alarm = false;

                // --- TTL-based expiry ---

                // Open and check states expire after TTL microseconds if the panel
                // has not re-reported them.  Bypassed zones are exempt — they persist
                // until the panel clears the bypass flag.
                if (!z.bypass && z.open && (now - z.time) > ttl)
                    z.open = false;

                if (!z.bypass && z.check && (now - z.time) > ttl)
                    z.check = false;

                // Publish the potentially-changed zone state.
                publish_zone(&z);

                // --- Build combined zone status string ---
                // Format: [OP|AL|BY|CK|LB]:<zone_number>, comma-separated.
                // Only flags that are currently active contribute an entry.
                char seg[16];
                auto append_seg = [&](const char *prefix)
                {
                    if (!status_msg.empty())
                        snprintf(seg, sizeof(seg), ",%s:%d", prefix, z.zone);
                    else
                        snprintf(seg, sizeof(seg),  "%s:%d", prefix, z.zone);
                    status_msg += seg;
                };

                if (z.open)    append_seg("OP");
                if (z.alarm)   append_seg("AL");
                if (z.bypass)  append_seg("BY");
                if (z.check)   append_seg("CK");
                if (z.rflowbat)append_seg("LB");
            }

            // Publish the zone-status string only when it has changed.
            if (status_msg != previous_zone_status_ && zone_status_sensor_ != nullptr)
                zone_status_sensor_->process(status_msg);

            previous_zone_status_ = status_msg;
        }

        // ---------------------------------------------------------------------------
        // RF heartbeat emulation
        //
        // Initialises heartbeat timers for all RF zones immediately after the task
        // starts (staggered within the first 1–10 minutes so they do not all fire
        // simultaneously), then fires periodic supervision messages at 70–90 minute
        // intervals with random jitter.
        // ---------------------------------------------------------------------------

        void ZoneManager::init_rf_heartbeat_timers()
        {
            ZoneMutexGuard guard(zone_mutex_);
            for (auto &z : zones_)
            {
                if (z.rfnext_hb != 0)
                {
                    // Stagger initial heartbeats across 1–10 minutes.
                    z.rfnext_hb = esp_timer_get_time()
                                  + 1LL * 60 * 1000 * 1000
                                  + static_cast<int64_t>(esp_random() % 10) * 60 * 1000 * 1000;
                }
            }
        }

        void ZoneManager::handle_rf_heartbeats(VistaBus &bus)
        {
            ZoneMutexGuard guard(zone_mutex_);
            const int64_t now = esp_timer_get_time();
            for (auto &z : zones_)
            {
                if (z.rfnext_hb == 0 || z.rfserial == 0)
                    continue;

                if (now <= z.rfnext_hb)
                    continue;

                // Supervision heartbeat: only the supervision bit (0x04) is set.
                // All loop-fault bits are clear — the zone is closed, not faulted.
                static constexpr uint8_t msg = 0x04;
                bus.sendRFmsg(z.rfserial, msg);

                // Schedule next heartbeat: 70–90 minutes with random jitter.
                z.rfnext_hb = now
                              + 70ULL * 60 * 1000 * 1000
                              + static_cast<int64_t>(esp_random() % 20) * 60 * 1000 * 1000;
            }
        }

        // ---------------------------------------------------------------------------
        // Initial state broadcast
        // ---------------------------------------------------------------------------

        void ZoneManager::publish_initial_states()
        {
            for (auto &z : zones_)
                publish_zone(&z);

            if (zone_status_sensor_ != nullptr)
                zone_status_sensor_->process("");
            previous_zone_status_ = "";
        }

        // ---------------------------------------------------------------------------
        // Zone status string accessor (for use during initial sensor publish)
        // ---------------------------------------------------------------------------

        std::string ZoneManager::build_zone_status_string() const
        {
            std::string msg;
            msg.reserve(64);
            char seg[16];

            for (const auto &z : zones_)
            {
                if (!z.active || !z.partition)
                    continue;

                auto append_seg = [&](const char *prefix)
                {
                    if (!msg.empty())
                        snprintf(seg, sizeof(seg), ",%s:%d", prefix, z.zone);
                    else
                        snprintf(seg, sizeof(seg),  "%s:%d", prefix, z.zone);
                    msg += seg;
                };

                if (z.open)    append_seg("OP");
                if (z.alarm)   append_seg("AL");
                if (z.bypass)  append_seg("BY");
                if (z.check)   append_seg("CK");
                if (z.rflowbat)append_seg("LB");
            }

            return msg;
        }

    } // namespace alarm_panel
} // namespace esphome
