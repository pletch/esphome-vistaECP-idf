#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/api/custom_api_device.h"

#include "vistabus.h"
#include "LRRstrings.h"
#include "translation.h"
#include <string>
#include "esp_random.h"
#include "paneltext.h"

// for documentation see project at https://github.com/pletch/esphome-vistaECP-idf

#define BIT_MASK_BYTE1_BEEP 0x07
#define BIT_MASK_BYTE1_NIGHT 0x10

#define BIT_MASK_BYTE2_ARMED_HOME 0x80
#define BIT_MASK_BYTE2_LOW_BAT 0x40
#define BIT_MASK_BYTE2_ALARM_ZONE 0x20
#define BIT_MASK_BYTE2_READY 0x10
#define BIT_MASK_BYTE2_UNKNOWN 0x08
#define BIT_MASK_BYTE2_SYSTEM_FLAG 0x04
#define BIT_MASK_BYTE2_CHECK_FLAG 0x02
#define BIT_MASK_BYTE2_FIRE 0x01

#define BIT_MASK_BYTE3_INSTANT 0x80
#define BIT_MASK_BYTE3_PROGRAM 0x40
#define BIT_MASK_BYTE3_CHIME_MODE 0x20
#define BIT_MASK_BYTE3_BYPASS 0x10
#define BIT_MASK_BYTE3_AC_POWER 0x08
#define BIT_MASK_BYTE3_ARMED_AWAY 0x04
#define BIT_MASK_BYTE3_ZONE_ALARM 0x02
#define BIT_MASK_BYTE3_IN_ALARM 0x01

namespace esphome
{
    namespace alarm_panel
    {
        extern VistaBus vistabus;
        enum sysState
        {
            soffline,
            sarmedaway,
            sarmedstay,
            sbypass,
            sac,
            schime,
            sbat,
            scheck,
            sarmednight,
            sdisarmed,
            striggered,
            sunavailable,
            strouble,
            salarm,
            sfire,
            sinstant,
            sready,
            sarmed,
            sarming
        };

        enum reqStates
        {
            rsidle,
            rsopenzones,
            rsbypasszones,
            rszonecount,
            rspartitionlist,
            rspartitionid,
            rszoneinfo,
            rsicode,
            rsdate
        };

        enum sourceDevice
        {
            unspecified = 0,
            aui = 0xF2,
            keypad_ack = 0xF6,
            keypad = 0xF7,
            long_range_radio = 0xF9,
            expander = 0xFA,
            rf_receiver = 0xFB
        };

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
                void set_displaySystemMsg(bool dsm) { displaySystemMsg = dsm; }
                void set_lrrSupervisor(bool ls) { lrrSupervisor = ls; }
                void set_rfrEmulation(bool rfr_emul, uint8_t rfr_addr) { rfrEmulation[0] = rfr_emul; rfrEmulation[1] = rfr_addr; }

