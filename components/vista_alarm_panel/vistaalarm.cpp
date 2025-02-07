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


        void vistaECPHome::publishStatusChange(sysState led, bool open, uint8_t partition)
        {
            std::string sensor = "NIL";
            switch (led)
            {
                case sfire:
                    sensor = "fire_";
                    break;
                case salarm:
                    sensor = "alm_";
                    break;
                case strouble:
                    sensor = "trbl_";
                    break;
                case sarmedstay:
                    sensor = "arms_";
                    break;
                case sarmedaway:
                    sensor = "arma_";
                    break;
                case sinstant:
                    sensor = "armi_";
                    break;
                case sready:
                    sensor = "rdy_";
                    break;
                case sac:
                    publishBinaryState("ac", 0, open);
                    return;
                case sbypass:
                    sensor = "byp_";
                    break;
                case schime:
                    sensor = "chm_";
                    break;
                case sbat:
                    publishBinaryState("bat", 0, open);
                    return;
                case sarmednight:
                    sensor = "armn_";
                    break;
                case sarmed:
                    sensor = "arm_";
                    break;
                case soffline:
                    break;
                case sunavailable:
                    break;
                default:
                    break;
            };
            publishBinaryState(sensor, partition, open);
        }


        void vistaECPHome::publishBinaryState(const std::string &idstr, uint8_t partition, bool open)
        {
            std::string id = idstr;
            if (partition)
                id += std::to_string(partition);

            auto it = std::find_if(bMap.begin(), bMap.end(), [id](binary_sensor::BinarySensor *bs)
                               { return bs->get_object_id() == id; });

            if (it != bMap.end() && (*it)->state != open)
                (*it)->publish_state(open);
        }


        void vistaECPHome::publishTextState(const std::string &idstr, uint8_t partition, std::string *text)
        {
            std::string id = idstr;
            if (partition)
                id += std::to_string(partition);
            auto it = std::find_if(tMap.begin(), tMap.end(), [id](text_sensor::TextSensor *ts)
                               { return ts->get_object_id() == id; });
            if (it != tMap.end() && (*it)->state != *text)
                (*it)->publish_state(*text);
        }


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
        }

        void vistaECPHome::zoneStatusUpdate(zoneType *zt)
        {
            if (zoneStatusChangeCallback != NULL)
            {
                std::string msg, zs1, lb;
                zs1 = zt->check ? "T" : zt->open ? "O"
                                         : "C";
                msg = zt->bypass ? "B" : zt->alarm ? "A"
                                           : "";
                lb = zt->lowbat || zt->rflowbat ? "L" : "";
                msg.append(zs1).append(lb);
                zoneStatusChangeCallback(zt->zone, msg.c_str());
            }

            if (zoneStatusChangeBinaryCallback != NULL)
            {
                if (zt->zone <= maxZones)
                {
                    zoneStatusChangeBinaryCallback(zt->zone, zt->open || zt->check);
                }
                else
                {
                    zoneStatusChangeBinaryCallback(zt->zone, zt->check || zt->open || zt->alarm || zt->trouble);
                }
            }
        }


        void vistaECPHome::loadZones()
        {
            for (auto obj : bMap)
            {
                createZoneFromId(obj->get_object_id().c_str());
            }

            for (auto obj : tMap)
            {
                createZoneFromId(obj->get_object_id().c_str());
            }
        }


        void vistaECPHome::createZoneFromId(const char * zid, uint8_t p)
        {
          char z_text[4];
          memset(z_text,'\0',sizeof(z_text));
          int start = 0;
          int len = 0;
          bool z_found = false;
          for (int i = 0; i < strlen(zid); i++)
          {
            if((zid[i] < 0x30 || zid[i] > 0x39) && z_found)
              break;
            if(zid[i] == 0x5A || zid[i] == 0x7A)
            {
              start = i+1;
              z_found = true;
            }
            if(zid[i] >= 0x30 && zid[i] <= 0x39 && z_found) 
            {
              len++;
            }
          }
          if (z_found && len > 0)
          {
            memcpy(z_text,zid+start,len);
            int z = toInt(z_text, 10);
            createZone(z, p);
          }
        }


        void vistaECPHome::createZone(uint16_t z, uint8_t p)
        {
            zoneType *zt = getZone(z);
            if (zt->zone == z)
                return;

            zoneType n;
            n.zone = z;
            n.active = true;
            n.partition = p;
            extZones.push_back(n);
            ESP_LOGD(TAG, "added zone %d", extZones.back().zone);
            if (zoneStatusChangeCallback != NULL)
                zoneStatusChangeCallback(z, "C");
            if (zoneStatusChangeBinaryCallback != NULL)
                zoneStatusChangeBinaryCallback(z, false);
        }


        std::string vistaECPHome::getZoneName(uint16_t zone, bool append)
        {
            std::string c = "z" + std::to_string(zone);
            auto it = std::find_if(bMap.begin(), bMap.end(), [c](binary_sensor::BinarySensor *bs)
                               { return bs->get_object_id() == c; });
            if (it != bMap.end())
            {
                if (append)
                    return std::string((*it)->get_name()).append(" (").append(std::to_string(zone)).append(")");
                else
                    return (*it)->get_name();
            }
            return std::to_string(zone);
        }


        vistaECPHome::zoneType *vistaECPHome::getZone(uint16_t z)
        {
            auto it = std::find_if(extZones.begin(), extZones.end(), [&z](zoneType &f)
                               { return f.zone == z; });
            if (it != extZones.end())
                return &(*it);

            return &zonetype_INIT;
        }


        vistaECPHome::serialType vistaECPHome::getRfSerialLookup(char *serialCode)
        {

            serialType rf;
            rf.zone = 0;
            if (rfSerialLookup != NULL && *rfSerialLookup)
            {
                std::string serial = serialCode;

                std::string s = rfSerialLookup;

                size_t pos, pos1, pos2;
                s.append(",");
                while ((pos = s.find(',')) != std::string::npos)
                {
                    std::string token, token1, token2, token3;
                    token = s.substr(0, pos);
                    pos1 = token.find(':');
                    pos2 = token.find(':', pos1 + 1);
                    token1 = token.substr(0, pos1); // serial
                    if (pos2 != std::string::npos)
                    {
                        token2 = token.substr(pos1 + 1, pos2 - pos1 - 1); // loop
                        token3 = token.substr(pos2 + 1);                  // zone
                    }
                    if (token1 == serial && token2 != "" && token3 != "")
                    {
                        rf.zone = toInt(token3, 10);
                        int8_t loop = toInt(token2, 10);
                        switch (loop)
                        {
                            case 1:
                                rf.mask = 0x80;
                                break;
                            case 2:
                                rf.mask = 0x20;
                                break;
                            case 3:
                                rf.mask = 0x10;
                                break;
                            case 4:
                                rf.mask = 0x40;
                                break;
                            default:
                                rf.mask = 0x80;
                                break;
                        }
                    break;
                    }
                    s.erase(0, pos + 1); /* erase() function store the current positon and move to next token. */
                }
            }
            return rf;
        }


        void vistaECPHome::setup()
        {
            ESP_LOGD(TAG, "Start setup: Free heap: (%lu)", esp_get_free_heap_size());

            bMap = App.get_binary_sensors();
            tMap = App.get_text_sensors();
            set_update_interval(250); // set interval to fire in main loop task
            loadZones();

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

            systemStatusChangeCallback(STATUS_ONLINE, 1);
            statusChangeCallback(sac, true, 1);

            vistabus.begin(uart1, rxPin, txPin, uart2, monitorPin);
            vistabus.emulateLRR(lrrSupervisor);

            // Disabling this for now.
            // set addresses of expander emulators
            //for (int x = 0; x < 9; x++)
            //{
            //  vista.zoneExpanders[x].expansionAddr = expanderAddr[x];
            //}

            for (uint8_t p = 0; p < maxPartitions; p++)
            {
                partitions[p] = 0;
                systemStatusChangeCallback(STATUS_NOT_READY, p + 1);
                beepsCallback("0", p + 1);
            }
            lrrMsgChangeCallback(" ");
            rfMsgChangeCallback(" ");
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
            //vista.setExpFault(zone, fault);
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
            line1DisplayCallback(p1.c_str(), partition);
            line2DisplayCallback(p2.c_str(), partition);
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
            AUIprocessQueue();
        }
    } //namespace
} // namespace
