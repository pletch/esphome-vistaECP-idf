/*
Functions in this file:
processReceiveQueue
processReceiveQueue_task_start
processF7
refreshStatusFlags
refreshLRRStatusFlags
refreshSensors
RF_handle_heartbeats
updateDisplayLines
AUIset_panel_time
AUIrequest_panel_time
AUIget_zone_faults
AUIprocess_zone_faults
*/

#include "vistaalarm.h"
    
namespace esphome 
{
    namespace alarm_panel 
    {
        void vistaECPHome::processReceiveQueue(void *args)
        {
            if (rfrEmulation[0])
            {
                for (auto &it : alarmZones) 
                {
                    if(it.rfnext_hb)
                    {
                        it.rfnext_hb = esp_timer_get_time() + 1*60*1000*1000 + (esp_random() % 10) * 60 * 1000 * 1000;
                    }
                }
            }
            vTaskDelay(250);
            while (1)
            {   
                uint64_t current_time = esp_timer_get_time();
                if (rfrEmulation[0])            
                    RF_handle_heartbeats();
                if (aui_request.pending && (current_time - aui_request.time > 6 * 1000 * 1000))
                    aui_request.pending = false;
                if (panel_clock.auto_sync && (current_time > panel_clock.next_sync))
                {
                    AUIrequest_panel_time();
                    panel_clock.next_sync += static_cast<uint64_t>(6)*60*60*1000*1000; //next sync in 6 hrs
                }

                char payload[48];
                int size;
                int type;
                int src;
                memset(payload,'\0',sizeof(payload));

                if (vistabus.read_packet(payload,size,type,src,true)) 
                {    
                    printPacket(payload, type, src, size);
                    if (type == 0) //yellow wire
                    {
                        if (src == 0xF7)
                        {
                            refreshStatusFlags(payload);
                            if (statusFlags.partition == 0) 
                                ESP_LOGW(TAG, "No keypad associated with this partition in YAML definition.");
                            else
                            {
                                processF7(payload);
                                refreshSensors();
                                ESP_LOGI(TAG, "Partition: %u", statusFlags.partition);
                            }
                            ESP_LOGI(TAG, "Prompt: %s", statusFlags.prompt1);
                            ESP_LOGI(TAG, "Prompt: %s", statusFlags.prompt2);
                            ESP_LOGI(TAG, "Beeps: %d", statusFlags.beeps);
                        }
                        else if (src == 0xF2)
                        {
                            if ((payload[7] & 0xF0) == 0x50)
                            {
                                uint8_t n = 8;
                                uint8_t sum = 0;
                                uint8_t data_len = 0;
                                while ((payload[n] == 0xFE || payload[n] == 0xEC || payload[n] == 0xF5) 
                                        && n < payload[1]+1)
                                {
                                    sum += 0xFF - payload[n];
                                    n++;
                                }
                                if (payload[n-1] == 0xEC)
                                {
                                    data_len = payload[1] - sum + 11;
                                }
                                else
                                {
                                    data_len = payload[1] - sum - 7;
                                }
                                char F2data[data_len+1];
                                memset(F2data,'\0',sizeof(F2data));
                                memcpy(F2data,&payload[size-data_len-1],data_len);
                                uint8_t target = 0;
                                switch (payload[2])
                                {
                                    case 0x40:
                                        target = 6;
                                        break;
                                    case 0x20:
                                        target = 5;
                                        break;
                                    case 0x04:
                                        target = 2;
                                        break;
                                    case 0x02:
                                        target = 1;
                                        break;
                                    default:
                                        break;
                                }
#ifdef DEBUG_LOG
                                char F2type[24];
                                memset(F2type,'\0',sizeof(F2type));
                                switch (sum)
                                {                            
                                    case 1:
                                        memcpy(F2type,"Partition",9);
                                        break;
                                    case 2: // Unsure..increases after entering program mode
                                        memcpy(F2type,"Program-Mode-Count",18);
                                        break;
                                    case 4:
                                        memcpy(F2type,"All-Zones-Clear",15);
                                        break;   
                                    case 20:  //There are other type 20 packets but we only print Time for now
                                        memcpy(F2type,"Panel-Time",10); 
                                        break;
                                    case 21:
                                        memcpy(F2type,"Panel-Info",10);
                                        break;
                                    case 22:
                                        memcpy(F2type,"Device/Zone-Info",16);
                                        break;
                                    case 23:
                                        memcpy(F2type,"Faulted-Zone(s)",15);
                                        break;
                                    default:
                                        memcpy(F2type,"Uncategorized",7);
                                        char num[5];
                                        memset(num,'\0',sizeof(num));
                                        sprintf(num," (%d)",sum);
                                        strncat(F2type,num,5);
                                        break; 
                                }
#endif
                                if (F2data[0] > 0x19 && F2data[0] < 0x80) //limit to standard ascii characters
                                {
                                    for (uint8_t i = 1; i < data_len; i++)
                                    {
                                        if(F2data[i] == 0)
                                            F2data[i] = 0x20;
                                    }
#ifdef DEBUG_LOG                     
                                    ESP_LOGI(TAG, " AUI Target:%d  Type:%s  Data:%s", target,F2type,F2data);
#endif
                                }
#ifdef DEBUG_LOG
                                else if (sum == 4)
                                {
                                    ESP_LOGI(TAG, " AUI Target:%d  Type:%s", target,F2type);
                                }
#endif
                                if (sum == 20 && target == aui_device.address)
                                {
                                    ESPTime panel_time;
                                    ESPTime rtc = now();
                                    if (rtc.is_valid())
                                    {    
                                        panel_time.year = 2000 + 10 * (F2data[0] - '0') + (F2data[1] - '0');
                                        panel_time.month = 10 * (F2data[2] - '0') + (F2data[3] - '0');
                                        panel_time.day_of_month = 10 * (F2data[4] - '0') + (F2data[5] - '0');
                                        panel_time.hour = 10 * (F2data[6] - '0') + (F2data[7] - '0');
                                        panel_time.minute = 10 * (F2data[8] - '0') + (F2data[9] - '0');
                                        panel_time.second = 10 * (F2data[10] - '0') + (F2data[11] - '0');
                                        panel_time.recalc_timestamp_local();
                                        int32_t delta = abs(rtc.timestamp - panel_time.timestamp);
                                        if (panel_clock.auto_sync && delta > 60)
                                            AUIset_panel_time();
#ifdef DEBUG_LOG
                                        char s[32];
                                        panel_time.strftime(s, 32,"%Y-%m-%d %H:%M:%S");
                                        ESP_LOGD(TAG,"Panel time is %s",s);
                                        rtc.strftime(s, 32,"%Y-%m-%d %H:%M:%S");
                                        ESP_LOGD(TAG,"RTC time is %s",s);
                                        ESP_LOGD(TAG, "Offset is %i seconds", delta);
#endif
                                    }
                                }

                                if (sum == 23 || sum == 4)
                                {
                                    AUIprocess_zone_faults(F2data);
                                    aui_request.pending = false;
                                }

                            }
                            else if ((payload[7] & 0xF0) == 0x60 && payload[8] == 0x63 && payload[1] == 0x16)
                            { 
#ifdef DEBUG_LOG
                                char targets[8];
                                memset(targets,'\0',sizeof(targets));
                                if (payload[2] & 0x02)
                                    memcpy(targets,"1",1);
                                if (payload[2] & 0x04)
                                    if (targets[0])
                                        strncat(targets,",2",3);
                                    else
                                        memcpy(targets,"2",1);
                                if (payload[2] & 0x20)
                                    if (targets[0])
                                        strncat(targets,",5",3);
                                    else
                                        memcpy(targets,"5",1);
                                if (payload[2] & 0x40)
                                    if (targets[0])
                                        strncat(targets,",6",3);
                                    else
                                        memcpy(targets,"6",1);
                                if(payload[22] == 0x06)
                                    ESP_LOGI(TAG, " AUI Target(s):%s  Type:Broadcast  Data:Zone-Fault", targets);
                                else if (payload[22] == 0x01)
                                    ESP_LOGI(TAG, " AUI Target(s):%s  Type:Broadcast  Data:Zone-Fault-Cleared", targets);
#endif
                                if (aui_device.address)
                                    AUIget_zone_faults();
                            }
                        }
                        else if (src == 0xF9)
                        {         
                            // we process all lrr messages with type 58
                            if (payload[3] == 0x58)
                            {
                                refreshLRRStatusFlags(payload);
                                int c = lrrstatusFlags.code;
                                int q = lrrstatusFlags.qual;
                                int z = lrrstatusFlags.data; //can be zone or user
                                int p = lrrstatusFlags.partition;

                                std::string qual;
                                char msg[100];
                                if (c < 400)
                                    qual = (q == 3) ? " is Cleared" : "";
                                else if (c == 570)
                                    qual = (q == 1) ? " is Active" : " is Cleared";
                                else
                                    qual = (q == 1) ? " is Restored" : "";
                                if (c)
                                {
                                    const char * lrrString = lrr_msg_lookup(c);
                                    std::string zn = std::to_string(z);
                                    std::string uf = "by user";
                                    if (lrrString[0] == 'Z') 
                                    {
                                        uf = "on";
                                    }
                                    if (p)
                                        snprintf(msg,100, "CID_%d%03d: %s %s %s%s, Partition %d", q,c, &lrrString[1], uf.c_str(), zn.c_str(), qual.c_str(),p);
                                    else
                                        snprintf(msg,100, "CID_%d%03d: %s %s %s%s", q,c, &lrrString[1], uf.c_str(), zn.c_str(), qual.c_str());

                                    if(text_sensors_common.lrr_messages != NULL)
                                        text_sensors_common.lrr_messages->process(msg);
                                }
                            }
                        }
                        else if (payload[0] != 0 && src == keypad_ack)
                        {
                            uint8_t kp_addr = (payload[0] & 0x0F) | 0x10;
                            for (auto &it : known_partitions)
                            {
                                if (kp_addr == it.assigned_keypad)
                                {
                                    it.keypad_sequence = it.keypad_sequence + 0x40;
                                    break;
                                }
                            }
                        }
                    }

                    if (type == 1) 
                    {
                        if (src == 0xFA || src == 0xFB)
                        {
                            //FD 09 31 00 30 open
                            //FD 09 31 00 20 closed
                            if (src = 0xFA && size == 4)
                            {
                                int z = payload[3] >> 5;
                                switch (payload[0])
                                {
                                    case 0x07:
                                        z += 8 + (payload[2] << 3);
                                        break;
                                    case 0x08:
                                        z += 16 + (payload[2] << 3);
                                        break;
                                    case 0x09:
                                        z += 24 + (payload[2] << 3);
                                        break;
                                    case 0x0A:
                                        z += 32 + (payload[2] << 3);
                                        break;
                                    case 0x0B:
                                        z += 40 + (payload[2] << 3);
                                        break;
                                }
                                bool open = (payload[3] >> 4) & 0x01;
                                zoneType *zt = getZone(z);
                                if (zt != NULL && zt->active)
                                {
                                    zt->open = open;
                                    zt->time = esp_timer_get_time();
                                    zoneStatusUpdate(zt);
                                }
                            }
                            else if (src = 0xFB && size == 7)
                            {
                                char rf_serial_char[14];
                                /* char rf_serial_char_out[20];
                                   FE 00 54 83 8f 89 a0 = Open / Active for door sensor.  
                                   FE 00 54 83 8f 89 80 = Closed / Inactive
                                   fe 00 51 85 f4 03 04 = heartbeat

                                   rf_serial_char
                                   1 - ? (loop flag?)
                                   2 - Low battery
                                   3 - Supervision required /heartbeat
                                   4 - ?
                                   5 -	Loop 3
                                   6 -	Loop 2
                                   7 -	Loop 4
                                   8 -	Loop 1  */
                                uint8_t chksum = 0;
                                for (int i = 0; i < RF_ZONE_MESSAGE_LENGTH-1; i++)
                                    chksum += payload[i];
                                chksum = ~(chksum) + 1;
                                if (chksum == payload[6])
                                {
                                    uint32_t device_serial = ((payload[2] & 0x0F) << 16) + (payload[3] << 8) + payload[4];
                                    snprintf(rf_serial_char, 14, "%lu%04lu", device_serial / 10000, device_serial % 10000);               
#ifdef DEBUG_LOG    
                                    ESP_LOGI(TAG, "  RFX: %s,%02x", rf_serial_char, payload[5]);
#endif
                                    if (!(payload[5] & 4) && !(payload[5] & 1))
                                    { // ignore heartbeat
                                        zoneType *zt = getRfSerialLookup(device_serial);
                                        if (zt != NULL)
                                        {
                                            int mask;
                                            switch (zt->rfloop)
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
                                            if (zt->active)
                                            {
                                                zt->time = esp_timer_get_time();
                                                zt->open = payload[5] & mask ? true : false;
                                                zt->rflowbat = payload[5] & 2 ? true : false; // low bat
                                                zoneStatusUpdate(zt);
                                            }
                                        }
                                    }
                                    if (text_sensors_common.rf_messages != NULL)
                                    {
                                        std::string s(rf_serial_char);
                                        text_sensors_common.rf_messages->process(s);
                                    }
                                }
                            }
                        }     
                    }
                }
            }
            vTaskDelete(NULL);
        }

