// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/api/custom_api_device.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "vista_bus.h"
#include "zone_manager.h"
#include "partition_manager.h"
#include "command_writer.h"
#include "aui_manager.h"
#include "packet_dispatcher.h"
#include "panel_text.h"
#include "helper_enums.h"
#include "constants.h"
#include "sensor_interfaces.h"

#ifdef CC1101_RECEIVER
#include "cc1101_receiver.h"
#include "driver/spi_master.h"
#endif

#include <string>
#include <cstdint>

namespace esphome
{
    namespace alarm_panel
    {
        // Sensor interface base classes are defined in sensor_interfaces.h so
        // that translation units which call ->process() can include that header
        // without pulling in the full ESPHome component tree.

        // -----------------------------------------------------------------------
        // VistaESPHome
        //
        // ESPHome component shell.  Owns the five collaborator objects and wires
        // them together during setup().  All substantive logic has moved into the
        // collaborators; this class is now a thin coordinator responsible only for:
        //
        //   - Holding configuration supplied by the YAML / generated code before
        //     setup() is called (access code, TTL, partition/keypad assignments,
        //     RFR emulation, AUI address, etc.).
        //   - Registering ESPHome services and forwarding calls to collaborators.
        //   - Publishing initial sensor states at startup.
        //   - Running the receive-queue FreeRTOS task.
        //   - Checking the connection watchdog in update().
        //
        // Collaborator ownership:
        //   vistabus_     — VistaBus      (hardware UART abstraction)
        //   zones_        — ZoneManager   (zone state + RF heartbeats)
        //   partitions_   — PartitionManager (partition state machine + sensors)
        //   cmd_          — CommandWriter  (all outbound panel commands)
        //   aui_          — AUIManager     (AUI clock sync + zone-fault queries)
        //   dispatcher_   — PacketDispatcher* (created in setup(); owned via ptr
        //                   so construction can be deferred until all collaborators
        //                   are fully configured)
        // -----------------------------------------------------------------------
        class VistaESPHome : public api::CustomAPIDevice,
                             public time::RealTimeClock
        {
        public:

            // -------------------------------------------------------------------
            // Construction
            // -------------------------------------------------------------------
            VistaESPHome(char kpaddr,
                         int  receivePin,
                         int  transmitPin,
                         int  uartnum1,
                         int  monitorTxPin,
                         int  uartnum2);

            // -------------------------------------------------------------------
            // Pre-setup configuration setters
            //
            // All called by generated ESPHome code before setup().
            // -------------------------------------------------------------------

            void set_accessCode(const char *ac)  { cmd_.set_access_code(ac); }
            void set_quickArm(bool qa)            { cmd_.set_quick_arm(qa);   }
            void set_auiaddr(uint8_t addr)        { aui_.set_device_address(addr); }
            void set_clocksync(bool cs)           { aui_.set_auto_sync(cs);   }
            void set_lrrSupervisor(bool ls)       { lrr_supervisor_ = ls;     }
            void set_defaultPartition(uint8_t dp) { default_partition_ = dp;  }
            void set_ttl(uint32_t t)              { ttl_ = static_cast<int64_t>(t) * 1000 * 1000; }
            void set_debug(uint8_t db)            { debug_ = db;              }

            // rfSerialLookup is kept for API compatibility; the original used it
            // as a lookup table for RF serial→zone mapping passed as a raw string.
            // In the refactored design ZoneManager holds the mapping implicitly
            // through register_zone(); this setter is a no-op retained so that
            // generated code that calls it continues to compile.
            void set_rfSerialLookup(const char * /*rf*/) {}

            void set_rfrEmulation(bool rfr_emul, uint8_t rfr_addr)
            {
                rfr_emulation_enabled_ = rfr_emul;
                rfr_emulation_addr_    = rfr_addr;
            }

            void set_rf_heartbeat_external(bool external)
            {
                zones_.set_external_heartbeat_mode(external);
            }

#ifdef CC1101_RECEIVER
            // Called from generated YAML code when cc1101_* pins are configured.
            // Must be called before setup() — stores pin numbers for begin() call.
            // rf_receiver_emulation must also be true in YAML; the CC1101 acts as
            // the hardware source for the emulated RF receiver device.
            void init_cc1101(int mosi, int miso, int sck, int csn, int gdo0, int spi_bus)
            {
                const spi_host_device_t host = (spi_bus == 3) ? SPI3_HOST : SPI2_HOST;
                cc1101_receiver_ = new CC1101Receiver(
                    vistabus_, mosi, miso, sck, csn, gdo0, host);
            }

            void set_cc1101_rssi_threshold(int8_t dbm)
            {
                if (cc1101_receiver_) cc1101_receiver_->set_rssi_threshold(dbm);
            }
#endif

            // -------------------------------------------------------------------
            // Partition / keypad configuration
            //
            // Must be called before initialize_partition_sensors().
            // -------------------------------------------------------------------

            void set_partitionKeypad(uint8_t partition_id, uint8_t keypad_addr)
            {
                partitions_.add_partition(partition_id, keypad_addr);
            }

            // Must be called after all set_partitionKeypad() calls and before any
            // register_*() sensor call.  Allocates the per-partition sensor vectors.
            void initialize_partition_sensors()
            {
                partitions_.initialize_sensor_vectors();
            }

            // -------------------------------------------------------------------
            // Sensor registration
            //
            // Forwarded to the appropriate collaborator. 
            // -------------------------------------------------------------------

            void register_zone(vistaECPBinarySensor *sensor,
                               uint8_t partition_number,
                               uint8_t zone_number,
                               uint32_t rf_serial,
                               uint8_t rf_loop,
                               bool emulated)
            {
                zones_.register_zone(sensor, partition_number, zone_number,
                                     rf_serial, rf_loop, emulated);
            }

            void register_zone_text(vistaECPTextSensor *sensor,
                                    uint8_t partition_number,
                                    uint8_t zone_number)
            {
                zones_.register_zone_text(sensor, partition_number, zone_number);
            }

            void register_status_sensor(vistaECPBinarySensor *sensor,
                                        uint8_t partition_number,
                                        const char *type)
            {
                partitions_.register_status_sensor(sensor, partition_number, type);
            }

            void register_text_sensor(vistaECPTextSensor *sensor,
                                      uint8_t partition_number,
                                      const char *type)
            {
                // LRR and RF message sensors are not partition-specific; intercept
                // them here and store locally for PacketDispatcher::Config.
                if (strcmp(type, "LRR_MESSAGES") == 0)
                    lrr_sensor_ = sensor;
                else if (strcmp(type, "RF_MESSAGES") == 0)
                    rf_sensor_ = sensor;
                else if (strcmp(type, "ZONE_STATUS") == 0)
                    zones_.register_zone_status_sensor(sensor);
                else
                    partitions_.register_text_sensor(sensor, partition_number, type);
            }

            void set_chksum_fail_sensor(text_sensor::TextSensor *s) { chksum_fail_sensor_ = s; }

            void register_ac(vistaECPBinarySensor *sensor)
            {
                partitions_.register_ac(sensor);
            }

            void register_bat(vistaECPBinarySensor *sensor)
            {
                partitions_.register_bat(sensor);
            }

            void register_expander(uint8_t zone)
            {
                vistabus_.add_emulated_expander(zone);
            }

            // -------------------------------------------------------------------
            // ESPHome component lifecycle
            // -------------------------------------------------------------------

            float get_setup_priority() const override { return setup_priority::LATE; }
            void  setup()       override;
            void  update()      override;
            void  dump_config() override;
            void  stop();

            // -------------------------------------------------------------------
            // Status accessor
            // -------------------------------------------------------------------

            bool connected() const { return vistabus_.connected(); }

            // -------------------------------------------------------------------
            // Direct zone control — callable from ESPHome YAML lambdas
            //
            // These mirror the corresponding HA service callbacks so that imported
            // homeassistant binary_sensor on_press/on_release handlers can drive
            // emulated zones without going through the HA service round-trip.
            // -------------------------------------------------------------------

            void set_zone_fault(int32_t zone, bool fault)
            {
                svc_set_zone_fault(zone, fault);
            }

            void set_rf_zone_heartbeat(int32_t zone, bool fault)
            {
                svc_set_rf_zone_heartbeat(zone, fault);
            }

        private:

            // -------------------------------------------------------------------
            // ESPHome service callbacks
            //
            // Registered in setup() via register_service().  All delegate to cmd_
            // or aui_ immediately.
            // -------------------------------------------------------------------

            // Reject service calls made before setup() has completed (e.g. from an
            // on_boot lambda or an imported-sensor on_press handler that fires
            // during boot).  Until VistaBus::begin() runs the bus task is not
            // draining sendQueue and the emulation modes are not configured, so
            // such early calls would be silently deferred or dropped.  Logging a
            // warning and returning makes the misuse visible instead of surprising.
            bool not_ready(const char *what) const
            {
                if (!ready_)
                {
                    ESP_LOGW(TAG, "%s called before setup complete — ignored", what);
                    return true;
                }
                return false;
            }

            void svc_set_panel_time()
            {
                if (not_ready("set_panel_time")) return;
                aui_.request_time_sync(vistabus_,
                                       /*in_program_mode=*/false,
                                       this);
            }

            void svc_alarm_keypress(std::string keys)
            {
                if (not_ready("alarm_keypress")) return;
                ESP_LOGI(TAG, "svc_alarm_keypress: keys='%s'", keys.c_str());
                cmd_.keypress(keys, default_partition_);
            }

            void svc_alarm_keypress_partition(std::string keys, int32_t partition)
            {
                if (not_ready("alarm_keypress_partition")) return;
                ESP_LOGI(TAG, "svc_alarm_keypress_partition: keys='%s' partition=%d",
                         keys.c_str(), static_cast<int>(partition));
                cmd_.keypress(keys, static_cast<int>(partition));
            }

            void svc_alarm_disarm(std::string code, int32_t partition)
            {
                if (not_ready("alarm_disarm")) return;
                ESP_LOGI(TAG, "svc_alarm_disarm: partition=%d", static_cast<int>(partition));
                cmd_.disarm(static_cast<int>(partition), code);
            }

            void svc_alarm_arm_home(int32_t partition)
            {
                if (not_ready("alarm_arm_home")) return;
                cmd_.arm_stay(static_cast<int>(partition));
            }

            void svc_alarm_arm_night(int32_t partition)
            {
                if (not_ready("alarm_arm_night")) return;
                cmd_.arm_night(static_cast<int>(partition));
            }

            void svc_alarm_arm_away(int32_t partition)
            {
                if (not_ready("alarm_arm_away")) return;
                cmd_.arm_away(static_cast<int>(partition));
            }

            void svc_alarm_trigger_panic(std::string code, int32_t partition)
            {
                if (not_ready("alarm_trigger_panic")) return;
                cmd_.trigger_panic(static_cast<int>(partition), code);
            }

            void svc_alarm_trigger_fire(std::string code, int32_t partition)
            {
                if (not_ready("alarm_trigger_fire")) return;
                cmd_.trigger_fire(static_cast<int>(partition), code);
            }

            void svc_set_zone_fault(int32_t zone, bool fault)
            {
                if (not_ready("set_zone_fault")) return;
                // Direct fast-path: publish to HA immediately without waiting for
                // the panel's ECP echo (FA expander or FB RF receiver packet).
                zones_.on_zone_direct(static_cast<uint8_t>(zone), fault);
                // ECP path: forward the fault to the panel so its alarm logic fires.
                zones_.send_emulated_fault(static_cast<uint8_t>(zone), fault, vistabus_);
            }

            // Sends an RF supervision heartbeat for the given virtual RF zone,
            // encoding the current fault state in the status byte alongside the
            // supervision bit.  Resets the internal heartbeat timer regardless of
            // whether external_heartbeat_mode is active.
            void svc_set_rf_zone_heartbeat(int32_t zone, bool fault)
            {
                if (not_ready("set_rf_zone_heartbeat")) return;
                ESP_LOGI(TAG, "svc_set_rf_zone_heartbeat: zone=%d fault=%d",
                         static_cast<int>(zone), fault);
                zones_.send_rf_heartbeat(static_cast<uint8_t>(zone), fault, vistabus_);
            }

            // -------------------------------------------------------------------
            // Receive task
            // -------------------------------------------------------------------

            void processReceiveQueue(void *args);
            static void processReceiveQueue_task_start(void *args);

#ifdef CC1101_RECEIVER
            // Dedicated task that drains rf_direct_queue and calls
            // ZoneManager::on_rf_direct() for an immediate HA publish that
            // bypasses the panel ECP round-trip.
            void rf_direct_task(void *args);
            static void rf_direct_task_start(void *args);
#endif

            // -------------------------------------------------------------------
            // Collaborators (owned by value except dispatcher_)
            // -------------------------------------------------------------------

            VistaBus         vistabus_;
            ZoneManager      zones_;
            PartitionManager partitions_;
            CommandWriter    cmd_;          // holds refs to vistabus_ and partitions_
            AUIManager       aui_;
            PacketDispatcher *dispatcher_ {nullptr}; // created in setup()

#ifdef CC1101_RECEIVER
            CC1101Receiver   *cc1101_receiver_ {nullptr};
#endif

            // OTA flash-safety guard — defined in vista_alarm.cpp to avoid
            // pulling ota_backend.h into headers compiled from subdirectories.
            struct OTAGuard;
            OTAGuard *ota_guard_ {nullptr};

            // -------------------------------------------------------------------
            // Common (non-partition) sensor pointers
            // Stored here because they are needed to build PacketDispatcher::Config
            // in setup() and may be set before setup() is called.
            // -------------------------------------------------------------------

            vistaECPTextSensor *lrr_sensor_        {nullptr};
            vistaECPTextSensor *rf_sensor_         {nullptr};
            text_sensor::TextSensor *chksum_fail_sensor_{nullptr};

            // -------------------------------------------------------------------
            // Configuration state set before setup()
            // -------------------------------------------------------------------

            char    keypad_addr1_          {0};
            int     rx_pin_                {0};
            int     tx_pin_                {0};
            int     uart1_                 {-1};
            int     monitor_pin_           {0};
            int     uart2_                 {-1};
            bool    lrr_supervisor_        {false};
            bool    rfr_emulation_enabled_ {false};
            uint8_t rfr_emulation_addr_    {0};
            int     default_partition_     {1};
            uint8_t debug_                 {0};
            int64_t ttl_                   {30LL * 1000 * 1000};  // 30 s in µs

            // -------------------------------------------------------------------
            // Runtime state
            // -------------------------------------------------------------------

            TaskHandle_t     processReceiveQHandle  {nullptr};
            int64_t          last_connection_check  {0};
            volatile bool    stop_requested_        {false};
            TaskHandle_t     caller_task_           {nullptr};
            // Set true at the end of setup(); gates service calls (see not_ready()).
            bool             ready_                 {false};
#ifdef CC1101_RECEIVER
            TaskHandle_t     rf_direct_task_handle_ {nullptr};
#endif

            static constexpr const char *TAG = "vista-alarm";
        };

    } // namespace alarm_panel
} // namespace esphome
