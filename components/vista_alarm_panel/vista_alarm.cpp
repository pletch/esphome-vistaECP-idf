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
#include "panel_text.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

namespace esphome
{
    namespace alarm_panel
    {
        const char * const VistaESPHome::TAG = "vista-alarm";

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
            : keypad_addr1_  (kpaddr)
            , rx_pin_        (receivePin)
            , tx_pin_        (transmitPin)
            , uart1_         (uartnum1)
            , monitor_pin_   (monitorTxPin)
            , uart2_         (uartnum2)
            , cmd_           (vistabus_, partitions_)   // holds refs — must come after both
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
            // Service names and parameter lists are identical to the originals so
            // that existing Home Assistant automations require no changes.
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
            register_service(&VistaESPHome::svc_set_emulated_zone_tamper,
                             "set_emulated_zone_tamper_state", {"zone", "tamper active"});

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

            // --- Build PacketDispatcher ---
            // Deferred until here so all collaborators are fully configured.
            PacketDispatcher::Config cfg;
            cfg.zones      = &zones_;
            cfg.partitions = &partitions_;
            cfg.aui        = &aui_;
            cfg.cmd        = &cmd_;
            cfg.lrr_sensor = lrr_sensor_;
            cfg.rf_sensor  = rf_sensor_;
            cfg.rtc        = this;           // VistaESPHome IS-A RealTimeClock
            cfg.ttl        = ttl_;

            dispatcher_ = new PacketDispatcher(vistabus_, cfg);

            // --- Start receive task ---
            xTaskCreate(processReceiveQueue_task_start,
                        "pRQtask",
                        4096,
                        static_cast<void *>(this),
                        10,
                        &processReceiveQHandle);

            // --- Start VistaBus (must come after task creation) ---
            vistabus_.begin(uart1_, rx_pin_, tx_pin_, uart2_, monitor_pin_);

            last_connection_check = esp_timer_get_time();
            ESP_LOGD(TAG, "Setup complete — free heap: %lu", esp_get_free_heap_size());
        }

        void VistaESPHome::update()
        {
            // Connection watchdog — log if no data has arrived in 30 seconds.
            if (!vistabus_.connected()
                    && (esp_timer_get_time() - last_connection_check) > 30LL * 1000 * 1000)
            {
                ESP_LOGE(TAG, "Data timeout — is the panel connected?");
                last_connection_check = esp_timer_get_time();
            }
        }

        void VistaESPHome::stop()
        {
            stop_requested_ = true;
            vistabus_.stop();

            if (processReceiveQHandle != nullptr)
            {
                caller_task_ = xTaskGetCurrentTaskHandle();
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

    } // namespace alarm_panel
} // namespace esphome