        void vistaECPHome::processReceiveQueue_task_start(void *args)
        {
            vistaECPHome *tsk = static_cast<vistaECPHome *>(args);
            tsk->processReceiveQueue(args);
        }

        void vistaECPHome::refreshStatusFlags(char * cbuf) 
        {
            char prompt1[18];
            char prompt2[18];
            memcpy(prompt1,&cbuf[12], 16);
            memcpy(prompt2, &cbuf[28], 16);
            statusFlags.keypad[0] = cbuf[1]; // 0 to 7

            statusFlags.keypad[1] = cbuf[2]; // 8 to 15

            statusFlags.keypad[2] = cbuf[3]; // 16 - 23

            statusFlags.keypad[3] = cbuf[4]; // 24 - 31.

            statusFlags.partition = 0;

            for (const auto &it : known_partitions)
            {
                if (cbuf[(it.assigned_keypad >> 3) + 1] & (0x01 << (it.assigned_keypad & 0x07)))
                {
                    statusFlags.partition = it.partition;
                    break;
                }
            }

            statusFlags.zone = (int)toDec(cbuf[5]);

            statusFlags.beeps = cbuf[6] & BIT_MASK_BYTE1_BEEP;

            statusFlags.fire = ((cbuf[7] & BIT_MASK_BYTE2_FIRE) > 0);
            statusFlags.systemFlag = ((cbuf[7] & BIT_MASK_BYTE2_SYSTEM_FLAG) > 0);
            statusFlags.ready = ((cbuf[7] & BIT_MASK_BYTE2_READY) > 0);

            statusFlags.night = ((cbuf[6] & BIT_MASK_BYTE1_NIGHT) > 0);
            statusFlags.armedStay = ((cbuf[7] & BIT_MASK_BYTE2_ARMED_HOME) > 0);

            statusFlags.lowBattery = ((cbuf[7] & BIT_MASK_BYTE2_LOW_BAT) > 0);

            statusFlags.check = ((cbuf[7] & BIT_MASK_BYTE2_CHECK_FLAG) > 0);
            statusFlags.fireZone = ((cbuf[7] & BIT_MASK_BYTE2_ALARM_ZONE) > 0);

            statusFlags.inAlarm = ((cbuf[8] & BIT_MASK_BYTE3_IN_ALARM) > 0);
            statusFlags.acPower = ((cbuf[8] & BIT_MASK_BYTE3_AC_POWER) > 0);
            statusFlags.chime = ((cbuf[8] & BIT_MASK_BYTE3_CHIME_MODE) > 0);
            statusFlags.bypass = ((cbuf[8] & BIT_MASK_BYTE3_BYPASS) > 0);
            statusFlags.programMode = (cbuf[8] & BIT_MASK_BYTE3_PROGRAM);

            statusFlags.instant = ((cbuf[8] & BIT_MASK_BYTE3_INSTANT) > 0);
            statusFlags.armedAway = ((cbuf[8] & BIT_MASK_BYTE3_ARMED_AWAY) > 0);

            if (!statusFlags.systemFlag) 
                statusFlags.alarm = ((cbuf[8] & BIT_MASK_BYTE3_ZONE_ALARM) > 0);

            statusFlags.promptPos = cbuf[10];

            statusFlags.backlight = ((cbuf[12] & 0x80) > 0);
            cbuf[12] = (cbuf[12] & 0x7F);
            //cbuf[14] = 0xE1;
            //cbuf[36] = 0xEF;  //Extended ascii code to test with
            // Translate single extended ascii or unicode code point to multibyte UTF8
            // Not sure what encoding all the OUS panel options might use.  Possibly
            // some sort of custom mapping to extended codes?
            // As an example, byte EF is given from panel when it should be F6
            // for small o with diaeresis. Byte E1 is given when it should be
            // byte E4.
            char tempbuf[32];
            memset(tempbuf,0,32);
            memcpy(tempbuf,&cbuf[12],16);
            for (int i=1; i<32; i++)
            {
                if (!tempbuf[i])
                    break;
                if (tempbuf[i] > 0x7F) 
                {
                    tempbuf[i] = shift_extended_char(tempbuf[i]);
                    char buf[32];
                    memcpy(buf, &tempbuf[i+1],32 - i - 1);
                    tempbuf[i+1] = 0x80 | (tempbuf[i] & 0x3F);
                    tempbuf[i] = 0xC0 | (tempbuf[i] >> 6);
                    memcpy(&tempbuf[i+2], buf, 32 - i - 2);
                    i++;
                }
            }
            memcpy(statusFlags.prompt1, tempbuf, 32);
            memset(tempbuf,0,32);
            memcpy(tempbuf,&cbuf[28],16);
            for (int i=0; i<32; i++)
            {
                if (!tempbuf[i])
                    break;
                if (tempbuf[i] > 0x7F) 
                {
                    tempbuf[i] = shift_extended_char(tempbuf[i]);
                    char buf[32];
                    memcpy(buf, &tempbuf[i+1],32 - i - 1);
                    tempbuf[i+1] = 0x80 | (tempbuf[i] & 0x3F);
                    tempbuf[i] = 0xC0 | (tempbuf[i] >> 6);
                    memcpy(&tempbuf[i+2], buf, 32 - i - 2);
                    i++;
                }
            }
            memcpy(statusFlags.prompt2, tempbuf, 32);
        }

