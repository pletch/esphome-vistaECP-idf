#ifndef __VISTALARM_H__
#define __VISTALARM_H__

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
#define MAX_ZONES 48
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
        extern const char *const TAG;

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


        class vistaECPHome : public api::CustomAPIDevice, public time::RealTimeClock
        {

            public:
                vistaECPHome(char kpaddr = KP_ADDR, int receivePin = RX_PIN, int transmitPin = TX_PIN, int uartnum1 = DEF_UART1, 
                            int monitorTxPin = MONITOR_PIN, int uartnum2 = DEF_UART2, int maxzones = MAX_ZONES, int maxpartitions = MAX_PARTITIONS);

                std::function<void(int, std::string)> zoneStatusChangeCallback;
                std::function<void(uint16_t, bool)> zoneStatusChangeBinaryCallback;
                std::function<void(std::string, uint8_t)> systemStatusChangeCallback;
                std::function<void(sysState, bool, uint8_t)> statusChangeCallback;
                std::function<void(std::string, uint8_t)> systemMsgChangeCallback;
                std::function<void(std::string)> lrrMsgChangeCallback;
                std::function<void(std::string)> rfMsgChangeCallback;
                std::function<void(std::string, uint8_t)> line1DisplayCallback;
                std::function<void(std::string, uint8_t)> line2DisplayCallback;
                std::function<void(std::string, uint8_t)> beepsCallback;
                std::function<void(std::string)> zoneExtendedStatusCallback;
                std::function<void(uint8_t, int, bool)> relayStatusChangeCallback;

                void onZoneStatusChange(std::function<void(int zone, std::string msg)> callback)
                {
                    zoneStatusChangeCallback = callback;
                }
                void onZoneStatusChangeBinarySensor(std::function<void(int zone, bool open)> callback)
                {
                    zoneStatusChangeBinaryCallback = callback;
                }
                void onSystemStatusChange(std::function<void(std::string status, uint8_t partition)> callback)
                {
                    systemStatusChangeCallback = callback;
                }
                void onStatusChange(std::function<void(sysState led, bool isOpen, uint8_t partition)> callback)
                {
                    statusChangeCallback = callback;
                }
                void onSystemMsgChange(std::function<void(std::string msg, uint8_t partition)> callback)
                {
                    systemMsgChangeCallback = callback;
                }
                void onLrrMsgChange(std::function<void(std::string msg)> callback)
                {
                    lrrMsgChangeCallback = callback;
                }
                void onLine1DisplayChange(std::function<void(std::string msg, uint8_t partition)> callback)
                {
                    line1DisplayCallback = callback;
                }
                void onLine2DisplayChange(std::function<void(std::string msg, uint8_t partition)> callback)
                {
                    line2DisplayCallback = callback;
                }
                void onBeepsChange(std::function<void(std::string beeps, uint8_t partition)> callback)
                {
                    beepsCallback = callback;
                }
                void onZoneExtendedStatusChange(std::function<void(std::string zoneExtendedStatus)> callback)
                {
                    zoneExtendedStatusCallback = callback;
                }
                void onRelayStatusChange(std::function<void(uint8_t addr, int channel, bool state)> callback)
                {
                    relayStatusChangeCallback = callback;
                }
                void onRfMsgChange(std::function<void(std::string msg)> callback)
                {
                    rfMsgChangeCallback = callback;
                }

                void set_accessCode(const char *ac) { accessCode = ac; }
                void set_rfSerialLookup(const char *rf) { rfSerialLookup = rf; }
                void set_quickArm(bool qa) { quickArm = qa; }
                void set_displaySystemMsg(bool dsm) { displaySystemMsg = dsm; }
                void set_lrrSupervisor(bool ls) { lrrSupervisor = ls; }
                void set_auiaddr(uint8_t addr) { auiAddr = addr; };
                void set_expanderAddr(uint8_t idx, uint8_t addr)
                {
                    if (idx && idx < 10)
                        expanderAddr[idx - 1] = addr;
                }

                void set_maxZones(int mz) { maxZones = mz; }
                void set_maxPartitions(uint8_t mp) { maxPartitions = mp; }
                void set_partitionKeypad(uint8_t idx, uint8_t addr)
                {
                    if (idx && idx < 4)
                        partitionKeypads[idx] = addr;
                }

                void set_defaultPartition(uint8_t dp) { defaultPartition = dp; }
                void set_debug(uint8_t db) { debug = db; }
                void set_ttl(uint32_t t) { TTL = t; };
                void set_text(uint8_t text_idx, const char *text)
                {
                    switch (text_idx)
                    {
                        case 1:
                            FAULT = text;
                            break;
                        case 2:
                            BYPAS = text;
                            break;
                        case 3:
                            ALARM = text;
                            break;
                        case 4:
                            FIRE = text;
                            break;
                        case 5:
                            CHECK = text;
                            break;
                        case 6:
                            TRBL = text;
                            break;
                        case 7:
                            HITSTAR = text;
                            break;
                        default:
                            break;
                    }
                }

                std::vector<binary_sensor::BinarySensor *> bMap;
                std::vector<text_sensor::TextSensor *> tMap;

                void publishStatusChange(sysState led, bool open, uint8_t partition);
                void publishBinaryState(const std::string &cstr, uint8_t partition, bool open);
                void publishTextState(const std::string &cstr, uint8_t partition, std::string *text);

                bool displaySystemMsg = false;
                bool forceRefreshGlobal, forceRefreshZones, forceRefresh;
                sysState currentSystemState,
                previousSystemState;
                void stop();

            private:
                uint64_t TTL = 3000000;
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
                    uint8_t armedStay : 1;
                    uint8_t armedAway : 1;
                    uint8_t night : 1;
                    uint8_t instant : 1;
                    uint8_t chime : 1;
                    uint8_t acPower : 1;
                    uint8_t acLoss : 1;
                    uint8_t ready : 1;
                    uint8_t entryDelay : 1;
                    uint8_t programMode : 1;
                    uint8_t zoneBypass : 1;
                    uint8_t zoneAlarm : 1;
                    uint8_t alarm : 1;
                    uint8_t check : 1;
                    uint8_t systemFlag : 1;
                    uint8_t lowBattery : 1;
                    uint8_t systemTrouble : 1;
                    uint8_t fire : 1;
                    uint8_t fireZone : 1;
                    uint8_t backlight : 1;
                    uint8_t armed : 1;
                    uint8_t away : 1;
                    uint8_t bypass : 1;
                    uint8_t inAlarm : 1;
                    uint8_t noAlarm : 1;
                    uint8_t exitDelay : 1;
                    uint8_t cancel : 1;
                    uint8_t fault : 1;
                    uint8_t panicAlarm : 1;
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
                    uint8_t partition;;
                };

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
                    uint16_t zone;
                    uint64_t time;
                    uint8_t partition : 7;
                    uint8_t open : 1;
                    uint8_t bypass : 1;
                    uint8_t alarm : 1;
                    uint8_t check : 1;
                    uint8_t fire : 1;
                    uint8_t panic : 1;
                    uint8_t trouble : 1;
                    uint8_t lowbat : 1;
                    uint8_t active : 1;
                    uint8_t rflowbat : 1;
                };
                zoneType zonetype_INIT = {
                    .zone = 0,
                    .time = 0,
                    .partition = 0,
                    .open = 0,
                    .bypass = 0,
                    .alarm = 0,
                    .check = 0,
                    .fire = 0,
                    .panic = 0,
                    .trouble = 0,
                    .lowbat = 0,
                    .active = 0,
                    .rflowbat = 0};

                struct
                {
                    uint8_t bell : 1;
                    uint8_t wrx1 : 1;
                    uint8_t wrx2 : 1;
                    uint8_t loop : 1;
                    uint8_t duress : 1;
                    uint8_t panic1 : 1;
                    uint8_t panic2 : 1;
                    uint8_t panic3 : 1;
                } otherSup;

                uint64_t lowBatteryTime;

                struct alarmStatusType
                {
                    uint64_t time;
                    bool state;
                    uint16_t zone;
                    char prompt[17];
                };

                struct lrrType
                {
                    int code;
                    uint8_t qual;
                    uint16_t zone;
                    uint8_t user;
                };

                struct lightStates
                {
                    uint8_t away : 1;
                    uint8_t stay : 1;
                    uint8_t night : 1;
                    uint8_t instant : 1;
                    uint8_t bypass : 1;
                    uint8_t ready : 1;
                    uint8_t ac : 1;
                    uint8_t chime : 1;
                    uint8_t bat : 1;
                    uint8_t alarm : 1;
                    uint8_t check : 1;
                    uint8_t fire : 1;
                    uint8_t canceled : 1;
                    uint8_t trouble : 1;
                    uint8_t armed : 1;
                };

                lightStates currentLightState,
                previousLightState;
                enum lrrtype
                {
                    user_t,
                    zone_t
                };

                struct partitionStateType
                {
                    sysState previousSystemState;
                    lightStates previousLightState;
                    int lastbeeps;
                    bool refreshStatus;
                    bool refreshLights;
                };

                void AUIupdateZoneState(zoneType *zt, int p, bool state, uint64_t t);
                char * AUIparseMessage(char *cmd);
                void AUIprocessZoneList(char *list);
                void AUIsendZoneRequest();
                void AUIprocessF2(char * cbuf);
                void loadZones();

            public:
                partitionStateType *partitionStates;

                void disconnectVista()
                {
                    vistabus.stop();
                }
                bool connected()
                {
                    return vistabus.connected();
                }

                void setExpFault(int zone, bool fault)
                {
                    //vista.setExpFault(zone, fault);
                }
                void createZoneFromId(const char * zid,uint8_t p=0);

            private:
                std::string previousMsg,
                previousZoneStatusMsg;

                alarmStatusType fireStatus,
                panicStatus,
                alarmStatus;
                uint8_t partitionTargets;
                bool firstRun;

                struct serialType
                {
                    uint16_t zone;
                    int mask;
                };

                void createZone(uint16_t z,uint8_t p=0);
      
                auiCmdType auiCmd;
                std::vector<zoneType> extZones{};
                std::queue<auiCmdType> auiQueue{};

                zoneType nz;

                zoneType *getZone(uint16_t z);
                std::string getZoneName(uint16_t zone, bool append=false);
                serialType getRfSerialLookup(char *serialCode);

                void zoneStatusUpdate(zoneType *zt);
                void assignPartitionToZone(zoneType *zt);

                statusFlagType statusFlags;
                lrrstatusFlagType lrrstatusFlags;
                void refreshStatusFlags(char * cbuf, struct statusFlagType * statusFlags);
                void refreshLRRStatusFlags(char * cbuf, struct lrrstatusFlagType * LRRstatusFlags); 

                void setup() override;

                void AUIset_panel_time();

                void alarm_disarm(std::string code, int32_t partition);

                void alarm_arm_home(int32_t partition);

                void alarm_arm_night(int32_t partition);

                void alarm_arm_away(int32_t partition);

                void alarm_trigger_fire(std::string code, int32_t partition);

                void alarm_trigger_panic(std::string code, int32_t partition);

                void set_zone_fault(int32_t zone, bool fault);

                void alarm_keypress(std::string keystring);

                void alarm_keypress_partition(std::string keystring, int32_t partition);
                void send_cmd_bytes(int32_t addr, std::string hexbytes);

                private:
                bool isInt(std::string s, int base);

                int toDec(int n);

                long int toInt(std::string s, int base);

                bool areEqual(char *a1, char *a2, uint8_t len);

                int getZoneFromPrompt(char *p1);
                //std::string getNameFromPrompt(char *p1, char *p2);


                void printPacket(const char *label, char cbuf[], int len);

                void updateDisplayLines(uint8_t partition);

            public:
                void set_alarm_state(std::string const &state, std::string code = "", int partition = DEFAULTPARTITION);

            private:
                int getZoneFromChannel(uint8_t deviceAddress, uint8_t channel);

                void getPartitionsFromMask();

                public:
                void update() override;

                private:
        };
        extern vistaECPHome *alarmPanelPtr;
    } // namespace
} // namespace

#endif
