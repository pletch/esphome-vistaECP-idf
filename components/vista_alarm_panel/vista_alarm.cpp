// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

/*
 * VistaESPHome — ESPHome component shell for the Vista ECP alarm panel integration.
 *
 * All substantive logic lives in the five collaborator classes:
 *   ZoneManager      — zone state, RF heartbeats, expander/RF packet decode
 *   PartitionManager — partition state machine, sensor publishing
 *   CommandWriter    — all outbound panel commands (arm/disarm/keypress)
 *   AUIManager       — AUI clock sync and zone-fault queries
 *   PacketDispatcher — receive-queue dispatch loop
 *
 * This file contains only construction, setup/update/stop, and the thin
 * FreeRTOS task wrapper that drives PacketDispatcher::dispatch_one().
 */

#include "vista_alarm.h"
#include "constants.h"
#include "panel_text.h"
//#include "esp_log.h"
#include "esp_timer.h"
#include "esphome/components/ota/ota_backend.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

namespace esphome
{
namespace alarm_panel
{

// ---------------------------------------------------------------------------
// OTAGuard — suspends flash-touching tasks during OTA cache-off windows.
// Defined here (not in the header) so ota_backend.h is only included in
// this translation unit, not in every file that includes vista_alarm.h.
// ---------------------------------------------------------------------------
struct VistaESPHome::OTAGuard : public ota::OTAGlobalStateListener {
    VistaESPHome *owner {nullptr};

    void on_ota_global_state(ota::OTAState state, float, uint8_t,
                             ota::OTAComponent *) override
    {
        if (!owner) return;
        if (state == ota::OTA_STARTED) {
            ESP_LOGI("vista", "OTA start — suspending tasks");
            // Stop consumers first, then feeders, so no task is mid-call into
            // bus code when flash cache is disabled by the OTA writer.
            // taskYIELD() after each suspend lets the target task actually reach
            // its next scheduling point on the other core before we proceed.
            if (owner->processReceiveQHandle) {
                vTaskSuspend(owner->processReceiveQHandle);
                taskYIELD();
            }
#ifdef CC1101_RECEIVER
            if (owner->rf_direct_task_handle_) {
                vTaskSuspend(owner->rf_direct_task_handle_);
                taskYIELD();
            }
            if (owner->cc1101_receiver_)
                owner->cc1101_receiver_->suspend();
#endif
            owner->vistabus_.suspend_monitor();
            owner->vistabus_.suspend_rx_tx();
        } else if (state == ota::OTA_COMPLETED
                || state == ota::OTA_ABORT
                || state == ota::OTA_ERROR) {
            ESP_LOGI("vista", "OTA end — resuming tasks");
            owner->vistabus_.resume_tasks();
            if (owner->processReceiveQHandle)
                vTaskResume(owner->processReceiveQHandle);
#ifdef CC1101_RECEIVER
            if (owner->cc1101_receiver_)
                owner->cc1101_receiver_->resume();
            if (owner->rf_direct_task_handle_)
                vTaskResume(owner->rf_direct_task_handle_);
#endif
        }
    }
};

} // namespace alarm_panel
} // namespace esphome

namespace esphome
{
    namespace alarm_panel
    {
        // ---------------------------------------------------------------------------
        // Construction
        //
        // Collaborators that hold references (cmd_) must be initialised after the
        // objects they reference (vistabus_, partitions_), which is guaranteed by
        // the member declaration order in the header.  C++ initialises members in
        // declaration order regardless of the order written in the initialiser list.
        // ---------------------------------------------------------------------------

        VistaESPHome::VistaESPHome(char    kpaddr,
                                   int     receivePin,
                                   int     transmitPin,
                                   int     uartnum1,
                                   int     monitorTxPin,
                                   int     uartnum2)
            : cmd_           (vistabus_, partitions_)   // holds refs — must come after both
            , keypad_addr1_  (kpaddr)
            , rx_pin_        (receivePin)
            , tx_pin_        (transmitPin)
            , uart1_         (uartnum1)
            , monitor_pin_   (monitorTxPin)
            , uart2_         (uartnum2)
        {
        }

        // ---------------------------------------------------------------------------
        // ESPHome component lifecycle
        // ---------------------------------------------------------------------------

