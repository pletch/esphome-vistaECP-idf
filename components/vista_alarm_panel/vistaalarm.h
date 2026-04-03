#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/api/custom_api_device.h"

#include "vistabus.h"
#include "LRR_strings.h"
#include "translation.h"
#include <string>
#include "esp_random.h"
#include "panel_text.h"
#include "helper_enums.h"
#include "constants.h"

namespace esphome
{
    namespace alarm_panel
    {
        class vistaECPBinarySensor
        {
            public:
                virtual void process(bool triggered) = 0;
        };

        class vistaECPTextSensor
        {
            public:
                virtual void process(std::string text) = 0;
        };


        class vistaECPHome : public api::CustomAPIDevice, public time::RealTimeClock
        {
            public:
                vistaECPHome(char kpaddr, int receivePin, int transmitPin, int uartnum1, 
                            int monitorTxPin, int uartnum2);

                void set_accessCode(const char *ac) { accessCode = ac; }
                void set_rfSerialLookup(const char *rf) { rfSerialLookup = rf; }
                void set_quickArm(bool qa) { quickArm = qa; }
                void set_lrrSupervisor(bool ls) { lrrSupervisor = ls; }
                void set_rfrEmulation(bool rfr_emul, uint8_t rfr_addr) { rfrEmulation[0] = rfr_emul; rfrEmulation[1] = rfr_addr; }
                void set_auiaddr(uint8_t addr) { aui_device.address = addr; aui_device.sequence1 = 0x20 | addr;};
                void set_clocksync(bool cs) { panel_clock.auto_sync = cs;};

                void initialize_partition_sensors();
                void set_partitionKeypad(uint8_t idx, uint8_t addr)
                {
                    Partition new_partition;
                    new_partition.assigned_keypad = addr;
                    new_partition.partition = idx;
                    new_partition.keypad_sequence = (addr & 0x0F) | 0x10;
                    known_partitions.push_back(new_partition);
                }
                void set_alarm_state(std::string const &state, std::string code, int partition);

                void set_defaultPartition(uint8_t dp) { defaultPartition = dp; }
                void set_debug(uint8_t db) { debug = db; }
                void set_ttl(uint32_t t) { TTL = t * 1000 * 1000; };
                float get_setup_priority() const override { return setup_priority::LATE; }

                bool connected()
                {
                    return vistabus.connected();
                }

                void set_zone_fault(int32_t zone, bool fault);
                void set_emulated_zone_tamper(int32_t zone, bool tamper_active);
                void register_zone(vistaECPBinarySensor *binary_sensor, uint8_t partition_number, uint8_t zone_number, uint32_t rf_serial, uint8_t rf_loop, bool emulated);
                void register_status_sensor(vistaECPBinarySensor *binary_sensor, uint8_t partition_number, const char * type);
                void register_zone_text(vistaECPTextSensor *text_sensor, uint8_t partition_number, uint8_t zone_number);
                void register_text_sensor(vistaECPTextSensor *text_sensor, uint8_t partition, const char * type);
                void register_ac(vistaECPBinarySensor *binary_sensor) {ac_bin_sensor = binary_sensor;}
                void register_bat(vistaECPBinarySensor *binary_sensor) {bat_bin_sensor = binary_sensor;}
                void register_expander(uint8_t zone) {vistabus.add_emulated_expander(zone);}

                void setup() override;
                void update() override;
                void stop();

            private:
                struct TextSensorPartition
                {
                    vistaECPTextSensor *system_status {nullptr};
                    vistaECPTextSensor *line1 {nullptr};
                    vistaECPTextSensor *line2 {nullptr};
                    vistaECPTextSensor *beeps {nullptr};
                };

                struct TextSensorCommon
                {
                    vistaECPTextSensor *zone_status {nullptr};
                    vistaECPTextSensor *rf_messages {nullptr};
                    vistaECPTextSensor *lrr_messages {nullptr};
                };

