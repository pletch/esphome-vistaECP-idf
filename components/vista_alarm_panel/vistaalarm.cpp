/* for documentation see project at https://github.com/Dilbert66/esphome-vistaecp.  
 
This version is a highly changed FORK of this project!!

Key differences from original project:
- Arduino dependency removed and tailed for ESP-IDF.
- Refactored to support workflow associated with Vistabus class.
- Only targeted towards ESPHome API.  MQTT is removed in this version.
- Expander emulation and relay emulation are not present / enabled. May be added back in future.
- Limited testing of expanded functionality such as AUI traffic handling.  My panel does not
    seem to respond to AUI commands from either original project or this forked version.

*/

#include "vistaalarm.h"

#include <esp_chip_info.h>
#include <esp_task_wdt.h>

namespace esphome
{
    namespace alarm_panel
    {

        VistaBus vistabus;

        const char *const TAG = "vista_alarm";
        vistaECPHome *alarmPanelPtr;

        void vistaECPHome::stop()
        {
        vistabus.stop();
        vTaskDelete(processReceiveQHandle);
        processReceiveQHandle = NULL;
        }


        vistaECPHome::vistaECPHome(char kpaddr, int receivePin, int transmitPin, int uartnum1, int monitorTxPin, int uartnum2, int maxzones, int maxpartitions) : keypadAddr1(kpaddr),
                                                                                                                                                        rxPin(receivePin),
                                                                                                                                                        txPin(transmitPin),
                                                                                                                                                        uart1(uartnum1),
                                                                                                                                                        monitorPin(monitorTxPin),
                                                                                                                                                        uart2(uartnum2),
                                                                                                                                                        maxZones(maxzones),
                                                                                                                                                        maxPartitions(maxpartitions)
        {
            partitionKeypads = new char[maxPartitions + 1];
            partitions = new uint8_t[maxPartitions];
            partitionStates = new partitionStateType[maxPartitions];
            alarmPanelPtr = this;
            api_connection_state = false;
        }

        void vistaECPHome::zoneStatusUpdate(zoneType *zt)
        {
            if (zt->text_sensor != NULL)
            {
                std::string msg, zs1, lb;
                zs1 = zt->check ? "T" : zt->open ? "O"
                                         : "C";
                msg = zt->bypass ? "B" : zt->alarm ? "A"
                                           : "";
                lb = zt->lowbat || zt->rflowbat ? "L" : "";
                msg.append(zs1).append(lb);
                zt->text_sensor->process(msg);
            }

            if (zt->binary_sensor != NULL)
            {
                if (zt->zone <= maxZones)
                {                   
                    zt->binary_sensor->process(zt->open || zt->check);
                }
                else
                {
                    zt->binary_sensor->process(zt->check || zt->open || zt->alarm || zt->trouble);
                }
            }
        }


        void vistaECPHome::register_status_sensor(vistaECPBinarySensor *binary_sensor, uint8_t partition_number, const char * type)
        {
            if (strncmp(type,"READY", 3) == 0)
                status_sensors_partition[partition_number-1].rdy = binary_sensor;
            else if (strncmp(type, "TROUBLE", 3) == 0)
                status_sensors_partition[partition_number-1].trbl = binary_sensor;
            else if (strncmp(type, "BYPASS", 3) == 0)
                status_sensors_partition[partition_number-1].byp = binary_sensor;
            else if (strncmp(type, "ARMED_AWAY", 10) == 0)
                status_sensors_partition[partition_number-1].arma = binary_sensor;
            else if (strncmp(type, "ARMED_STAY", 10) == 0)
                status_sensors_partition[partition_number-1].arms = binary_sensor;
            else if (strncmp(type, "ARMED_INSTANT", 10)== 0)
                status_sensors_partition[partition_number-1].armi = binary_sensor;
            else if (strncmp(type, "ARMED_NIGHT", 10)== 0)
                status_sensors_partition[partition_number-1].armn = binary_sensor;
            else if (strncmp(type, "ARMED",5)== 0)
                status_sensors_partition[partition_number-1].arm = binary_sensor;
            else if (strncmp(type, "CHIME", 3)== 0)
                status_sensors_partition[partition_number-1].chm = binary_sensor;
            else if (strncmp(type, "ALARM", 3)== 0)
                status_sensors_partition[partition_number-1].alm = binary_sensor;
            else if (strncmp(type, "FIRE", 3)== 0)
                status_sensors_partition[partition_number-1].chm = binary_sensor;
            ESP_LOGI("","Registering partition sensor %s for partition %d.",type, partition_number);
        }