        void VistaESPHome::setup()
        {
            ESP_LOGD(TAG, "Setup start — free heap: %lu", esp_get_free_heap_size());

            // ESPHome main-loop update interval (connection watchdog).
            set_update_interval(5000);

            // --- Register ESPHome API services ---
            register_service(&VistaESPHome::svc_set_panel_time,
                             "set_panel_time", {});
            register_service(&VistaESPHome::svc_alarm_keypress,
                             "alarm_keypress", {"keys"});
            register_service(&VistaESPHome::svc_alarm_keypress_partition,
                             "alarm_keypress_partition", {"keys", "partition"});
            register_service(&VistaESPHome::svc_alarm_disarm,
                             "alarm_disarm", {"code", "partition"});
            register_service(&VistaESPHome::svc_alarm_arm_home,
                             "alarm_arm_home", {"partition"});
            register_service(&VistaESPHome::svc_alarm_arm_night,
                             "alarm_arm_night", {"partition"});
            register_service(&VistaESPHome::svc_alarm_arm_away,
                             "alarm_arm_away", {"partition"});
            register_service(&VistaESPHome::svc_alarm_trigger_panic,
                             "alarm_trigger_panic", {"code", "partition"});
            register_service(&VistaESPHome::svc_alarm_trigger_fire,
                             "alarm_trigger_fire", {"code", "partition"});
            register_service(&VistaESPHome::svc_set_zone_fault,
                             "set_zone_fault", {"zone", "fault"});
            register_service(&VistaESPHome::svc_set_rf_zone_heartbeat,
                             "set_rf_zone_heartbeat", {"zone", "fault"});

            // --- Publish initial sensor states ---
            partitions_.publish_initial_states();
            zones_.publish_initial_states();

            if (lrr_sensor_ != nullptr)
                lrr_sensor_->process(" ");
            if (rf_sensor_ != nullptr)
                rf_sensor_->process(" ");

            // --- Configure VistaBus emulation modes ---
            vistabus_.emulateLRR(lrr_supervisor_);
            if (rfr_emulation_enabled_)
                vistabus_.emulateRFR(rfr_emulation_addr_);

#ifdef CC1101_RECEIVER
            // Start the CC1101 hardware receiver after emulateRFR().  If begin()
            // fails (radio or RMT init), the receiver is disabled but the rest of
            // the panel keeps running — so skip the watchdog and the now-idle
            // RF direct task too.
            if (cc1101_receiver_ != nullptr && cc1101_receiver_->begin())
            {
                // Enable tier-2 packet-absence watchdog only if at least one
                // physical RF sensor is configured.  Pure-emulated / no-RF
                // setups would otherwise trigger a full chip re-init every
                // 90 minutes for nothing.
                cc1101_receiver_->set_packet_watchdog_enabled(
                    zones_.has_physical_rf_zones());

                // Start the direct RF→HA task.  It blocks on rf_direct_queue and
                // calls ZoneManager::on_rf_direct() as soon as CC1101Receiver posts
                // a valid packet, bypassing the panel ECP round-trip.
                xTaskCreate(rf_direct_task_start,
                            "rf_direct",
                            kRfDirectTaskStackSize,
                            static_cast<void *>(this),
                            8,       // priority 8: above cc1101_rx (5), below processReceiveQueue (10)
                            &rf_direct_task_handle_);
            }
#endif

            // --- OTA flash-safety: suspend tasks during cache-off windows ---
            // During OTA the IDF disables the flash instruction cache while each
            // 4 KB page is written.  Any task executing flash-resident code at
            // that moment faults (corrupted backtrace / cache error).
            // ESPHome's OTAGlobalCallback fires on_ota_global_state() at key
            // lifecycle points.  We suspend all flash-touching tasks on
            // OTA_STARTED and resume on OTA_COMPLETED / OTA_ABORT / OTA_ERROR.
            ota_guard_ = new OTAGuard();
            ota_guard_->owner = this;
            ota::get_global_ota_callback()->add_global_state_listener(ota_guard_);

            // --- Build PacketDispatcher ---
            // Deferred until here so all collaborators are fully configured.
            PacketDispatcher::Config cfg;
            cfg.zones      = &zones_;
            cfg.partitions = &partitions_;
            cfg.aui        = &aui_;
            cfg.cmd        = &cmd_;
            cfg.lrr_sensor         = lrr_sensor_;
            cfg.rf_sensor          = rf_sensor_;
            cfg.chksum_fail_sensor = chksum_fail_sensor_;
            cfg.rtc        = this;           // VistaESPHome IS-A RealTimeClock
            cfg.ttl        = ttl_;

            dispatcher_ = new PacketDispatcher(vistabus_, cfg);

            // --- Start receive task ---
            xTaskCreate(processReceiveQueue_task_start,
                        "pRQtask",
                        kProcessRxTaskStackSize,
                        static_cast<void *>(this),
                        10,
                        &processReceiveQHandle);

            // --- Start VistaBus (must come after task creation) ---
            vistabus_.begin(uart1_, rx_pin_, tx_pin_, uart2_, monitor_pin_);

            last_connection_check = esp_timer_get_time();
            // Bus task is running and emulation modes are configured; service
            // calls are now safe to honour.
            ready_ = true;
            ESP_LOGD(TAG, "Setup complete — free heap: %lu", esp_get_free_heap_size());
        }

