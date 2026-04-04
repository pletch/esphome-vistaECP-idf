#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/api/custom_api_device.h"

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
            void  setup()  override;
            void  update() override;
            void  stop();

            // -------------------------------------------------------------------
            // Status accessor
            // -------------------------------------------------------------------

            bool connected() const { return vistabus_.connected(); }

        private:

            // -------------------------------------------------------------------
            // ESPHome service callbacks
            //
            // Registered in setup() via register_service().  All delegate to cmd_
            // or aui_ immediately.
            // -------------------------------------------------------------------

            void svc_set_panel_time()
            {
                aui_.request_time_sync(vistabus_,
                                       /*in_program_mode=*/false,
                                       this);
            }

            void svc_alarm_keypress(std::string keys)
            {
                cmd_.keypress(keys, default_partition_);
            }

            void svc_alarm_keypress_partition(std::string keys, int32_t partition)
            {
                cmd_.keypress(keys, static_cast<int>(partition));
            }

            void svc_alarm_disarm(std::string code, int32_t partition)
            {
                cmd_.disarm(static_cast<int>(partition), code);
            }

            void svc_alarm_arm_home(int32_t partition)
            {
                cmd_.arm_stay(static_cast<int>(partition));
            }

            void svc_alarm_arm_night(int32_t partition)
            {
                cmd_.arm_night(static_cast<int>(partition));
            }

            void svc_alarm_arm_away(int32_t partition)
            {
                cmd_.arm_away(static_cast<int>(partition));
            }

            void svc_alarm_trigger_panic(std::string code, int32_t partition)
            {
                cmd_.trigger_panic(static_cast<int>(partition), code);
            }

            void svc_alarm_trigger_fire(std::string code, int32_t partition)
            {
                cmd_.trigger_fire(static_cast<int>(partition), code);
            }

            void svc_set_zone_fault(int32_t zone, bool fault)
            {
                zones_.send_emulated_fault(static_cast<uint8_t>(zone), fault, vistabus_);
            }

            void svc_set_emulated_zone_tamper(int32_t zone, bool tamper_active)
            {
                zones_.send_emulated_tamper(static_cast<uint8_t>(zone),
                                            tamper_active, vistabus_);
            }

            // -------------------------------------------------------------------
            // Receive task
            // -------------------------------------------------------------------

            void processReceiveQueue(void *args);
            static void processReceiveQueue_task_start(void *args);

            // -------------------------------------------------------------------
            // Collaborators (owned by value except dispatcher_)
            // -------------------------------------------------------------------

            VistaBus         vistabus_;
            ZoneManager      zones_;
            PartitionManager partitions_;
            CommandWriter    cmd_;          // holds refs to vistabus_ and partitions_
            AUIManager       aui_;
            PacketDispatcher *dispatcher_ {nullptr}; // created in setup()

            // -------------------------------------------------------------------
            // Common (non-partition) sensor pointers
            // Stored here because they are needed to build PacketDispatcher::Config
            // in setup() and may be set before setup() is called.
            // -------------------------------------------------------------------

            vistaECPTextSensor *lrr_sensor_ {nullptr};
            vistaECPTextSensor *rf_sensor_  {nullptr};

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

            TaskHandle_t     processReceiveQHandle {nullptr};
            int64_t          last_connection_check {0};
            volatile bool    stop_requested_       {false};
            TaskHandle_t     caller_task_          {nullptr};

            static const char * const TAG;
        };

    } // namespace alarm_panel
} // namespace esphome