        void vistaECPHome::register_zone(vistaECPBinarySensor *binary_sensor, uint8_t partition_number, uint8_t zone_number, uint32_t rf_serial, uint8_t rf_loop)
        {
            auto it = std::find_if(alarmZones.begin(), alarmZones.end(), [zone_number](zoneType &f)
                { return f.zone == zone_number; });
            if (it != alarmZones.end())
            {
                it->binary_sensor = binary_sensor;
                it->rfserial = rf_serial;
                it->rfloop = rf_loop;

                //if(emulated)
                //    emulated_zones.push_back(zone_number);

                ESP_LOGI("","Adding binary zone sensor.  Zone: %d   rfserial:%lu   rfloop:%d",it->zone, it->rfserial, it->rfloop);
                return;
            }
            else 
            {
                zoneType zt = zonetype_INIT;
                zt.binary_sensor = binary_sensor;
                zt.partition = partition_number;
                zt.zone = zone_number;
                zt.rfserial = rf_serial;
                zt.rfloop = rf_loop;
                zt.active = true;
                alarmZones.push_back(zt);
                ESP_LOGI("","Registering zone.  Zone: %d   rfserial:%lu   rfloop:%d",zt.zone, zt.rfserial, zt.rfloop);
            } 
        }


        vistaECPHome::zoneType *vistaECPHome::getZone(uint16_t z)
        {
    
            auto it = std::find_if(alarmZones.begin(), alarmZones.end(), [=](zoneType &f)
                { return f.zone == z; });
            if (it != alarmZones.end())
                return &(*it);

            return NULL;

        }
    

        vistaECPHome::zoneType *vistaECPHome::getRfSerialLookup(uint32_t serialCode)
        {
            auto it = std::find_if(alarmZones.begin(), alarmZones.end(), [&serialCode](zoneType &f)
                { 
                    return f.rfserial == serialCode; });
            if (it != alarmZones.end())
                return &(*it);

            return NULL;
        }


        void vistaECPHome::register_zone_text(vistaECPTextSensor *text_sensor, uint8_t partition_number, uint8_t zone_number)
        {
            auto it = std::find_if(alarmZones.begin(), alarmZones.end(), [zone_number](zoneType &f)
                { return f.zone == zone_number; });
            if (it != alarmZones.end())
            {
                it->text_sensor = text_sensor;
                ESP_LOGI("","Adding text zone sensor.  Zone: %d",it->zone);
                return;
            }
            else 
            {
                zoneType zt = zonetype_INIT;
                zt.binary_sensor = NULL;
                zt.text_sensor = text_sensor;
                zt.partition = partition_number;
                zt.zone = zone_number;
                zt.active = true;
                alarmZones.push_back(zt);
                ESP_LOGI("","Registering zone.  Zone: %d   rfserial:%lu   rfloop:%d",zt.zone, zt.rfserial, zt.rfloop);
            } 
        }