        void VistaESPHome::update()
        {
            // NOTE: PartitionManager::tick_battery_decay() is deliberately NOT
            // called here.  Wiring it up would clear the low-battery flag 'ttl'
            // after the last F7 that reported it (ttl defaults to 30 s), but the
            // panel surfaces low battery as a rotating system message rather than
            // a continuously-asserted flag, and F7 status frames arrive only
            // every ~10 s — so the sensor would flap on and off with the message
            // rotation.  Leaving it uncalled keeps the flag latched until reboot,
            // which is the pre-existing behaviour.  Needs a dedicated timeout
            // matched to the panel's message cadence, not the zone TTL.

            // Connection watchdog — log if no data has arrived in 30 seconds.
            if (!vistabus_.connected()
                    && (esp_timer_get_time() - last_connection_check) > 30LL * 1000 * 1000)
            {
                ESP_LOGE(TAG, "Data timeout — is the panel connected?");
                last_connection_check = esp_timer_get_time();
            }
        }

        void VistaESPHome::dump_config()
        {
            ESP_LOGCONFIG(TAG, "Vista Alarm Panel:");
            ESP_LOGCONFIG(TAG, "  UART: %d  RX pin: %d  TX pin: %d",
                          uart1_, rx_pin_, tx_pin_);
            if (monitor_pin_ != -1)
                ESP_LOGCONFIG(TAG, "  Monitor UART: %d  Monitor pin: %d",
                              uart2_, monitor_pin_);
            ESP_LOGCONFIG(TAG, "  Default partition: %d", default_partition_);
            ESP_LOGCONFIG(TAG, "  Connection TTL: %lld s",
                          ttl_ / (1000LL * 1000));
            ESP_LOGCONFIG(TAG, "  LRR supervisor: %s",
                          lrr_supervisor_ ? "enabled" : "disabled");
            if (rfr_emulation_enabled_) {
                ESP_LOGCONFIG(TAG, "  RF receiver emulation: enabled  address: %d",
                              rfr_emulation_addr_);
#ifdef CC1101_RECEIVER
                if (cc1101_receiver_ != nullptr) {
                    ESP_LOGCONFIG(TAG, "  CC1101 hardware receiver: enabled (~344.985 MHz, OOK, async serial)");
                    cc1101_receiver_->log_config();
                } else {
                    ESP_LOGCONFIG(TAG, "  CC1101 hardware receiver: disabled (software emulation only)");
                }
#endif
            } else {
                ESP_LOGCONFIG(TAG, "  RF receiver emulation: disabled");
            }
            if (aui_.device_address() != 0)
            {
                ESP_LOGCONFIG(TAG, "  AUI address: %d  Auto clock sync: %s",
                              aui_.device_address(),
                              aui_.auto_sync() ? "yes" : "no");
            }
            else
            {
                ESP_LOGCONFIG(TAG, "  AUI: disabled");
            }
            ESP_LOGCONFIG(TAG, "  Partitions:");
            for (const auto &p : partitions_.partitions())
                ESP_LOGCONFIG(TAG, "    Partition %d  keypad address: %d",
                              p.partition, p.assigned_keypad);
        }