                struct StatusSensorPartition
                {
                    vistaECPBinarySensor *rdy {nullptr};
                    vistaECPBinarySensor *trbl {nullptr};
                    vistaECPBinarySensor *byp {nullptr};
                    vistaECPBinarySensor *arm {nullptr};
                    vistaECPBinarySensor *arma {nullptr};
                    vistaECPBinarySensor *arms {nullptr};
                    vistaECPBinarySensor *armi {nullptr};
                    vistaECPBinarySensor *armn {nullptr};
                    vistaECPBinarySensor *chm {nullptr};
                    vistaECPBinarySensor *alm {nullptr};
                    vistaECPBinarySensor *fire {nullptr};
                };

                struct Zone
                {
                    vistaECPBinarySensor *binary_sensor {nullptr};
                    vistaECPTextSensor *text_sensor {nullptr};
                    uint8_t zone {0};
                    int64_t time {0};
                    uint32_t rfserial {0};
                    uint8_t rfloop {0};
                    int64_t rfnext_hb {0};
                    uint8_t partition {0};
                    bool open {false};
                    bool bypass {false};
                    bool alarm {false};
                    bool check {false};
                    bool active {false};
                    bool rflowbat {false};
                };

                struct TextSensor
                {
                    vistaECPTextSensor *text_sensor {nullptr};
                    uint8_t partition {0};
                    const char * type {nullptr};
                };
                VistaBus vistabus;
                const char *const TAG = "vista-alarm";
                esp_log_level_t log_level = ESP_LOG_DEBUG;
                int64_t TTL = 30000000;
                int64_t last_connection_check = 0;
                uint8_t debug = 0;
                char last_F7[48];
                char keypadAddr1 = 0;
                int rxPin = 0;
                int txPin = 0;
                int uart1 = -1;
                int monitorPin = 0;
                int uart2 = -1;
                void processReceiveQueue(void *args);
                static void processReceiveQueue_task_start(void *args);
                TaskHandle_t processReceiveQHandle;
                bool api_connection_state;

                LightStates currentLightState,previousLightState;

                std::vector<Partition> known_partitions{};

                PanelClock panel_clock;

                AUIDevice aui_device;

                AUIRequest aui_request;
                
                std::vector<binary_sensor::BinarySensor *> bMap;
                std::vector<text_sensor::TextSensor *> tMap;

                std::vector<TextSensorPartition> text_sensors_partition;

                std::vector<StatusSensorPartition> status_sensors_partition;

                TextSensorCommon text_sensors_common;

                const char *accessCode;
                const char *rfSerialLookup;
                bool quickArm;

                bool lrrSupervisor;
                uint8_t rfrEmulation[2];
                int defaultPartition;

                int64_t lowBatteryTime;

                SysState currentSystemState,previousSystemState;

                std::string previousZoneStatusMsg = " ";

                uint8_t partitionTargets;

                std::vector<Zone> alarmZones{};
                vistaECPBinarySensor *ac_bin_sensor = NULL;
                vistaECPBinarySensor *bat_bin_sensor = NULL;

                Zone *getZone(uint16_t z);
                Zone *getRfSerialLookup(uint32_t serialCode);

                void zoneStatusUpdate(Zone *zt);

                StatusFlags statusFlags;
                LrrStatusFlags lrrstatusFlags;
                void refreshStatusFlags(char * cbuf);
                void processStatus();
                int64_t last_refresh = 0;

                void refreshSensors();
                void refreshLRRStatusFlags(char * cbuf); 

                void RF_handle_heartbeats();

                void alarm_disarm(std::string code, int32_t partition);
                void alarm_arm_home(int32_t partition);
                void alarm_arm_night(int32_t partition);
                void alarm_arm_away(int32_t partition);
                void alarm_trigger_fire(std::string code, int32_t partition);
                void alarm_trigger_panic(std::string code, int32_t partition);
                void alarm_keypress(std::string keystring);
                void alarm_keypress_partition(std::string keystring, int32_t partition);

                void AUIrequest_panel_time();
                void AUIset_panel_time();
                void AUIget_zone_faults();
                void AUIprocess_zone_faults(char *list);

                void print_packet(char cbuf[], int type, int src, int len);
                int getZoneFromPrompt(char *p1);
                void updateDisplayLines(uint8_t partition);
        };
    } // namespace
} // namespace