        void vistaECPHome::register_text_sensor(vistaECPTextSensor *text_sensor, uint8_t partition_number, const char * type)
        {
            if (strncmp(type,"SYSTEM_STATUS", 13) == 0)
                text_sensors_partition[partition_number-1].system_status = text_sensor;
            else if (strncmp(type, "LRR_MESSAGES", 12) == 0)
                text_sensors_common.lrr_messages = text_sensor;
            else if (strncmp(type, "RF_MESSAGES", 11) == 0)
                text_sensors_common.rf_messages = text_sensor;
            else if (strncmp(type, "LINE1", 5) == 0)
                text_sensors_partition[partition_number-1].line1 = text_sensor;
            else if (strncmp(type, "LINE2", 5) == 0)
                text_sensors_partition[partition_number-1].line2 = text_sensor;
            else if (strncmp(type, "ZONE_STATUS", 11)== 0)
                text_sensors_common.zone_status = text_sensor;
            else if (strncmp(type, "BEEPS", 5) == 0)
                text_sensors_partition[partition_number-1].beeps = text_sensor;
            ESP_LOGI("","Registering text sensor %s for partition %d.",type, partition_number);
        }


        void vistaECPHome::setup()
        {
            ESP_LOGD(TAG, "Start setup: Free heap: (%lu)", esp_get_free_heap_size());

            set_update_interval(1000); // set interval to fire in main loop task

            register_service(&vistaECPHome::AUIset_panel_time, "set_panel_time", {});
            register_service(&vistaECPHome::alarm_keypress, "alarm_keypress", {"keys"});
            register_service(&vistaECPHome::send_cmd_bytes, "send_cmd_bytes", {"addr", "hexdata"});
            register_service(&vistaECPHome::alarm_keypress_partition, "alarm_keypress_partition", {"keys", "partition"});
            register_service(&vistaECPHome::alarm_disarm, "alarm_disarm", {"code", "partition"});
            register_service(&vistaECPHome::alarm_arm_home, "alarm_arm_home", {"partition"});
            register_service(&vistaECPHome::alarm_arm_night, "alarm_arm_night", {"partition"});
            register_service(&vistaECPHome::alarm_arm_away, "alarm_arm_away", {"partition"});
            register_service(&vistaECPHome::alarm_trigger_panic, "alarm_trigger_panic", {"code", "partition"});
            register_service(&vistaECPHome::alarm_trigger_fire, "alarm_trigger_fire", {"code", "partition"});
            register_service(&vistaECPHome::set_zone_fault, "set_zone_fault", {"zone", "fault"});

            // Disabling this for now.
            // set addresses of expander emulators
            //for (int x = 0; x < 9; x++)
            //{
            //  vista.zoneExpanders[x].expansionAddr = expanderAddr[x];
            //}
            if(text_sensors_partition[0].system_status != NULL)
                text_sensors_partition[0].system_status->process(STATUS_ONLINE);
            for (uint8_t p = 0; p < maxPartitions; p++)
            {
                partitions[p] = 0;
                if (text_sensors_partition[p].system_status != NULL)
                    text_sensors_partition[p].system_status->process(STATUS_NOT_READY);             
                if (text_sensors_partition[p].beeps != NULL)
                    text_sensors_partition[p].beeps->process("0");
            }
            if (text_sensors_common.lrr_messages != NULL)
                text_sensors_common.lrr_messages->process(" ");
            if (text_sensors_common.rf_messages != NULL)
                text_sensors_common.rf_messages->process(" ");
\
            esp_chip_info_t info;
            esp_chip_info(&info);

            xTaskCreate
            (
                this->processReceiveQueue_task_start, // Function to implement the task
                "pRQtask",       // Name of the task
                3072,                      // Stack size in words
                (void *)this,              // Task input parameter
                10,                        // Priority of the task
                &processReceiveQHandle              // Task handle.
            );
            vistabus.emulateLRR(lrrSupervisor);    
            vistabus.begin(uart1, rxPin, txPin, uart2, monitorPin);
            ESP_LOGD(TAG, "Completed setup. Free heap=%lu", esp_get_free_heap_size()); 
        }

        void vistaECPHome::alarm_disarm(std::string code, int32_t partition)
        {
            set_alarm_state("D", code, partition);
        }

        void vistaECPHome::alarm_arm_home(int32_t partition)
        {
            set_alarm_state("S", "", partition);
        }

        void vistaECPHome::alarm_arm_night(int32_t partition)
        {
            set_alarm_state("N", "", partition);
        }