        void vistaECPHome::processF7(const char * cbuf)
        {
            uint8_t kpi = 0;
            bool forceRefresh = false;
            uint64_t current_time = esp_timer_get_time();
            if (current_time - last_refresh > 300*1000*1000) //force send at least every 5 minutes
            {
                forceRefresh = true;
                last_refresh = current_time;
            }
            for (const auto &it : known_partitions)
            {
                if (statusFlags.partition == it.partition)
                    break;
                kpi++;
            }

            if (statusFlags.partition == known_partitions[kpi].partition)
            {
                updateDisplayLines(statusFlags.partition);
                if (known_partitions[kpi].partition_state.lastbeeps != statusFlags.beeps && text_sensors_partition[known_partitions[kpi].partition - 1].beeps != NULL)
                        text_sensors_partition[known_partitions[kpi].partition - 1].beeps->process(std::to_string(statusFlags.beeps));
                known_partitions[kpi].partition_state.lastbeeps = statusFlags.beeps;
                if (statusFlags.systemFlag && strstr(statusFlags.prompt2, HITSTAR))
                    alarm_keypress_partition("*", known_partitions[kpi].partition);
            }

            currentSystemState = sunavailable;
            currentLightState.stay = false;
            currentLightState.away = false;
            currentLightState.night = false;
            currentLightState.instant = false;
            currentLightState.ready = false;
            currentLightState.alarm = false;
            currentLightState.armed = false;
            currentLightState.ac = true;
            currentLightState.fire = false;
            currentLightState.check = false;
            currentLightState.trouble = false;
            currentLightState.bypass = false;
            currentLightState.chime = false;
            bool updateSystemState = false;

            // Publishes ready status
            if (statusFlags.ready)
            {
                currentSystemState = sdisarmed;
                currentLightState.ready = true;
                updateSystemState = true;
            }

            // armed status lights
            if (statusFlags.armedAway || statusFlags.armedStay)
            {
                updateSystemState = true;
                if (statusFlags.night)
                {
                    currentSystemState = sarmednight;
                    currentLightState.night = true;
                    currentLightState.stay = true;
                }
                else if (statusFlags.armedAway)
                {
                    currentSystemState = sarmedaway;
                    currentLightState.away = true;
                }
                else
                {
                    currentSystemState = sarmedstay;
                    currentLightState.stay = true;
                }
                currentLightState.armed = true;
            }

            // device check status
            if (statusFlags.check)
            {
                updateSystemState = true; // we also get system flags when a device has a check flag
                zoneType *zt = getZone(statusFlags.zone);
                if (zt != NULL)
                {
#ifdef DEBUG_LOG
                    ESP_LOGD(TAG, "check found for zone %d,status=%d", statusFlags.zone, zt->check);
#endif
                    if (!zt->check && zt->active)
                    {
                        zt->check = true;
                        zt->open = false;
                        zt->alarm = false;
                        currentLightState.trouble = true;
                        zoneStatusUpdate(zt);
                    }
                    if (!zt->partition && zt->active)
                        zt->partition = known_partitions[kpi].partition;
                    zt->time = current_time;
                }
            }

            // zone fault status
            if (!statusFlags.systemFlag && !statusFlags.check && !statusFlags.bypass && !statusFlags.alarm && !statusFlags.programMode &&
                !(statusFlags.instant || statusFlags.armedAway || statusFlags.armedStay || statusFlags.night))
            {
                zoneType *zt = getZone(statusFlags.zone);
                if (zt != NULL)
                {
#ifdef DEBUG_LOG
                    ESP_LOGD(TAG, "fault found for zone %d", statusFlags.zone);
#endif
                    if (!zt->open && zt->active)
                    {
                        zt->open = true;
                        zt->check = false;
                        zt->bypass = false;
                        zoneStatusUpdate(zt);                        
                    }
                    zt->time = current_time;
                }
            }

            // zone bypass status
            if (!statusFlags.systemFlag && !statusFlags.check && statusFlags.bypass && !statusFlags.alarm && 
                !(statusFlags.instant || statusFlags.armedAway || statusFlags.armedStay || statusFlags.night))
            {
                zoneType *zt = getZone(statusFlags.zone);
                if (zt != NULL)
                {
                    if (!zt->bypass && zt->active)
                    {
                        zt->bypass = true;
                        zoneStatusUpdate(zt);
                    }
                    zt->time = current_time;
                }
            }

            // trouble lights
            if (!statusFlags.acPower)
            {
                currentLightState.ac = false;
            }
            
            if ( statusFlags.lowBattery && (statusFlags.systemFlag || statusFlags.check))
            {
                currentLightState.bat = true;
                lowBatteryTime = current_time;
            }

            if (statusFlags.fire)
            {
                currentLightState.fire = true;
                currentSystemState = striggered;
            }

            if (statusFlags.inAlarm)
                currentSystemState = striggered;

            if (statusFlags.chime)
                currentLightState.chime = true;

            if (statusFlags.bypass)
                currentLightState.bypass = true;

            if (statusFlags.check)
                currentLightState.check = true;

            if (statusFlags.instant)
                currentLightState.instant = true;

            if (!currentLightState.ac || currentLightState.bat || statusFlags.check)
                currentLightState.trouble = true;

            if (statusFlags.alarm || statusFlags.inAlarm)
                currentLightState.alarm = true;

            if (statusFlags.partition == known_partitions[kpi].partition && (statusFlags.systemFlag || updateSystemState))
            {
                if ((currentSystemState != known_partitions[kpi].partition_state.previousSystemState) && 
                            text_sensors_partition[known_partitions[kpi].partition-1].system_status != NULL)
                switch (currentSystemState)
                { 
                    case striggered:
                        text_sensors_partition[known_partitions[kpi].partition-1].system_status->process(STATUS_TRIGGERED);
                        break;
                    case sarmedaway:
                        text_sensors_partition[known_partitions[kpi].partition-1].system_status->process(STATUS_ARMED);
                        break;
                    case sarmednight:
                        text_sensors_partition[known_partitions[kpi].partition-1].system_status->process(STATUS_NIGHT);
                        break;
                    case sarmedstay:
                        text_sensors_partition[known_partitions[kpi].partition-1].system_status->process(STATUS_STAY);
                        break;
                    case sunavailable:
                        text_sensors_partition[known_partitions[kpi].partition-1].system_status->process(STATUS_NOT_READY);
                        break;
                    case sdisarmed:
                        text_sensors_partition[known_partitions[kpi].partition-1].system_status->process(STATUS_OFF);
                        break;
                    default:
                        text_sensors_partition[known_partitions[kpi].partition-1].system_status->process(STATUS_NOT_READY);
                        break;
                }
                known_partitions[kpi].partition_state.previousSystemState = currentSystemState;
                known_partitions[kpi].partition_state.refreshStatus = false;
            }

            if (statusFlags.partition == known_partitions[kpi].partition )
            {
                previousLightState = known_partitions[kpi].partition_state.previousLightState;

                if ((currentLightState.fire != previousLightState.fire || forceRefresh) && status_sensors_partition[known_partitions[kpi].partition - 1].fire != NULL)
                    status_sensors_partition[known_partitions[kpi].partition - 1].fire->process(currentLightState.fire);
                if ((currentLightState.alarm != previousLightState.alarm || forceRefresh) && status_sensors_partition[known_partitions[kpi].partition - 1].alm != NULL)
                    status_sensors_partition[known_partitions[kpi].partition - 1].alm->process(currentLightState.alarm);
                if ((currentLightState.trouble != previousLightState.trouble || forceRefresh) && status_sensors_partition[known_partitions[kpi].partition - 1].trbl != NULL)
                    status_sensors_partition[known_partitions[kpi].partition - 1].trbl->process(currentLightState.trouble);
                if ((currentLightState.chime != previousLightState.chime || forceRefresh) && status_sensors_partition[known_partitions[kpi].partition - 1].chm != NULL)
                    status_sensors_partition[known_partitions[kpi].partition - 1].chm->process(currentLightState.chime);
                if (statusFlags.systemFlag || updateSystemState)
                {
                    if ((currentLightState.away != previousLightState.away || forceRefresh) && status_sensors_partition[known_partitions[kpi].partition - 1].arma != NULL)
                        status_sensors_partition[known_partitions[kpi].partition - 1].arma->process(currentLightState.away);
                    if ((currentLightState.stay != previousLightState.stay || forceRefresh) && status_sensors_partition[known_partitions[kpi].partition -1 ].arms != NULL)
                        status_sensors_partition[known_partitions[kpi].partition - 1].arms->process(currentLightState.stay);
                    if ((currentLightState.night != previousLightState.night || forceRefresh) && status_sensors_partition[known_partitions[kpi].partition - 1].armn != NULL)
                        status_sensors_partition[known_partitions[kpi].partition - 1].armn->process(currentLightState.night);
                    if ((currentLightState.instant != previousLightState.instant || forceRefresh) && status_sensors_partition[known_partitions[kpi].partition - 1].armi != NULL)
                        status_sensors_partition[known_partitions[kpi].partition - 1].armi->process(currentLightState.instant);
                    if ((currentLightState.armed != previousLightState.armed || forceRefresh) && status_sensors_partition[known_partitions[kpi].partition - 1].arm != NULL)
                        status_sensors_partition[known_partitions[kpi].partition - 1].arm->process(currentLightState.armed);
                }
                if ((currentLightState.bypass != previousLightState.bypass || forceRefresh) && status_sensors_partition[known_partitions[kpi].partition - 1].byp != NULL)
                    status_sensors_partition[known_partitions[kpi].partition - 1].byp->process(currentLightState.bypass);
                if ((currentLightState.ready != previousLightState.ready || forceRefresh) && status_sensors_partition[known_partitions[kpi].partition - 1].rdy != NULL)
                    status_sensors_partition[known_partitions[kpi].partition - 1].rdy->process(currentLightState.ready);
                if ((currentLightState.bat != previousLightState.bat || forceRefresh) && ac_bin_sensor != NULL)
                    ac_bin_sensor->process(currentLightState.ac);
                if ((currentLightState.bat != previousLightState.bat || forceRefresh) && bat_bin_sensor != NULL)
                    bat_bin_sensor->process(currentLightState.bat);
                known_partitions[kpi].partition_state.previousLightState = currentLightState;
            }
        }