                void initialize_partition_sensors();
                void set_partitionKeypad(uint8_t idx, uint8_t addr)
                {
                    partitionType new_partition;
                    new_partition.assigned_keypad = addr;
                    new_partition.partition = idx;
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

            protected:
                const char *const TAG = "vista-alarm";
                esp_log_level_t log_level = ESP_LOG_DEBUG;
                uint64_t TTL = 30000000;
                uint64_t last_refresh = 0;
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

                struct lightStates
                {
                    bool away;
                    bool stay;
                    bool night;
                    bool instant;
                    bool bypass;
                    bool ready;
                    bool ac;
                    bool chime;
                    bool bat;
                    bool alarm;
                    bool check;
                    bool fire;
                    bool trouble;
                    bool armed;
                };

                lightStates currentLightState,previousLightState;

                struct partitionStateType
                {
                    sysState previousSystemState;
                    lightStates previousLightState;
                    int lastbeeps;
                    bool refreshStatus;
                    bool refreshLights;
                };

                struct partitionType
                {
                    uint8_t partition = 0;
                    uint8_t assigned_keypad = 0;
                    partitionStateType partition_state;
                };

                std::vector<partitionType> known_partitions{};

                struct statusFlagType
                {
                    uint8_t beeps = 0;
                    bool armedStay = false;
                    bool armedAway = false;
                    bool night = false;
                    bool instant = false;
                    bool chime = false;
                    bool acPower = false;
                    bool acLoss = false;
                    bool ready = false;
                    bool entryDelay = false;
                    bool programMode = false;
                    bool zoneBypass = false;
                    bool zoneAlarm = false;
                    bool alarm = false;
                    bool check = false;
                    bool systemFlag = false;
                    bool lowBattery = false;
                    bool systemTrouble = false;
                    bool fire = false;
                    bool fireZone = false;
                    bool backlight = false;
                    bool armed = false;
                    bool away = false;
                    bool bypass = false;
                    bool inAlarm = false;
                    bool fault = false;
                    bool panicAlarm = false;
                    char keypad[4];
                    uint8_t partition = 0;
                    int zone;
                    char prompt1[32] = {0};
                    char prompt2[32] = {0};
                    char promptPos;
                    uint8_t attempts = 10;
                };

                struct lrrstatusFlagType
                {
                    int code;
                    uint8_t qual;
                    int data;
                    uint8_t partition;
                };

                std::vector<binary_sensor::BinarySensor *> bMap;
                std::vector<text_sensor::TextSensor *> tMap;

                struct textSensorPartition
                {
                    vistaECPTextSensor *system_status {NULL};
                    vistaECPTextSensor *line1 {NULL};
                    vistaECPTextSensor *line2 {NULL};
                    vistaECPTextSensor *beeps {NULL};
                };
                std::vector<textSensorPartition> text_sensors_partition;

                struct statusSensorPartition
                {
                    vistaECPBinarySensor *rdy {NULL};
                    vistaECPBinarySensor *trbl {NULL};
                    vistaECPBinarySensor *byp {NULL};
                    vistaECPBinarySensor *arm {NULL};
                    vistaECPBinarySensor *arma {NULL};
                    vistaECPBinarySensor *arms {NULL};
                    vistaECPBinarySensor *armi {NULL};
                    vistaECPBinarySensor *armn {NULL};
                    vistaECPBinarySensor *chm {NULL};
                    vistaECPBinarySensor *alm {NULL};
                    vistaECPBinarySensor *fire {NULL};
                };
                std::vector<statusSensorPartition> status_sensors_partition;

                struct textSensorCommon
                {
                    vistaECPTextSensor *zone_status {NULL};
                    vistaECPTextSensor *rf_messages {NULL};
                    vistaECPTextSensor *lrr_messages {NULL};
                };
                textSensorCommon text_sensors_common;

                const char *accessCode;
                const char *rfSerialLookup;
                bool quickArm;

                bool lrrSupervisor;
                uint8_t rfrEmulation[2];
                int defaultPartition;

                struct zoneType
                {
                    vistaECPBinarySensor *binary_sensor;
                    vistaECPTextSensor *text_sensor;
                    uint8_t zone;
                    uint64_t time;
                    uint32_t rfserial;
                    uint8_t rfloop;
                    uint64_t rfnext_hb;
                    uint8_t partition;
                    bool open;
                    bool bypass;
                    bool alarm;
                    bool check;
                    bool active;
                    bool rflowbat;
                };
                zoneType zonetype_INIT = 
                {
                    .binary_sensor = NULL,
                    .text_sensor = NULL,
                    .zone = 0,
                    .time = 0,
                    .rfserial = 0,
                    .rfloop = 0,
                    .rfnext_hb = 0,
                    .partition = 0,
                    .open = false,
                    .bypass = false,
                    .alarm = false,
                    .check = false,
                    .active = false,
                    .rflowbat = false
                };

                struct textSensor
                {
                    vistaECPTextSensor *text_sensor;
                    uint8_t partition;
                    const char * type;
                };
                textSensor textSensor_INIT = 
                {
                    .text_sensor = NULL,
                    .partition = 0,
                    .type = NULL
                };

                uint64_t lowBatteryTime;

                struct alarmStatusType
                {
                    uint64_t time;
                    bool state;
                    uint16_t zone;
                };

                bool displaySystemMsg = false;
                bool forceRefreshGlobal = false;
                bool forceRefreshZones, forceRefresh;
                sysState currentSystemState,previousSystemState;


                std::string previousZoneStatusMsg;

                alarmStatusType fireStatus, panicStatus, alarmStatus;
                uint8_t partitionTargets;
      
                std::vector<zoneType> alarmZones{};
                vistaECPBinarySensor *ac_bin_sensor = NULL;
                vistaECPBinarySensor *bat_bin_sensor = NULL;

                zoneType *getZone(uint16_t z);
                zoneType *getRfSerialLookup(uint32_t serialCode);

                void zoneStatusUpdate(zoneType *zt);

                statusFlagType statusFlags;
                lrrstatusFlagType lrrstatusFlags;
                void refreshStatusFlags(char * cbuf, struct statusFlagType * statusFlags);
                void refreshLRRStatusFlags(char * cbuf, struct lrrstatusFlagType * LRRstatusFlags); 

                void RF_handle_heartbeats();

                void alarm_disarm(std::string code, int32_t partition);
                void alarm_arm_home(int32_t partition);
                void alarm_arm_night(int32_t partition);
                void alarm_arm_away(int32_t partition);
                void alarm_trigger_fire(std::string code, int32_t partition);
                void alarm_trigger_panic(std::string code, int32_t partition);
                void alarm_keypress(std::string keystring);
                void alarm_keypress_partition(std::string keystring, int32_t partition);
                void send_cmd_bytes(int32_t addr, std::string hexbytes);

                bool isInt(std::string s, int base);
                int toDec(int n);
                long int toInt(std::string s, int base);
                bool areEqual(char *a1, char *a2, uint8_t len);

                int getZoneFromPrompt(char *p1);
                void printPacket(char cbuf[], int type, int src, int len);
                void updateDisplayLines(uint8_t partition);
        };
    } // namespace
} // namespace