        void vistaECPHome::alarm_arm_away(int32_t partition)
        {
            set_alarm_state("A", "", partition);
        }

        void vistaECPHome::alarm_trigger_fire(std::string code, int32_t partition)
        {
            set_alarm_state("F", code, partition);
        }

        void vistaECPHome::alarm_trigger_panic(std::string code, int32_t partition)
        {
            set_alarm_state("P", code, partition);
        }

        void vistaECPHome::set_zone_fault(int32_t zone, bool fault)
        {
            vistabus.setExpFaultBits(zone, fault);
        }

        void vistaECPHome::set_maxPartitions(uint8_t mp)
        {
            maxPartitions = mp;
            for (int i=0; i < mp; i++)
            {
                textSensorPartition ts;
                statusSensorPartition ss;
                text_sensors_partition.push_back(ts);
                status_sensors_partition.push_back(ss);
            }
        }

        void vistaECPHome::alarm_keypress(std::string keystring)
        {
            alarm_keypress_partition(keystring, defaultPartition);
        }

        void vistaECPHome::alarm_keypress_partition(std::string keystring, int32_t partition)
        {
            if (keystring == "R")
            {
                forceRefreshGlobal = true;
                forceRefresh = true;
                return;
            }
            if (keystring == "A")
            {
                set_alarm_state("A", "", partition);
                return;
            }
            if (keystring == "S")
            {
                set_alarm_state("S", "", partition);
                return;
            }
            if (keystring == "N")
            {
                set_alarm_state("N", "", partition);
                return;
            }
            if (keystring == "I")
            {
                set_alarm_state("I", "", partition);
                return;
            }
            if (keystring == "B")
            {
                set_alarm_state("B", "", partition);
                return;
            }
            if (keystring == "Y")
            {
                set_alarm_state("Y", "", partition);
                return;
            }
            if (keystring == "D")
            {
                set_alarm_state("D", "", partition);
                return;
            }

            if (!partition)
                partition = defaultPartition;
            if (debug > 0)
                ESP_LOGD(TAG, "Writing keys: %s to partition %li", keystring.c_str(), partition);

            uint8_t addr = 0;
            if (partition > maxPartitions || partition < 1)
                return;
            addr = partitionKeypads[partition];
            if (addr > 0 and addr < 24)
                vistabus.write(keystring.c_str(), keystring.length(), addr);
        }

        void vistaECPHome::send_cmd_bytes(int32_t addr, std::string hexbytes)
        {
            ESP_LOGD(TAG, "Cmd bytes=%s", hexbytes.c_str());
            std::string::iterator end_pos = std::remove(hexbytes.begin(), hexbytes.end(), ' ');
            hexbytes.erase(end_pos, hexbytes.end());

            int NumberChars = hexbytes.length();
            char *bytes = new char[NumberChars / 2];
            for (int i = 0; i < NumberChars; i += 2)
            {
                bytes[i / 2] = toInt(hexbytes.substr(i, 2), 16);
            }
            vistabus.writedirect(bytes, sizeof(bytes),addr);

            return;
        }

        bool vistaECPHome::isInt(std::string s, int base)
        {
            if (s.empty() || std::isspace(s[0]))
                return false;
            char *p;
            strtol(s.c_str(), &p, base);
            return (*p == 0);
        }

        int vistaECPHome::toDec(int n)
        {
            char b[4];
            char *p;
            itoa(n, b, 16);
            long int li = strtol(b, &p, 10);
            return (int)li;
        }

        long int vistaECPHome::toInt(std::string s, int base)
        {
            if (s.empty() || std::isspace(s[0]))
                return 0;
            char *p;
            long int li = strtol(s.c_str(), &p, base);
            return li;
        }

        bool vistaECPHome::areEqual(char *a1, char *a2, uint8_t len)
        {
            for (int x = 0; x < len; x++)
            {
                if (a1[x] != a2[x])
                    return false;     
            }
            return true;
        }