        void vistaECPHome::refreshSensors()
        {
            std::string zoneStatusMsg = "";
            char s1[16];
            uint64_t current_time = esp_timer_get_time();
            // clears restored zones after timeout
            for (auto &x : alarmZones)
            {

                if (!x.active || !x.partition)
                    continue;

                uint8_t kpi = 0;
                for (const auto &it : known_partitions)
                {
                    if (x.partition == it.partition)
                        break;
                    kpi++;
                }

                if (kpi == known_partitions.size())
                    continue;

                if (!x.bypass && x.open && known_partitions[kpi].partition_state.previousLightState.ready)
                {
                    x.open = false;
                    x.check = false;
                    x.alarm = false;
                }

                if (x.bypass && !known_partitions[kpi].partition_state.previousLightState.bypass)
                {
                    x.bypass = false;
                }

                if (x.alarm && !known_partitions[kpi].partition_state.previousLightState.alarm)
                {
                    x.alarm = false;
                }

                if (!x.bypass && x.open && (current_time - x.time) > TTL)
                {
                    x.open = false;
                }

                if (!x.bypass && x.check && (current_time - x.time) > TTL)
                {
                    x.check = false;
                }

                zoneStatusUpdate(&x);

                // Construct zone status msg
                if (x.open)
                {
                    if (zoneStatusMsg != "")
                        sprintf(s1, ",OP:%d", x.zone);
                    else
                        sprintf(s1, "OP:%d", x.zone);
                    zoneStatusMsg.append(s1);
                }

                if (x.alarm)
                { 
                    if (zoneStatusMsg != "")
                        sprintf(s1, ",AL:%d", x.zone);
                    else
                        sprintf(s1, "AL:%d", x.zone);
                    zoneStatusMsg.append(s1);
                }

                if (x.bypass)
                {
                    if (zoneStatusMsg != "")
                        sprintf(s1, ",BY:%d", x.zone);
                    else
                        sprintf(s1, "BY:%d", x.zone);
                    zoneStatusMsg.append(s1);
                }

                if (x.check)
                {
                    if (zoneStatusMsg != "")
                        sprintf(s1, ",CK:%d", x.zone);
                    else
                        sprintf(s1, "CK:%d", x.zone);
                    zoneStatusMsg.append(s1);
                }
                    
                if (x.rflowbat)
                { // low rf battery
                    if (zoneStatusMsg != "")
                        sprintf(s1, ",LB:%d", x.zone);
                    else
                        sprintf(s1, "LB:%d", x.zone);
                    zoneStatusMsg.append(s1);
                }
            }

            if ((current_time - lowBatteryTime) > TTL)
                currentLightState.bat = false;

            if ((zoneStatusMsg != previousZoneStatusMsg) && text_sensors_common.zone_status != NULL)
                text_sensors_common.zone_status->process(zoneStatusMsg);

            previousZoneStatusMsg = zoneStatusMsg;            
        }

