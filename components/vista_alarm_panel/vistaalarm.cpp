/* for documentation see project at https://github.com/Dilbert66/esphome-vistaecp.  
 
This version is a highly changed FORK of this project!!

Key differences from original project:
- Arduino dependency removed and tailored for ESP-IDF.
- Refactored to support workflow associated with Vistabus class.
- Only targeted towards ESPHome API.  MQTT is removed in this version.
- Relay emulation not implemented. Expander emulation is enabled with support for fault & tamper detection on zones.

*/

#include "vistaalarm.h"

namespace esphome
{
    namespace alarm_panel
    {
        void vistaECPHome::stop()
        {
            vistabus.stop();
            vTaskDelete(processReceiveQHandle);
            processReceiveQHandle = NULL;
        }

        vistaECPHome::vistaECPHome(char kpaddr, int receivePin, int transmitPin, 
            int uartnum1, int monitorTxPin, int uartnum2) : keypadAddr1(kpaddr),
                                                            rxPin(receivePin),
                                                            txPin(transmitPin),
                                                            uart1(uartnum1),
                                                            monitorPin(monitorTxPin),
                                                            uart2(uartnum2)
        {
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
                lb = zt->rflowbat ? "L" : "";
                msg.append(zs1).append(lb);
                zt->text_sensor->process(msg);
            }

            if (zt->binary_sensor != NULL)
            {       
                zt->binary_sensor->process(zt->open || zt->check);
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
                status_sensors_partition[partition_number-1].fire = binary_sensor;
            ESP_LOGI("","Registering partition sensor %s for partition %d.",type, partition_number);
        }


        void vistaECPHome::register_zone(vistaECPBinarySensor *binary_sensor, uint8_t partition_number, uint8_t zone_number, 
            uint32_t rf_serial, uint8_t rf_loop, bool emulated)
        {
            for (auto &it: alarmZones)
            {
                if (it.zone == zone_number )
                {
                    it.binary_sensor = binary_sensor;
                    it.rfserial = rf_serial;
                    it.rfloop = rf_loop;
                    
                    ESP_LOGI(TAG,"Adding binary zone sensor.  Zone: %d   rfserial:%lu   rfloop:%d",it.zone, it.rfserial, it.rfloop);
                    return;
                }
            }
            zoneType zt = zonetype_INIT;
            zt.binary_sensor = binary_sensor;
            zt.partition = partition_number;
            zt.zone = zone_number;
            zt.rfserial = rf_serial;
            zt.rfloop = rf_loop;
            zt.rfnext_hb = 1;
            zt.active = true;
            alarmZones.push_back(zt);
            if (rf_serial == 0)
                if (emulated)
                    ESP_LOGI(TAG,"Registering emulated hardwired zone.  Zone: %d",zt.zone);
                else
                    ESP_LOGI(TAG,"Registering hardwired zone.  Zone: %d",zt.zone);
            else
                if (emulated)
                    ESP_LOGI(TAG,"Registering emulated wireless zone.  Zone: %d   rfserial:%lu   rfloop:%d",zt.zone, zt.rfserial, zt.rfloop);
                else
                    ESP_LOGI(TAG,"Registering wireless zone.  Zone: %d   rfserial:%lu   rfloop:%d",zt.zone, zt.rfserial, zt.rfloop);
        }


        vistaECPHome::zoneType *vistaECPHome::getZone(uint16_t z)
        {
            for (auto &it: alarmZones)
            {
                if (it.zone == z)
                    return &(it);
            }
            return NULL;
        }
    

        vistaECPHome::zoneType *vistaECPHome::getRfSerialLookup(uint32_t serialCode)
        {
            for (auto &it: alarmZones)
            {
                if (it.rfserial == serialCode)
                    return &(it);
            }
            return NULL;
        }


