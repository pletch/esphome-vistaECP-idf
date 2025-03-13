#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/api/custom_api_device.h"
#include "esphome/core/defines.h"

#include "vistabus.h"
#include "LRRstrings.h"
#include <string>
#include "paneltext.h"

// for documentation see project at https://github.com/Dilbert66/esphome-vistaecp

#define KP_ADDR 17 // only used as a default if not set in the yaml
#define MAX_ZONES 32
#define MAX_PARTITIONS 3
#define DEFAULTPARTITION 1

// default pins to use for serial comms to the panel
// The pinouts below are only examples. You can choose any other gpio pin that is available and not a strapping pin used for boot.
// These have proven to work fine.

// esp32 use gpio pins 4,13,16-39
#define RX_PIN 21
#define TX_PIN 23
#define DEF_UART1 1
#define MONITOR_PIN 22 // pin used to monitor the green TX line (3.3 level dropped from 12 volts via voltage divider)
#define DEF_UART2 2

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
            scanceled,
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
            sarming,
            spending
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
            rsdate,
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
                vistaECPHome(char kpaddr = KP_ADDR, int receivePin = RX_PIN, int transmitPin = TX_PIN, int uartnum1 = DEF_UART1, 
                            int monitorTxPin = MONITOR_PIN, int uartnum2 = DEF_UART2, int maxzones = MAX_ZONES, int maxpartitions = MAX_PARTITIONS);

                void set_accessCode(const char *ac) { accessCode = ac; }
                void set_rfSerialLookup(const char *rf) { rfSerialLookup = rf; }
                void set_quickArm(bool qa) { quickArm = qa; }
                void set_displaySystemMsg(bool dsm) { displaySystemMsg = dsm; }
                void set_lrrSupervisor(bool ls) { lrrSupervisor = ls; }
                void set_auiaddr(uint8_t addr) { auiAddr = addr; };

                void set_maxZones(int mz) { maxZones = mz; }
                void set_maxPartitions(uint8_t mp);
                void set_partitionKeypad(uint8_t idx, uint8_t addr)
                {
                    if (idx && idx < 4)
                        partitionKeypads[idx] = addr;
                }
                void set_alarm_state(std::string const &state, std::string code = "", int partition = DEFAULTPARTITION);

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
                void stop();

            protected:
                const char *const TAG = "v-a";
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
                int maxZones = 0;
                int maxPartitions = 0;
                uint8_t auiAddr = 0;
                bool AUIsendTime();
                char auiSeq = 8;
                void AUIprocessQueue();
                void processReceiveQueue(void *args);
                static void processReceiveQueue_task_start(void *args);
                TaskHandle_t processReceiveQHandle;
                bool api_connection_state;

                struct auiCmdType
                {
                    reqStates state = rsidle;
                    uint64_t time = 0;
                    uint8_t partition = 0;
                    uint8_t records = 0;
                    uint8_t record = 0;
                    bool pending = false;
                };

                struct statusFlagType
                {
                    char beeps : 3;
                    bool armedStay;
                    bool armedAway;
                    bool night;
                    bool instant;
                    bool chime;
                    bool acPower;
                    bool acLoss;
                    bool ready;
                    bool entryDelay;
                    bool programMode;
                    bool zoneBypass;
                    bool zoneAlarm;
                    bool alarm;
                    bool check;
                    bool systemFlag;
                    bool lowBattery;
                    bool systemTrouble;
                    bool fire;
                    bool fireZone;
                    bool backlight;
                    bool armed;
                    bool away;
                    bool bypass;
                    bool inAlarm;
                    bool noAlarm;
                    bool exitDelay;
                    bool cancel;
                    bool fault;
                    bool panicAlarm;
                    char keypad[4];
                    int zone;
                    char prompt1[18];
                    char prompt2[18];
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

                bool lrrSupervisor, vh;
                char *partitionKeypads;
                int defaultPartition = DEFAULTPARTITION;
                char expanderAddr[9] = {};

                uint8_t *partitions;
                std::string topic_prefix, topic;

                struct zoneType
                {
                    vistaECPBinarySensor *binary_sensor;
                    vistaECPTextSensor *text_sensor;
                    uint8_t zone;
                    uint64_t time;
                    uint32_t rfserial;
                    uint8_t rfloop;
                    uint8_t partition;
                    bool open;
                    bool bypass;
                    bool alarm;
                    bool check;
                    bool fire;
                    bool panic;
                    bool trouble;
                    bool lowbat;
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
                    .partition = 0,
                    .open = false,
                    .bypass = false,
                    .alarm = false,
                    .check = false,
                    .fire = false,
                    .panic = false,
                    .trouble = false,
                    .lowbat = false,
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
                    char prompt[17];
                };

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
                    bool canceled;
                    bool trouble;
                    bool armed;
                };

                lightStates currentLightState,
                previousLightState;

                struct partitionStateType
                {
                    sysState previousSystemState;
                    lightStates previousLightState;
                    int lastbeeps;
                    bool refreshStatus;
                    bool refreshLights;
                };

                bool displaySystemMsg = false;
                bool forceRefreshGlobal, forceRefreshZones, forceRefresh;
                sysState currentSystemState,
                previousSystemState;
                partitionStateType *partitionStates;

                void AUIupdateZoneState(zoneType *zt, int p, bool state, uint64_t t);
                char * AUIparseMessage(char *cmd);
                void AUIprocessZoneList(char *list);
                void AUIsendZoneRequest();
                void AUIprocessF2(char * cbuf);
                void loadZones();

                std::string previousZoneStatusMsg;

                alarmStatusType fireStatus, panicStatus, alarmStatus;
                uint8_t partitionTargets;

                void createZone(uint16_t z,uint8_t p=0);
      
                auiCmdType auiCmd;
                std::vector<zoneType> alarmZones{};
                vistaECPBinarySensor *ac_bin_sensor = NULL;
                vistaECPBinarySensor *bat_bin_sensor = NULL;

                std::queue<auiCmdType> auiQueue{};

                zoneType nz;
                zoneType *getZone(uint16_t z);

                zoneType *getRfSerialLookup(uint32_t serialCode);

                void zoneStatusUpdate(zoneType *zt);
                void assignPartitionToZone(zoneType *zt);

                statusFlagType statusFlags;
                lrrstatusFlagType lrrstatusFlags;
                void refreshStatusFlags(char * cbuf, struct statusFlagType * statusFlags);
                void refreshLRRStatusFlags(char * cbuf, struct lrrstatusFlagType * LRRstatusFlags); 

                void AUIset_panel_time();
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
                //std::string getNameFromPrompt(char *p1, char *p2);
                void printPacket(const char *label, char cbuf[], int len);
                void updateDisplayLines(uint8_t partition);

                int getZoneFromChannel(uint8_t deviceAddress, uint8_t channel);

                void getPartitionsFromMask();

                public:
                void update() override;

        };
        extern vistaECPHome *alarmPanelPtr;
    } // namespace
} // namespace