        void vistaECPHome::refreshLRRStatusFlags(char * cbuf)
        {
            if (cbuf[2] == 0)
                return;

            if (cbuf[3] == 0x58)
            {
                int c = (((0x0f & cbuf[8]) << 8) | cbuf[9]);
                c = toDec(c); // convert to decimal representation for correct code display
                lrrstatusFlags.qual = (uint8_t)(0xf0 & cbuf[8]) >> 4;
                lrrstatusFlags.code = c;
                lrrstatusFlags.data = toDec(((uint8_t)cbuf[12] >> 4) | ((uint8_t)cbuf[11] << 4));
                lrrstatusFlags.partition = (uint8_t)cbuf[10];
            }
        }   

        void vistaECPHome::RF_handle_heartbeats()
        {  
            for (auto &it : alarmZones) 
            {
                if(it.rfnext_hb && (esp_timer_get_time() > it.rfnext_hb))
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
                uint8_t msg = 0x80 ^ mask & 0x04;
                vistabus.sendRFmsg(it.rfserial,msg);
                it.rfnext_hb = esp_timer_get_time() + 70ULL*60*1000*1000 + (esp_random() % 20) * 60 * 1000 * 1000;
                }
            }
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

        void vistaECPHome::AUIrequest_panel_time()
        {
            if (statusFlags.programMode || !aui_device.address )
                return;
            ESP_LOGD(TAG, "Requesting AUI time from panel...");
            char bytes[6] = {0,0, 0x05, 0x02, 0x43, 0x43};
            aui_device.sequence2 = aui_device.sequence2 == 0x6F ? 0x68 : aui_device.sequence2 + 1;
            bytes[1] = aui_device.sequence2;
            vistabus.writedirect(bytes, 6, aui_device.address, aui_device.sequence1);
            aui_device.sequence1 += 0x40;
            return;
        }