        void vistaECPHome::register_zone_text(vistaECPTextSensor *text_sensor, uint8_t partition_number, uint8_t zone_number)
        {
            for (auto &it: alarmZones)
            {
                if (it.zone == zone_number )
                {
                    it.text_sensor = text_sensor;
                    ESP_LOGI("","Adding text zone sensor.  Zone: %d",it.zone);
                    return;
                }
            }
            zoneType zt = zonetype_INIT;
            zt.binary_sensor = NULL;
            zt.text_sensor = text_sensor;
            zt.partition = partition_number;
            zt.zone = zone_number;
            zt.active = true;
            alarmZones.push_back(zt);
            ESP_LOGI("","Registering zone.  Zone: %d   rfserial:%lu   rfloop:%d",zt.zone, zt.rfserial, zt.rfloop); 
        }


        void vistaECPHome::register_text_sensor(vistaECPTextSensor *text_sensor, uint8_t partition_number, const char * type)
        {
            if (partition_number == 0 || partition_number-1 < known_partitions.size())
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
            else
            {
                ESP_LOGE("","No keypad assigned to partition %d. Aborting %s text sensor registration.",partition_number, type);
            }
        }


        void vistaECPHome::setup()
        {
            ESP_LOGD(TAG, "Start setup: Free heap: (%lu)", esp_get_free_heap_size());

            set_update_interval(5000); // set interval frequency in main loop task

            register_service(&vistaECPHome::AUIset_panel_time, "set_panel_time", {});
            register_service(&vistaECPHome::alarm_keypress, "alarm_keypress", {"keys"});
            register_service(&vistaECPHome::alarm_keypress_partition, "alarm_keypress_partition", {"keys", "partition"});
            register_service(&vistaECPHome::alarm_disarm, "alarm_disarm", {"code", "partition"});
            register_service(&vistaECPHome::alarm_arm_home, "alarm_arm_home", {"partition"});
            register_service(&vistaECPHome::alarm_arm_night, "alarm_arm_night", {"partition"});
            register_service(&vistaECPHome::alarm_arm_away, "alarm_arm_away", {"partition"});
            register_service(&vistaECPHome::alarm_trigger_panic, "alarm_trigger_panic", {"code", "partition"});
            register_service(&vistaECPHome::alarm_trigger_fire, "alarm_trigger_fire", {"code", "partition"});
            register_service(&vistaECPHome::set_zone_fault, "set_zone_fault", {"zone", "fault"});
            register_service(&vistaECPHome::set_emulated_zone_tamper, "set_emulated_zone_tamper_state", {"zone", "tamper active"});

            if(text_sensors_partition[0].system_status != NULL)
                text_sensors_partition[0].system_status->process(STATUS_ONLINE);
            for (uint8_t p = 0; p < known_partitions.size(); p++)
            {
                if (text_sensors_partition[p].system_status != NULL)
                    text_sensors_partition[p].system_status->process(STATUS_NOT_READY);             
                if (text_sensors_partition[p].beeps != NULL)
                    text_sensors_partition[p].beeps->process("0");
            }
            if (text_sensors_common.lrr_messages != NULL)
                text_sensors_common.lrr_messages->process(" ");
            if (text_sensors_common.rf_messages != NULL)
                text_sensors_common.rf_messages->process(" ");

            xTaskCreate
            (
                this->processReceiveQueue_task_start, // Function to implement the task
                "pRQtask",       // Name of the task
                4096,                      // Stack size in words
                (void *)this,              // Task input parameter
                10,                        // Priority of the task
                &processReceiveQHandle              // Task handle.
            );
            vistabus.emulateLRR(lrrSupervisor);
            if (rfrEmulation[0])
                vistabus.emulateRFR(rfrEmulation[1]);    
            vistabus.begin(uart1, rxPin, txPin, uart2, monitorPin);
            last_connection_check = esp_timer_get_time();
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
            for (const auto &it: alarmZones)
            {
                if (it.zone == zone)
                {
                    if (it.rfserial)
                    {
                        int mask;
                        switch (it.rfloop)
                        {
                            case 1:
                                mask = 0x80;
                                break;
                            case 2:
                                mask = 0x20;
                                break;
                            case 3:
                                mask = 0x10;
                                break;
                            case 4:
                                mask = 0x40;
                                break;
                            default:
                                mask = 0x80;
                                break;
                        }
                        uint8_t msg = fault ? 0x80 | mask : 0x80 ^ mask;
                        ESP_LOGI(TAG,"setting virtual rf serial: %i fault: %i", zone, fault);
                        vistabus.sendRFmsg(it.rfserial,msg);
                    }
                    else
                    {
                        ESP_LOGI(TAG,"setting virtual hardwired zone: %i fault: %i", zone, fault);
                        vistabus.setExpFaultBits(zone, fault);
                    }
                    return;
                }
            }            
        }