        void vistaECPHome::updateDisplayLines(uint8_t partition)
        {
            uint8_t pos = statusFlags.promptPos;
            std::string p1 = statusFlags.prompt1;
            std::string p2 = statusFlags.prompt2;
            if (pos > 0)
            {
                char buf[10];
                std::string sub1, sub2;
                if (pos > 15)
                {
                    sub1 = p2.substr(0, pos - 16);
                    if (pos < 31)
                        sub2 = p2.substr(pos - 15);
                    sprintf(buf, "[%c]", p2[pos - 16]);
                    p2 = sub1 + std::string(buf) + sub2;
                }
                else
                {
                    sub1 = p1.substr(0, pos);
                    if (pos < 15)
                        sub2 = p1.substr(pos + 1);
                    sprintf(buf, "[%c]", p2[pos]);
                    p1 = sub1 + std::string(buf) + sub2;
                }
            }
            if (text_sensors_partition[partition-1].line1 != NULL)
                text_sensors_partition[partition-1].line1->process(p1);
            if (text_sensors_partition[partition-1].line2 != NULL)
                text_sensors_partition[partition-1].line2->process(p2);
        }

    /*std::string vistaECPHome::getNameFromPrompt(char *p1, char *p2)  <-- not used for anything at this time. 
                                                                      Need to refactor to eliminate regex pattern matching with digit search if enabling.
    {
        std::string p = std::string(p1) + std::string(p2);

        MatchState ms;
        char buf[5];
        char buf1[20];
        ms.Target((char *)p.c_str());
        char res = ms.Match("[%a]+%s+([%d]+)%s*(.*)");
        if (res == REGEXP_MATCHED)
        {
            ms.GetCapture(buf, 0);
            ms.GetCapture(buf1, 1);
            ESP_LOGD(TAG, "name match=%s,zone=%s", buf1, buf);
            return std::string(buf1);
        }
        return "";
    }*/

        int vistaECPHome::getZoneFromPrompt(char *p1)
        {
            char z_text[4];
            memset(z_text,'\0',sizeof(z_text));
            int start = 0;
            int len = 0;
            bool z_found = false;
            for (int i = 0; i < 18; i++)
            {
                if(p1[i] == 0x20)
                {
                    start = i+1;
                    z_found = true;
                }
                if(p1[i] >= 0x30 && p1[i] <= 0x39 && z_found) 
                {
                    len++;
                }
                if(p1[i] == 0x20 && z_found) 
                {
                    break;
                }
            }
            if (z_found && len > 0 && len < 5)
            {
                strncpy(z_text,p1+start,len);
                int z = toInt(z_text, 10);
                ESP_LOGD(TAG, "zone match=%d", z);
                return z;
            }
            else
            {
                ESP_LOGD(TAG, "No zone match");
            }
            return 0;
        }

        void vistaECPHome::printPacket(const char *label, char cbuf[], int len)
        {
            char s1[4];

            std::string s = "";

            char s2[25];
            ESPTime rtc = now();
            sprintf(s2, "[%02d:%02d:%02d]", rtc.hour, rtc.minute, rtc.second);

            for (int c = 0; c < len; c++)
            {
                sprintf(s1, "%02X ", cbuf[c]);
                s = s.append(s1);
            }
            ESP_LOGI(label, "%s %s", s2, s.c_str());
        }

