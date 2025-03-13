/*
Functions in this file:
processReceiveQueue
processReceiveQueue_task_start
refreshStatusFlags
refreshLRRStatusFlags
updateDisplayLines
*/

#include "vistaalarm.h"
    
namespace esphome 
{
    namespace alarm_panel 
    {
        void vistaECPHome::processReceiveQueue(void *args)
        {
            forceRefreshGlobal = true;
            while (1)
            {               
                AUIprocessQueue();
                char payload[48];
                int size;
                int type;
                memset(payload,'\0',sizeof(payload));
                bool F7_no_change = true;

                if (vistabus.read_packet(payload,size,type, true)) 
                {
                    if (debug > 0 && type == 0)
                    {
                        if (payload[0] == 0xF7 || payload[0] == 0xF8)
                            printPacket("CMD", payload, 13);
                        else
                            printPacket("CMD", payload, size);
                    }

                    if (debug > 0 && type == 1)
                    {
                        printPacket("EXT", payload, size);
                    }

                    if (type == 0) //yellow wire
                    {
                        if (payload[0]==0xF7)
                        {
                            F7_no_change = areEqual(payload, last_F7, size);
            
                            if (!F7_no_change)
                            {
                                memcpy(last_F7,payload,size);
                                refreshStatusFlags(payload, &statusFlags);
                                forceRefreshGlobal = true;
                            }

                            if ((esp_timer_get_time() - last_refresh) > 300*1000*1000)
                            {
                                forceRefreshGlobal = true;
                            }
                            ESP_LOGI(TAG,"Raw char  %02X %02X %02X", statusFlags.prompt1[0], statusFlags.prompt1[1], statusFlags.prompt1[2]);
                            ESP_LOGI(TAG, "Prompt: %s", statusFlags.prompt1);
                            ESP_LOGI(TAG, "Prompt: %s", statusFlags.prompt2);
                            ESP_LOGI(TAG, "Beeps: %d", statusFlags.beeps);
                        //forceRefreshZones = true;
                        }
                        else if (payload[0]==0xF2)
                        {
                            if (auiAddr)
                            AUIprocessF2(payload);
                        }
                        else if (payload[0] == 0xF9)
                        {         
                            // we process all lrr messages with type 58
                            if (payload[3] == 0x58)
                            {
                                refreshLRRStatusFlags(payload, &lrrstatusFlags);
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
                                        //zn=getZoneName(z);
                                        //zn = "null"; //fix this later
                                    }

                                    snprintf(msg,100, "CID_%d%03d: %s %s %s%s, Partition %d", q,c, &lrrString[1], uf.c_str(), zn.c_str(), qual.c_str(),p);

                                    if(text_sensors_common.lrr_messages != NULL)
                                        text_sensors_common.lrr_messages->process(msg);

                                    //refreshLrrTime = esp_timer_get_time();
                                }
                            }
                        }
                    }
                    if (type == 1) 
                    {
                        if (payload[1] != 0 && (payload[0] == 0x7F || payload[0] == 0xFE || payload[0] == 0xFD || 
                                payload[0] == 0xFB || payload[0] == 0xF7)) //Expander board 
                        {
                            //FD 09 31 00 30 open
                            //FD 09 31 00 20 closed
                            int z = payload[4] >> 5;
                            switch (payload[1])
                            {
                                case 0x07:
                                    z += 8 + (payload[3] << 3);
                                    break;
                                case 0x08:
                                    z += 16 + (payload[3] << 3);
                                    break;
                                case 0x09:
                                    z += 24 + (payload[3] << 3);
                                    break;
                                case 0x0A:
                                    z += 32 + (payload[3] << 3);
                                    break;
                                case 0x0B:
                                    z += 40 + (payload[3] << 3);
                                    break;
                            }
                            bool open = (payload[4] >> 4) & 0x01;
                            zoneType *zt = getZone(z);
                            if (zt != NULL && zt->active)
                            {
                                zt->open = open;
                                zoneStatusUpdate(zt);
                            }
                        }     
                        else if (payload[0] == 0xFE && payload[1] == 0 && size == 8) // Honeywell 5881 uses address of 0 on Vista 15/20
                        {
                            char rf_serial_char[14];
                            //char rf_serial_char_out[20];
                            // FE 00 54 83 8f 89 a0 = Open / Active for door sensor.  
                            // FE 00 54 83 8f 89 80 = Closed / Inactive
                            // fe 00 51 85 f4 03 04 = heartbeat
                            uint8_t chksum = 0;
                            for (int i = 2; i < 7; i++)
                                chksum += payload[i];
                            chksum = ~(chksum) + 1;
                            if (chksum == payload[7])
                            {
                                uint32_t device_serial = ((payload[3] & 0xF) << 16) + (payload[4] << 8) + payload[5];
                                snprintf(rf_serial_char, 14, "%3lu%04lu", device_serial / 10000, device_serial % 10000);
                            
                                if (debug > 0)
                                    ESP_LOGI(TAG, "RFX: %s,%02x", rf_serial_char, payload[6]);
                                if (!(payload[6] & 4) && !(payload[6] & 1))
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
                                            zt->open = payload[6] & mask ? true : false;
                                            zt->rflowbat = payload[6] & 2 ? true : false; // low bat
                                            //ESP_LOGD(TAG, "set rf low bat to %d", zt->rflowbat);
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
                        /* rf_serial_char

                        1 - ? (loop flag?)
                        2 - Low battery
                        3 -	Supervision required /heartbeat
                        4 - ?
                        5 -	Loop 3
                        6 -	Loop 2
                        7 -	Loop 4
                        8 -	Loop 1  */
                    }
                }
                // done other cmd processing.    Process f7 now
                if (!forceRefreshGlobal)
                    continue;
        
                last_refresh = esp_timer_get_time();
                getPartitionsFromMask();
                for (uint8_t partition = 1; partition <= maxPartitions; partition++)
                {
                    if (partitions[partition - 1])
                    {
                        forceRefresh = partitionStates[partition - 1].refreshStatus || forceRefreshGlobal;
                        ESP_LOGI(TAG, "Partition: %02X", partition);

                        updateDisplayLines(partition);
                        if (partitionStates[partition - 1].lastbeeps != (statusFlags.beeps || forceRefresh) && text_sensors_partition[partition-1].beeps != NULL)
                        {
                            text_sensors_partition[partition-1].beeps->process(std::to_string(statusFlags.beeps));
                        }

                        partitionStates[partition - 1].lastbeeps = statusFlags.beeps;

                        if (statusFlags.systemFlag && strstr(statusFlags.prompt2, HITSTAR))
                            alarm_keypress_partition("*", partition);
                    }
                    //forceRefreshZones = true;
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
                // zone fire status
                // int tz;
                if (!statusFlags.systemFlag && !statusFlags.check && statusFlags.fireZone)
                {
                    if (payload[5] > 0x90)
                        getZoneFromPrompt(statusFlags.prompt1);
                    fireStatus.zone = statusFlags.zone;
                    fireStatus.time = esp_timer_get_time();
                    fireStatus.state = true;
                    zoneType *zt = getZone(statusFlags.zone);
                    if (zt != NULL)
                        zt->fire = true;
                }
                // zone alarm status
                if (!statusFlags.systemFlag && !statusFlags.check && statusFlags.alarm)
                {
                    if (payload[5] > 0x90)
                        getZoneFromPrompt(statusFlags.prompt1);
                    zoneType *zt = getZone(statusFlags.zone);
                    if (zt != NULL)
                    {
                    ESP_LOGD(TAG,"alarm found for zone %d,status=%d",statusFlags.zone,zt->alarm );
                    if (!zt->alarm && zt->active)
                        {
                            zt->alarm = true;
                            zoneStatusUpdate(zt);
                        }
                        if (!zt->partition && zt->active)
                            assignPartitionToZone(zt);
                        zt->time = esp_timer_get_time();
                        alarmStatus.zone = statusFlags.zone;
                        alarmStatus.time = zt->time;
                        alarmStatus.state = true;
                    }
                }
                // device check status
                if (statusFlags.check)
                {
                    updateSystemState = true; // we also get system flags when a device has a check flag
                    if (payload[5] > 0x90)
                        getZoneFromPrompt(statusFlags.prompt1);
                    zoneType *zt = getZone(statusFlags.zone);
                    if (zt != NULL)
                    {
                        ESP_LOGD(TAG, "check found for zone %d,status=%d", statusFlags.zone, zt->check);
                        if (!zt->check && zt->active)
                        {
                            zt->check = true;
                            zt->open = false;
                            zt->alarm = false;
                            currentLightState.trouble = true;
                            zoneStatusUpdate(zt);
                        }
                        if (!zt->partition && zt->active)
                            assignPartitionToZone(zt);
                        zt->time = esp_timer_get_time();
                    }
                }
                // zone fault status

                if (!statusFlags.systemFlag && !statusFlags.check && !statusFlags.bypass && !statusFlags.alarm && 
                    !(statusFlags.instant || statusFlags.armedAway || statusFlags.armedStay || statusFlags.night))
                {
                    if (payload[5] > 0x90)
                    {
                        getZoneFromPrompt(statusFlags.prompt1);
                    }

                    zoneType *zt = getZone(statusFlags.zone);
                    if (zt != NULL)
                    {
                        if (!zt->open && zt->active)
                        {
                            zt->open = true;
                            zt->check = false;
                            zt->bypass = false;
                            zoneStatusUpdate(zt);
                        }
                        zt->time = esp_timer_get_time();
                    }
                }
                // zone bypass status
                if (!statusFlags.systemFlag && !statusFlags.check && statusFlags.bypass && !statusFlags.alarm && 
                    !(statusFlags.instant || statusFlags.armedAway || statusFlags.armedStay || statusFlags.night))
                {
                    if (payload[5] > 0x90)
                        getZoneFromPrompt(statusFlags.prompt1);

                    zoneType *zt = getZone(statusFlags.zone);
                    if (zt != NULL)
                    {

                        if (!zt->bypass && zt->active)
                        {
                            zt->bypass = true;
                            zoneStatusUpdate(zt);
                        }
                        zt->time = esp_timer_get_time();
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
                    lowBatteryTime = esp_timer_get_time();
                }

                if (statusFlags.fire)
                {
                    currentLightState.fire = true;
                    currentSystemState = striggered;
                }

                if (statusFlags.inAlarm)
                {
                    currentSystemState = striggered;
                    alarmStatus.zone = 99;
                    alarmStatus.time = esp_timer_get_time();
                    alarmStatus.state = true;
                }

                if (statusFlags.chime)
                {
                    currentLightState.chime = true;
                }

                if (statusFlags.bypass)
                {
                    currentLightState.bypass = true;
                }

                if (statusFlags.check)
                {
                    currentLightState.check = true;
                }
                if (statusFlags.instant)
                {
                    currentLightState.instant = true;
                }
                uint64_t chkTime = esp_timer_get_time();
                // clear alarm statuses    when timer expires
                if ((chkTime - fireStatus.time) > TTL)
                {
                    fireStatus.state = false;
                    if (fireStatus.zone > 0 && fireStatus.zone <= maxZones)
                    {
                        zoneType *zt = getZone(fireStatus.zone);
                        if (zt != NULL)
                            zt->fire = false;
                    }
                }
                if ((chkTime - alarmStatus.time) > TTL)
                {
                    alarmStatus.state = false;
                    if (alarmStatus.zone > 0 && alarmStatus.zone <= maxZones)
                    {
                        zoneType *zt = getZone(alarmStatus.zone);
                        if (zt != NULL)
                            zt->alarm = false;
                    }
                }
                if ((chkTime - panicStatus.time) > TTL)
                {
                    panicStatus.state = false;
                    if (panicStatus.zone > 0 && panicStatus.zone <= maxZones)
                    {
                        zoneType *zt = getZone(panicStatus.zone);
                        if (zt != NULL)
                            zt->panic = false;
                    }
                }
                if ((chkTime - lowBatteryTime) > TTL)
                    currentLightState.bat = false;

                if (!currentLightState.ac || currentLightState.bat || statusFlags.check)
                    currentLightState.trouble = true;

                currentLightState.alarm = alarmStatus.state;

                for (uint8_t partition = 1; partition <= maxPartitions; partition++)
                {
                    if ((partitions[partition - 1] && partitionTargets == 1) && (statusFlags.systemFlag || updateSystemState))
                    {
                    // system status message
                        forceRefresh = partitionStates[partition - 1].refreshStatus || forceRefreshGlobal;

                        if ((currentSystemState != partitionStates[partition - 1].previousSystemState || forceRefresh) && text_sensors_partition[partition-1].system_status != NULL)
                        switch (currentSystemState)
                        { 
                            case striggered:
                                text_sensors_partition[partition-1].system_status->process(STATUS_TRIGGERED);
                                break;
                            case sarmedaway:
                                text_sensors_partition[partition-1].system_status->process(STATUS_ARMED);
                                break;
                            case sarmednight:
                                text_sensors_partition[partition-1].system_status->process(STATUS_NIGHT);
                                break;
                            case sarmedstay:
                                text_sensors_partition[partition-1].system_status->process(STATUS_STAY);
                                break;
                            case sunavailable:
                                text_sensors_partition[partition-1].system_status->process(STATUS_NOT_READY);
                                break;
                            case sdisarmed:
                                text_sensors_partition[partition-1].system_status->process(STATUS_OFF);
                                break;
                            default:
                                text_sensors_partition[partition-1].system_status->process(STATUS_NOT_READY);
                                break;
                        }
                        partitionStates[partition - 1].previousSystemState = currentSystemState;
                        partitionStates[partition - 1].refreshStatus = false;
                    }
                }

                for (uint8_t partition = 1; partition <= maxPartitions; partition++)
                {
                    if ((partitions[partition - 1] && partitionTargets == 1))
                    {
                        // publish status on change only - keeps api traffic down
                        previousLightState = partitionStates[partition - 1].previousLightState;

                        forceRefresh = partitionStates[partition - 1].refreshLights || forceRefreshGlobal;

                        // ESP_LOGD("test","refreshing partition statuse partitions: %d,force refresh=%d",partition,forceRefresh);
                        if ((currentLightState.fire != previousLightState.fire || forceRefresh) && status_sensors_partition[partition - 1].fire != NULL)
                            status_sensors_partition[partition - 1].fire->process(currentLightState.fire);
                        if ((currentLightState.alarm != previousLightState.alarm || forceRefresh) && status_sensors_partition[partition - 1].alm != NULL)
                            status_sensors_partition[partition - 1].alm->process(currentLightState.alarm);
                        if ((currentLightState.trouble != previousLightState.trouble || forceRefresh) && status_sensors_partition[partition - 1].trbl != NULL)
                            status_sensors_partition[partition - 1].trbl->process(currentLightState.trouble);
                        if ((currentLightState.chime != previousLightState.chime || forceRefresh) && status_sensors_partition[partition - 1].chm != NULL)
                            status_sensors_partition[partition - 1].chm->process(currentLightState.chime);


                        if (statusFlags.systemFlag || updateSystemState)
                        {
                            if ((currentLightState.away != previousLightState.away || forceRefresh) && status_sensors_partition[partition - 1].arma != NULL)
                                status_sensors_partition[partition - 1].arma->process(currentLightState.away);
                            if ((currentLightState.stay != previousLightState.stay || forceRefresh) && status_sensors_partition[partition -1 ].arms != NULL)
                                status_sensors_partition[partition - 1].arms->process(currentLightState.stay);
                            if ((currentLightState.night != previousLightState.night || forceRefresh) && status_sensors_partition[partition - 1].armn != NULL)
                                status_sensors_partition[partition - 1].armn->process(currentLightState.night);
                            if ((currentLightState.instant != previousLightState.instant || forceRefresh) && status_sensors_partition[partition - 1].armi != NULL)
                                status_sensors_partition[partition - 1].armi->process(currentLightState.instant);
                            if ((currentLightState.armed != previousLightState.armed || forceRefresh) && status_sensors_partition[partition - 1].arm != NULL)
                                status_sensors_partition[partition - 1].arm->process(currentLightState.armed);
                        }
                        if ((currentLightState.bypass != previousLightState.bypass || forceRefresh) && status_sensors_partition[partition - 1].byp != NULL)
                            status_sensors_partition[partition - 1].byp->process(currentLightState.bypass);
                        if ((currentLightState.ready != previousLightState.ready || forceRefresh) && status_sensors_partition[partition - 1].rdy != NULL)
                            status_sensors_partition[partition - 1].rdy->process(currentLightState.ready);
                        if ((currentLightState.bat != previousLightState.bat || forceRefresh) && ac_bin_sensor != NULL)
                            ac_bin_sensor->process(currentLightState.ac);
                        if ((currentLightState.bat != previousLightState.bat || forceRefresh) && bat_bin_sensor != NULL)
                            bat_bin_sensor->process(currentLightState.bat);
                        partitionStates[partition - 1].previousLightState = currentLightState;
                        partitionStates[partition - 1].refreshLights = false;
                    }
                }
                std::string zoneStatusMsg = "";
                char s1[16];
                // clears restored zones after timeout
                for (auto &x : alarmZones)
                {

                    if (!x.active || !x.partition)
                        continue;

                    if (!x.bypass && x.open && partitionStates[x.partition - 1].previousLightState.ready)
                    {
                        x.open = false;
                        x.check = false;
                        x.alarm = false;
                        zoneStatusUpdate(&x);
                    }

                    if (x.bypass && !partitionStates[x.partition - 1].previousLightState.bypass)
                    {
                        x.bypass = false;
                    }

                    if (x.alarm && !partitionStates[x.partition - 1].previousLightState.alarm)
                    {
                        x.alarm = false;
                    }

                    if (!x.bypass && x.open && (esp_timer_get_time()- x.time) > TTL)
                    {
                        x.open = false;
                        zoneStatusUpdate(&x);
                    }
                    if (!x.bypass && x.check && (esp_timer_get_time() - x.time) > TTL)
                    {
                        x.check = false;
                        zoneStatusUpdate(&x);
                    }

                    if (forceRefreshZones || forceRefreshGlobal)
                    {
                        zoneStatusUpdate(&x);
                    }
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
                    if (x.lowbat || x.rflowbat)
                    { // low rf battery
                        if (zoneStatusMsg != "")
                            sprintf(s1, ",LB:%d", x.zone);
                        else
                            sprintf(s1, "LB:%d", x.zone);
                        zoneStatusMsg.append(s1);
                    }
                }

                if ((zoneStatusMsg != previousZoneStatusMsg || forceRefreshZones || forceRefreshGlobal) && text_sensors_common.zone_status != NULL)
                    text_sensors_common.zone_status->process(zoneStatusMsg);

                previousZoneStatusMsg = zoneStatusMsg;
                forceRefreshGlobal = false;
            }
            vTaskDelete(NULL);
        }

        void vistaECPHome::processReceiveQueue_task_start(void *args)
        {
            vistaECPHome *tsk = static_cast<vistaECPHome *>(args);
            tsk->processReceiveQueue(args);
        }

        void vistaECPHome::refreshStatusFlags(char * cbuf, struct statusFlagType * statusFlags) 
        {
            char prompt1[18];
            char prompt2[18];
            memcpy(prompt1,&cbuf[12], 16);
            memcpy(prompt2, &cbuf[28], 16);
            statusFlags->keypad[0] = cbuf[1]; // 0 to 7

            statusFlags->keypad[1] = cbuf[2]; // 8 to 15

            statusFlags->keypad[2] = cbuf[3]; // 16 - 23

            statusFlags->keypad[3] = cbuf[4]; // 24 - 31.

            statusFlags->zone = (int)toDec(cbuf[5]);

            statusFlags->beeps = cbuf[6] & BIT_MASK_BYTE1_BEEP;

            statusFlags->fire = ((cbuf[7] & BIT_MASK_BYTE2_FIRE) > 0);
            statusFlags->systemFlag = ((cbuf[7] & BIT_MASK_BYTE2_SYSTEM_FLAG) > 0);
            statusFlags->ready = ((cbuf[7] & BIT_MASK_BYTE2_READY) > 0);

            statusFlags->night = ((cbuf[6] & BIT_MASK_BYTE1_NIGHT) > 0);
            statusFlags->armedStay = ((cbuf[7] & BIT_MASK_BYTE2_ARMED_HOME) > 0);

            statusFlags->lowBattery = ((cbuf[7] & BIT_MASK_BYTE2_LOW_BAT) > 0);

            statusFlags->check = ((cbuf[7] & BIT_MASK_BYTE2_CHECK_FLAG) > 0);
            statusFlags->fireZone = ((cbuf[7] & BIT_MASK_BYTE2_ALARM_ZONE) > 0);

            statusFlags->inAlarm = ((cbuf[8] & BIT_MASK_BYTE3_IN_ALARM) > 0);
            statusFlags->acPower = ((cbuf[8] & BIT_MASK_BYTE3_AC_POWER) > 0);
            statusFlags->chime = ((cbuf[8] & BIT_MASK_BYTE3_CHIME_MODE) > 0);
            statusFlags->bypass = ((cbuf[8] & BIT_MASK_BYTE3_BYPASS) > 0);
            statusFlags->programMode = (cbuf[8] & BIT_MASK_BYTE3_PROGRAM);

            statusFlags->instant = ((cbuf[8] & BIT_MASK_BYTE3_INSTANT) > 0);
            statusFlags->armedAway = ((cbuf[8] & BIT_MASK_BYTE3_ARMED_AWAY) > 0);

            if (!statusFlags->systemFlag) 
                statusFlags->alarm = ((cbuf[8] & BIT_MASK_BYTE3_ZONE_ALARM) > 0);

            statusFlags->promptPos = cbuf[10];

            statusFlags->backlight = ((cbuf[12] & 0x80) > 0);
            cbuf[12] = (cbuf[12] & 0x7F);
            // cbuf[36] = 0xEF;  //Extended ascii code to test with
            // Translate single unicode code point to multibyte UTF8
            // Not sure what encoding all the OUS panel options might use
            // At least for Swedish, doesn't work exactly right but keeps HA from
            // disconnecting on invalid character.
            // As an example, code point EF is given from panel when it should be F6
            // for small o with diaeresis
            for (int i=0; i< 15;i++)
            {
                if (cbuf[i+12] > 126) 
                {
                    char buf[16];
                    memcpy(buf, &cbuf[i+1+12],16 - i - 1); 
                    cbuf[i+12+1] = 0x80 | (cbuf[i+12] & 0x3F);
                    cbuf[i+12] = 0xC0 | (cbuf[i+12] >> 6);
                    memcpy(&cbuf[i+12+2], buf, 16 - i - 2);
                    i++;
                }
            }
            for (int i=0; i<15;i++)
            {
                if (cbuf[i+28] > 126)
                {
                    char buf[16];
                    memcpy(buf, &cbuf[i+28+1],16 - i - 1);
                    cbuf[i+28+1] = 0x80 | (cbuf[i+28] & 0x3F);
                    cbuf[i+28] = 0xC0 | (cbuf[i+28] >> 6);
                    memcpy(&cbuf[i+28+2], buf, 16 - i - 2);
                    i++;
                }
            }
            memcpy(statusFlags->prompt1, &cbuf[12], 16);
            statusFlags->prompt1[16] = 0;
            memcpy(statusFlags->prompt2, &cbuf[28], 16);
            statusFlags->prompt2[16] = 0;
        }

        void vistaECPHome::refreshLRRStatusFlags(char * cbuf, struct lrrstatusFlagType * lrrstatusFlags) 
        {

            int len = cbuf[2];

            if (len == 0)
                return;
            char type = cbuf[3];
            char lcbuf[12];
            int lcbuflen = 0;

            // 0x52 means respond with only cycle message
            // 0x48 means same thing
            //, i think 0x52 and and 0x48 are the same
            if (type == (char)0x52 || type == (char)0x48)
            {
                lcbuf[0] = (char)cbuf[1];
                lcbuflen++;
            }
            else if (type == (char)0x58)
            {
            // just respond, but 0x58s have lots of info
                int c = (((0x0f & cbuf[8]) << 8) | cbuf[9]);
                c = toDec(c); // convert to decimal representation for correct code display
                lrrstatusFlags->qual = (uint8_t)(0xf0 & cbuf[8]) >> 4;
                lrrstatusFlags->code = c;
                lrrstatusFlags->data = toDec(((uint8_t)cbuf[12] >> 4) | ((uint8_t)cbuf[11] << 4));
                lrrstatusFlags->partition = (uint8_t)cbuf[10];

                lcbuf[0] = (char)(cbuf[1]);
                lcbuflen++;
            }
            else if (type == (char)0x53)
            {

                lcbuf[0] = (char)((cbuf[1] + 0x40) & 0xFF);
                lcbuf[1] = (char)0x04;
                lcbuf[2] = (char)0x00;
                lcbuf[3] = (char)0x00;
                // 0x08 is sent if we're in test mode
                // 0x0a after a test
                // 0x04 if you have network problems?
                // 0x06 if you have network problems?
                lcbuf[4] = (char)0x00;
                lcbuflen = 5;
            }

            // we don't need a checksum for 1 byte messages (no length bit)
            // if we don't even have a message length byte, then we are just
            //  ACKing a cycle header byte.
            if (lcbuflen >= 2)
            {
                uint32_t chksum = 0;
                for (int x = 0; x < lcbuflen; x++)
                {
                    chksum += lcbuf[x];
                }
                chksum -= 1;
                chksum = ~chksum;
                lcbuf[lcbuflen] = (char)chksum;
                lcbuflen++;
            }

            if (lrrSupervisor)
            {
                for (int x = 0; x < lcbuflen; x++)
                {
                    vistabus.writedirect(lcbuf,lcbuflen,static_cast<char>((cbuf[1] + 0x40) & 0xFF));
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
    } //namespace
} //namespace