        void vistaECPHome::set_emulated_zone_tamper(int32_t zone, bool tamper_active)
        {
            vistabus.setExpTamper(zone, tamper_active);
        }

        void vistaECPHome::initialize_partition_sensors()
        {
            for (int i=0; i < known_partitions.size(); i++)
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

            uint8_t addr = 0;
            uint8_t seq = 0;
            if (partition > 8 || partition < 1)
                return;
            addr = known_partitions[partition - 1].assigned_keypad;
            seq = known_partitions[partition - 1].keypad_sequence;
            bool result = false;
            if ((addr > 0 and addr < 24 ) || addr == 31)
                result = vistabus.write(keystring.c_str(), keystring.length(), addr, seq);
            if (result)
                ESP_LOGD(TAG, "Writing keys: %s to partition %li", keystring.c_str(), partition);
            else
                ESP_LOGE(TAG, "Failed to write keys: %s to partition %li. Send Queue Full.", keystring.c_str(), partition);
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
            //char b[4];
            //char *p;
            //itoa(n, b, 16);
            //long int li = strtol(b, &p, 10);
            return ((n >> 4) * 10) + (n & 0x0F);
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
#ifdef DEBUG_LOG
                ESP_LOGD(TAG, "zone match=%d", z);
#endif
                return z;
            }
            else
            {
#ifdef DEBUG_LOG
                ESP_LOGD(TAG, "No zone match");
#endif
            }
            return 0;
        }

        void vistaECPHome::printPacket(char cbuf[], int type, int src, int len)
        {
            char s1[4];
            std::string s = "";
            char s2[48];
            packetType source = static_cast<packetType>(src);
            char device[5];
            switch(source)
            {
                case unspecified:
                    sprintf(device, "EXT");
                    break;
                case chksum_fail:
                    sprintf(device, "CHK");
                    break;
                case expander:
                    sprintf(device, "EXP");
                    break;
                case rf_receiver:
                    sprintf(device, "RFR");
                    break;
                case aui:
                    sprintf(device, "AUI");
                    break;
                case keypad_ack:
                    sprintf(device, "KPA");
                    break;
                case keypad:
                    sprintf(device, "KPD");
                    break;
                case legacy_protocol:
                    sprintf(device, "KPDL");
                    break;
                case long_range_radio:
                    sprintf(device, "LRR");
                    break;
                default:
                    sprintf(device, "   ");               
            }
            struct timeval tv_now;
            gettimeofday(&tv_now, NULL);
            char time_str[16];
            struct tm timeinfo;
            localtime_r(&tv_now.tv_sec, &timeinfo);
            strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
            snprintf(time_str + strlen(time_str), sizeof(time_str) - strlen(time_str), ".%03ld", tv_now.tv_usec/1000);
            if (type == 0)
                sprintf(s2, "(PANEL-->%s) [%s]", device, time_str);
            else
                sprintf(s2, "(%s-->PANEL) [%s]", device, time_str);
            bool abbr = false;
#ifndef DEBUG_LOG
            if (len > 8)
            {
                len = 8;
                abbr = true;
            }
#endif
            for (int c = 0; c < len; c++)
            {
                sprintf(s1, "%02X ", cbuf[c]);
                s = s.append(s1);
            }
            if (abbr)
                s = s.append("...");
            if (source == chksum_fail)
                ESP_LOGE(TAG, "%s %s", s2, s.c_str());
            else
                ESP_LOGD(TAG, "%s %s", s2, s.c_str());
        }