        void VistaESPHome::stop()
        {
            // Publish caller_task_ BEFORE stop_requested_.  vistabus_.stop() below
            // blocks for several hundred ms, during which the receive task can
            // observe stop_requested_, read a still-null caller_task_, and delete
            // itself — leaving the handshake below to burn its full 2 s timeout.
            if (processReceiveQHandle != nullptr)
                caller_task_ = xTaskGetCurrentTaskHandle();

            stop_requested_ = true;

#ifdef CC1101_RECEIVER
            // Unblock rf_direct_task if it is waiting on an empty rf_direct_queue.
            if (rf_direct_task_handle_ != nullptr)
            {
                RfDirectMsg sentinel{};
                xQueueSend(vistabus_.rf_direct_queue, &sentinel, 0);
            }
#endif

            vistabus_.stop();

            if (processReceiveQHandle != nullptr)
            {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
                processReceiveQHandle = nullptr;
            }

            delete dispatcher_;
            dispatcher_ = nullptr;
        }

        // ---------------------------------------------------------------------------
        // Receive task
        //
        // Runs in a dedicated FreeRTOS task.  Initialises RF heartbeat timers once,
        // then loops calling dispatcher_->dispatch_one() which blocks internally on
        // VistaBus::read_packet().  The AUI tick (clock sync + request timeout) and
        // RF heartbeat checks are driven each iteration around the receive loop.
        // ---------------------------------------------------------------------------

        void VistaESPHome::processReceiveQueue_task_start(void *args)
        {
            VistaESPHome *self = static_cast<VistaESPHome *>(args);
            self->processReceiveQueue(args);
        }

        void VistaESPHome::processReceiveQueue(void * /*args*/)
        {
            // Stagger initial RF heartbeat timers now that all zones are registered.
            if (rfr_emulation_enabled_)
                zones_.init_rf_heartbeat_timers();

            // Small delay to let VistaBus tasks settle before we start reading.
            vTaskDelay(pdMS_TO_TICKS(250));

            while (!stop_requested_)
            {
                // Per-iteration housekeeping.
                if (rfr_emulation_enabled_)
                    zones_.handle_rf_heartbeats(vistabus_);

                // AUI tick: expire timed-out zone-fault requests and trigger
                // periodic clock sync.
                aui_.tick(vistabus_, partitions_.in_program_mode(), this);

                // Block on the receive queue and dispatch one packet.
                if (dispatcher_ != nullptr)
                    dispatcher_->dispatch_one();
            }

            if (caller_task_ != nullptr)
                xTaskNotifyGive(caller_task_);

            vTaskDelete(nullptr);
        }

#ifdef CC1101_RECEIVER
        // ---------------------------------------------------------------------------
        // RF direct-to-HA task
        //
        // Dedicated FreeRTOS task (priority 8) that drains rf_direct_queue and
        // calls ZoneManager::on_rf_direct() immediately when CC1101Receiver posts
        // a valid, non-heartbeat RF packet.  This publishes zone state to Home
        // Assistant without waiting for the panel's F1 poll → FA/FB round-trip,
        // reducing sensor event latency from 50–500 ms to < 5 ms.
        //
        // Zone state is still updated via the ECP path (on_rf_zone_packet) for
        // panel consistency; the timestamp guard inside ZoneManager suppresses the
        // redundant publish on the ECP path to prevent HA state flapping.
        // ---------------------------------------------------------------------------

        void VistaESPHome::rf_direct_task_start(void *args)
        {
            static_cast<VistaESPHome *>(args)->rf_direct_task(args);
        }

        void VistaESPHome::rf_direct_task(void * /*args*/)
        {
            while (!stop_requested_)
            {
                RfDirectMsg msg;
                if (xQueueReceive(vistabus_.rf_direct_queue, &msg,
                                  pdMS_TO_TICKS(500)) == pdPASS)
                {
                    if (!stop_requested_)
                        zones_.on_rf_direct(msg.serial, msg.ecp_status, msg.rssi);
                }
            }
            rf_direct_task_handle_ = nullptr;
            vTaskDelete(nullptr);
        }
#endif // CC1101_RECEIVER

    } // namespace alarm_panel
} // namespace esphome