        void vistaECPHome::AUIset_panel_time()
        {
            ESPTime rtc = now();
            if (!rtc.is_valid() || statusFlags.programMode || !aui_device.address )
                return;
            ESP_LOGD(TAG, "Setting AUI time...");
            char bytes[22] = {0,0, 0x05, 0x02, 0x45, 0x43, 0xF5, 0xEC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            aui_device.sequence2 = aui_device.sequence2 == 0x6F ? 0x68 : aui_device.sequence2 + 1;
            bytes[1] = aui_device.sequence2;

            snprintf(&bytes[8], 14, "%02d%02d%02d%02d%02d%02d%d", rtc.year % 100, rtc.month, rtc.day_of_month, rtc.hour, rtc.minute, 
                rtc.second, rtc.day_of_week-1);
            vistabus.writedirect(bytes, 21, aui_device.address, aui_device.sequence1);
            aui_device.sequence1 += 0x40;
            return;
        }

        void vistaECPHome::AUIget_zone_faults()
        {
            if ( !aui_device.address || aui_request.pending)
                return;
            char bytes[22] = {0,0, 0x62, 0x31, 0x45, 0x49, 0xF5, 0x31, 0xFB, 0x45, 0x4A, 0xF5, 0x32, 0xFB, 0x45, 0x43, 0xF5, 0x31, 0xFB, 0x43, 0x6C};
            aui_device.sequence2 = aui_device.sequence2 == 0x6F ? 0x68 : aui_device.sequence2 + 1;
            bytes[1] = aui_device.sequence2;
            vistabus.writedirect(bytes, 21, aui_device.address, aui_device.sequence1);
            aui_device.sequence1 += 0x40;
            aui_request.pending = true;
            aui_request.time = esp_timer_get_time();
            return;
        }

        void vistaECPHome::AUIprocess_zone_faults(char *list)
        {
            uint32_t zonestatusmask1to32 = 0;
            uint32_t zonestatusmask33to64 = 0;
            uint32_t zonestatusmask65to96 = 0;
            uint32_t zonestatusmask97to128 = 0;
            if (list[0] != 0xFE)
            {
                int data_len = strlen(list);
                // Search all occurrences of integers or ranges
                size_t pos;
                char buf[5], buf1[5];
                char zones[16];
                memset(zones,'\0',sizeof(zones));
                uint8_t zone_ct = 0;
                bool prev_digit = false;
                bool range = false;
                for (uint8_t i = 0; i < data_len; i++)
                {
                    if (list[i] >= 0x30 && list[i] <= 0x39)
                    {
                        if (prev_digit)
                        {
                            zones[zone_ct] = zones[zone_ct] * 10;
                            zones[zone_ct] += (list[i] - '0');
                            prev_digit = false;
                        }
                        else
                        {
                            zones[zone_ct] = (list[i] - '0');
                            prev_digit = true;
                        }
                    }
                    else if (list[i] == 0x2D) //range
                    {
                        zone_ct++;
                        range = true;
                        prev_digit = false;
                    }
                    if (list[i] == 0x20 || i == (data_len-1)) //separator or end of list
                    {
                        if (range)
                        {
                            uint8_t start = zones[zone_ct-1]+1;
                            uint8_t end = zones[zone_ct];
                            for (uint8_t k = start; k <= end; k++)
                            {
                                zones[zone_ct] = k;
                                zone_ct++;
                            }
                            range = false;
                        }
                        else
                            zone_ct++;
                        prev_digit = false;
                    }           
                }   

                for (uint8_t i = 0; i < strlen(zones); i++)
                {
                    if (zones[i] <= 32)
                        zonestatusmask1to32 = zonestatusmask1to32 | (1 << (zones[i]-1));
                    else if (zones[i] > 32 && zones[i] <= 64)
                        zonestatusmask33to64 = zonestatusmask33to64 | (1 << (zones[i]-32-1));
                    else if (zones[i] > 64 && zones[i] <= 96)
                        zonestatusmask65to96 = zonestatusmask65to96 | (1 << (zones[i]-64-1));
                    else if (zones[i] > 96 && zones[i] <= 128)
                        zonestatusmask97to128 = zonestatusmask97to128 | (1 << (zones[i]-96-1));
                    else
                        return;
                }
            }
            for (auto &it: alarmZones)
            {
                if (it.zone <= 32)
                {
                    if (it.open != static_cast<bool>((zonestatusmask1to32 >> (it.zone - 1)) & 0x01))
                    {
                        it.open = (zonestatusmask1to32 >> (it.zone - 1)) & 0x01;
                        if (it.open)
                            it.time = esp_timer_get_time();
                        zoneStatusUpdate(&it);
                    }   
                }
                else if (it.zone > 32 && it.zone <= 64)
                {
                    if (it.open != static_cast<bool>((zonestatusmask33to64 >> (it.zone - 32 - 1)) & 0x01))
                    {
                        it.open = (zonestatusmask33to64 >> (it.zone - 1)) & 0x01;
                        if (it.open)
                            it.time = esp_timer_get_time();
                        zoneStatusUpdate(&it);
                    }
                }
                else if (it.zone > 64 && it.zone <= 96)
                {
                    if (it.open != static_cast<bool>((zonestatusmask65to96 >> (it.zone - 64 - 1)) & 0x01))
                    {
                        it.open = (zonestatusmask65to96 >> (it.zone - 1)) & 0x01;
                        if (it.open)
                            it.time = esp_timer_get_time();
                        zoneStatusUpdate(&it);
                    }
                }
                else if (it.zone > 96 && it.zone <= 128)
                {
                    if (it.open != static_cast<bool>((zonestatusmask97to128 >> (it.zone - 96 - 1)) & 0x01))
                    {
                        it.open = (zonestatusmask97to128 >> (it.zone - 1)) & 0x01;
                        if (it.open)
                            it.time = esp_timer_get_time();
                        zoneStatusUpdate(&it);
                    }
                }
            }
        }
    } //namespace
} //namespace