        void vistaECPHome::set_alarm_state(std::string const &state, std::string code, int partition)
        {

            if (code.length() != 4 || !isInt(code, 10))
                code = accessCode; // ensure we get a numeric 4 digit code

            if (partition > 8 || partition < 1)
                return;
                
            uint8_t kpi = 0;
            for (const auto &it : known_partitions)
            {
                if (partition == it.partition)
                    break;
                kpi++;
            }

            uint8_t addr = 0;
            uint seq = 0;
            addr = known_partitions[kpi].assigned_keypad;
            seq = known_partitions[kpi].keypad_sequence;
            if (addr < 1 || addr > 23)
                return;

            // Arm stay
            if (state.compare("S") == 0 && !known_partitions[kpi].partition_state.previousLightState.armed)
            {
                if (quickArm)
                    vistabus.write("#3",2, addr, seq);
                else if (code.length() == 4)
                {
                    char send_str[5];
                    memcpy(send_str,code.c_str(),4);
                    memcpy(send_str+4,"3",1);
                    vistabus.write(send_str,5, addr, seq);
                }
            }
            // Arm away
            else if ((state.compare("A") == 0 || state.compare("W") == 0) && !known_partitions[kpi].partition_state.previousLightState.armed)
            {
                if (quickArm)
                    vistabus.write("#2",2, addr, seq);
                else if (code.length() == 4)
                {
                    char send_str[5];
                    memcpy(send_str,code.c_str(),4);
                    memcpy(send_str+4,"2",1);
                    vistabus.write(send_str,5, addr, seq);
                }
            }
            else if (state.compare("I") == 0 && !known_partitions[kpi].partition_state.previousLightState.armed)
            {
                if (quickArm)
                    vistabus.write("#7",2, addr, seq);
                else if (code.length() == 4)
                {
                    char send_str[5];
                    memcpy(send_str,code.c_str(),4);
                    memcpy(send_str+4,"7",1);
                    vistabus.write(send_str,5, addr, seq);
                }
            }
            else if (state.compare("N") == 0 && !known_partitions[kpi].partition_state.previousLightState.armed)
            {
                if (quickArm)
                    vistabus.write("#33",3, addr, seq);
                else if (code.length() == 4)
                {
                char send_str[6];
                memcpy(send_str,code.c_str(),4);
                memcpy(send_str+4,"33",2);
                vistabus.write(send_str,6, addr, seq);
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
                    memcpy(send_str+4,"6#",2);
                    vistabus.write(send_str,6, addr, seq);
                }
            }
            else if (state.compare("Y") == 0)
            {
                if (code.length() == 4)
                {
                    char send_str[7];
                    memcpy(send_str,code.c_str(),4);
                    memcpy(send_str+4,"600",3);
                    vistabus.write(send_str,7, addr, seq);
                }
            }
            else if (state.compare("D") == 0)
            {
                if (code.length() == 4)
                { // ensure we get 4 digit code
                    char send_str[5];
                    memcpy(send_str,code.c_str(),4);
                    memcpy(send_str+4,"1",1);
                    vistabus.write(send_str,5, addr, seq);
                }
            }
        }

        void vistaECPHome::update()
        {    
            if (!vistabus.connected() && esp_timer_get_time() - last_connection_check > 30*1000*1000)
            {
                ESP_LOGE(TAG, "Data timeout. Is the panel connected?");
                last_connection_check = esp_timer_get_time();
                return;
            }      
        }
    } //namespace
} // namespace