        void vistaECPHome::set_alarm_state(std::string const &state, std::string code, int partition)
        {

            if (code.length() != 4 || !isInt(code, 10))
                code = accessCode; // ensure we get a numeric 4 digit code

            uint8_t addr = 0;
            if (partition > maxPartitions || partition < 1)
                return;
            addr = partitionKeypads[partition];
            if (addr < 1 || addr > 23)
                return;

            // Arm stay
            if (state.compare("S") == 0 && !partitionStates[partition - 1].previousLightState.armed)
            {
                if (quickArm)
                    vistabus.write("#3",1, addr);
                else if (code.length() == 4)
                {
                    char send_str[5];
                    memcpy(send_str,code.c_str(),4);
                    memcpy(send_str+4,"3",1);
                    vistabus.write(send_str,5, addr);
                }
            }
            // Arm away
            else if ((state.compare("A") == 0 || state.compare("W") == 0) && !partitionStates[partition - 1].previousLightState.armed)
            {
                if (quickArm)
                    vistabus.write("#2",1, addr);
                else if (code.length() == 4)
                {
                    char send_str[5];
                    memcpy(send_str,code.c_str(),4);
                    memcpy(send_str+4,"2",1);
                    vistabus.write(send_str,5, addr);
                }
            }
            else if (state.compare("I") == 0 && !partitionStates[partition - 1].previousLightState.armed)
            {
                if (quickArm)
                    vistabus.write("#7",1, addr);
                else if (code.length() == 4)
                {
                    char send_str[5];
                    memcpy(send_str,code.c_str(),4);
                    memcpy(send_str+4,"7",1);
                    vistabus.write(send_str,5, addr);
                }
            }
            else if (state.compare("N") == 0 && !partitionStates[partition - 1].previousLightState.armed)
            {
                if (quickArm)
                    vistabus.write("#33",1, addr);
                else if (code.length() == 4)
                {
                char send_str[6];
                memcpy(send_str,code.c_str(),4);
                memcpy(send_str+4,"33",1);
                vistabus.write(send_str,6, addr);
                }
            }
            // Fire command
            else if (state.compare("F") == 0)
            {
                // todo
            }
            // Panic command
            else if (state.compare("P") == 0)
            {
                // todo
            }
            else if (state.compare("B") == 0)
            {
                if (code.length() == 4)
                {
                    char send_str[6];
                    memcpy(send_str,code.c_str(),4);
                    memcpy(send_str+4,"6#",1);
                    vistabus.write(send_str,6, addr);
                }
            }
            else if (state.compare("Y") == 0)
            {
                if (code.length() == 4)
                {
                    char send_str[7];
                    memcpy(send_str,code.c_str(),4);
                    memcpy(send_str+4,"600",1);
                    vistabus.write(send_str,7, addr);
                }
            }
            else if (state.compare("D") == 0)
            {
                if (code.length() == 4)
                { // ensure we get 4 digit code
                    char send_str[5];
                    memcpy(send_str,code.c_str(),4);
                    memcpy(send_str+4,"1",1);
                    vistabus.write(send_str,5, addr);
                }
            }
        }

        int vistaECPHome::getZoneFromChannel(uint8_t deviceAddress, uint8_t channel)
        {
            switch (deviceAddress)
            {
                case 7:
                    return channel + 8;
                case 8:
                    return channel + 16;
                case 9:
                    return channel + 24;
                case 10:
                    return channel + 32;
                case 11:
                    return channel + 40;
                default:
                    return 0;
            }
        }

        void vistaECPHome::assignPartitionToZone(zoneType *zt)
        {

            for (int p = 1; p < 4; p++)
            {
                if (partitions[p - 1])
                {
                    ESP_LOGD(TAG, "Assigning partition %d, to zone %d", p, zt->zone);
                    zt->partition = p;
                    break;
                }
            }
        }

        void vistaECPHome::getPartitionsFromMask()
        {
            partitionTargets = 0;
            memset(partitions, 0, maxPartitions);
            for (uint8_t p = 1; p <= maxPartitions; p++)
            {
                for (int8_t i = 3; i >= 0; i--)
                {
                    int8_t shift = partitionKeypads[p] - (8 * i);
                    if (shift >= 0 && (statusFlags.keypad[i] & (0x01 << shift)))
                    {
                        partitionTargets = partitionTargets + 1;
                        partitions[p - 1] = 1;
                        break;
                    }
                }
            }
        }

        void vistaECPHome::update()
        {    
            if (!vistabus.connected() && esp_timer_get_time() - last_refresh > 30*1000*1000)
            {
                ESP_LOGE(TAG, "Data timeout. Is the panel connected?");
                last_refresh = esp_timer_get_time();
                return;
            }      
        }
    } //namespace
} // namespace
