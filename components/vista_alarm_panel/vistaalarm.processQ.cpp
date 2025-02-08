/*
Functions in this file:
processReceiveQueue
processReceiveQueue_task_start
refreshStatusFlags
refreshLRRStatusFlags
*/

#include "vistaalarm.h"
    
namespace esphome 
{
    namespace alarm_panel 
    {
        void vistaECPHome::processReceiveQueue(void *args)
        {
            while (1)
            {
                char payload[48];
                int size;
                int type;
                memset(payload,'\0',sizeof(payload));
                bool F7_no_change = true;

                if (vistabus.read_packet(payload,size,type, true)) 
                {
                    if (debug > 0 && type == 0)
                    {
                        if (payload[0] == 0xF7)
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

                            if ((esp_timer_get_time() - last_refresh) > 60*1000*1000)
                            {
                                forceRefreshGlobal = true;
                            }

                            getPartitionsFromMask();
                            for (uint8_t partition = 1; partition <= maxPartitions; partition++)
                            {
                                if (partitions[partition - 1])
                                {
                                    forceRefresh = partitionStates[partition - 1].refreshStatus || forceRefreshGlobal;
                                    ESP_LOGI("v-a", "Partition: %02X", partition);

                                    updateDisplayLines(partition);
                                    if (partitionStates[partition - 1].lastbeeps != statusFlags.beeps || forceRefresh)
                                    {
                                        beepsCallback(std::to_string(statusFlags.beeps), partition);
                                    }

                                    partitionStates[partition - 1].lastbeeps = statusFlags.beeps;

                                    if (statusFlags.systemFlag && strstr(statusFlags.prompt2, HITSTAR))
                                        alarm_keypress_partition("*", partition);
                                }
                                //forceRefreshZones = true;
                            }
                            ESP_LOGI("v-a", "Prompt: %s", statusFlags.prompt1);
                            ESP_LOGI("v-a", "Prompt: %s", statusFlags.prompt2);
                            ESP_LOGI("v-a", "Beeps: %d", statusFlags.beeps);
                        //forceRefreshZones = true;
                        }
                        if (payload[0]==0xF2)
                        {
                            if (auiAddr)
                            AUIprocessF2(payload);
                        }
                        else if ((payload[0] == 0xF9))
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
                                        uf = "on zone";
                                        zn=getZoneName(z);
                                    }

                                    snprintf(msg,100, "CID_%d%03d: %s %s %s%s, Partition %d", q,c, &lrrString[1], uf.c_str(), zn.c_str(), qual.c_str(),p);

                                    lrrMsgChangeCallback(msg);
                                    //refreshLrrTime = esp_timer_get_time();
                                }
                            }
                        }
                    }
                    if (type == 1) 
                    {
                        if (payload[0] == 0xFA)
                        {
                            int z = payload[3];
                            if (payload[2] == 0xf1 && z > 0 && z <= maxZones)
                            { // we have a zone status (zone expander address range)
                                ESP_LOGD(TAG, "fa status update to zone");
                                zoneType *zt = getZone(z);

                                if (zt->active)
                                {
                                    zt->time = esp_timer_get_time();
                                    zt->open = payload[4];
                                    zoneStatusUpdate(zt);
                                }
                            }
                            else if (payload[2] == 0x00)
                            { // relay update z = 1 to 4
                            if (z > 0)
                                {
                                    relayStatusChangeCallback(payload[1], z, payload[4] ? true : false);
                                    if (debug > 0)
                                        ESP_LOGD(TAG, "Got relay address %d channel %d = %d", payload[1], z, payload[4]);
                                }
                            }
                            else if (payload[2] == 0x0d)
                            { // relay update z = 1 to 4 - 1sec on / 1 sec off
                                if (z > 0)
                                {
                                    // relayStatusChangeCallback(vistaCmd.cbuf[1],z,vistaCmd.cbuf[4]?true:false);
                                    if (debug > 0)
                                        ESP_LOGD(TAG, "Got relay address %d channel %d = %d. Cmd 0D. Pulsing 1sec on/ 1sec off", payload[1], z, payload[4]);
                                }
                            }
                            else if (payload[2] == 0xf7)
                            { // 30 second zone expander module status update
                                uint8_t faults = payload[4];
                                for (int x = 8; x > 0; x--)
                                {
                                    z = getZoneFromChannel(payload[1], x); // device id=extcmd[1]
                                    if (!z)
                                        continue;
                                    bool zs = faults & 1 ? true : false; // check first bit . lower bit = channel 8. High bit= channel 1
                                    faults = faults >> 1;                // get next zone status bit from field
                                    zoneType *zt = getZone(z);
                                    if (zt->open != zs && zt->active)
                                    {
                                        zt->open = zs;
                                        zoneStatusUpdate(zt);
                                    }
                                    zt->time = esp_timer_get_time();
                                }
                            }
                        }     
                        else if (payload[0] == 0xFE && size == 7 && payload[1] == 0)
                        {
                            char rf_serial_char[14];
                            char rf_serial_char_out[20];
                            // FB 04 06 18 98 B0 00 00 00 00 00 00  <-- Pattern from original upstream.
                            // FE 00 54 83 8f 89 a0 = Open / Active for door sensor.    Hardware UART produces FE after UART break rather than FB header.
                            // FE 00 54 83 8f 89 80 = Closed / Inactive
                            // fe 00 51 85 f4 03 04 = heartbeat
                            uint32_t device_serial = ((payload[3] & 0xF) << 16) + (payload[4] << 8) + payload[5];
                            snprintf(rf_serial_char, 14, "%03lu%04lu", device_serial / 10000, device_serial % 10000);
                            serialType rf = getRfSerialLookup(rf_serial_char);
                            int z = rf.zone;
                            if (debug > 0)
                                ESP_LOGI(TAG, "RFX: %s,%02x", rf_serial_char, payload[6]);
                            if (z && !(payload[6] & 4) && !(payload[6] & 1))
                            { // ignore heartbeat
                                zoneType *zt = getZone(z);
                                if (zt->active)
                                {
                                    zt->time = esp_timer_get_time();
                                    zt->open = payload[6] & rf.mask ? true : false;
                                    zt->rflowbat = payload[6] & 2 ? true : false; // low bat
                                    //ESP_LOGD(TAG, "set rf low bat to %d", zt->rflowbat);
                                    zoneStatusUpdate(zt);
                                }
                            }
                            sprintf(rf_serial_char_out, "%s,%02x", rf_serial_char, payload[5]);
                            rfMsgChangeCallback(rf_serial_char);
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
                    getZone(statusFlags.zone)->fire = true;
                }
                // zone alarm status
                if (!statusFlags.systemFlag && !statusFlags.check && statusFlags.alarm)
                {
                    if (payload[5] > 0x90)
                        getZoneFromPrompt(statusFlags.prompt1);
                        // if (promptContains(p1,ALARM,tz) && !statusFlags.systemFlag) {
                    zoneType *zt = getZone(statusFlags.zone);
                    ESP_LOGD("test", "updating check zone %d,status=%d", statusFlags.zone, zt->check);
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
                    // ESP_LOGD("test","alarm found for zone %d,status=%d",statusFlags.zone,zt->alarm );
                }
                // device check status
                if (statusFlags.check)
                {
                    updateSystemState = true; // we also get system flags when a device has a check flag
                    if (payload[5] > 0x90)
                        getZoneFromPrompt(statusFlags.prompt1);
                    zoneType *zt = getZone(statusFlags.zone);
                    ESP_LOGD("test", "check found for zone %d,status=%d", statusFlags.zone, zt->check);
                    if (!zt->check && zt->active)
                    {
                        zt->check = true;
                        zt->open = false;
                        zt->alarm = false;
                        currentLightState.trouble = true;
                        zoneStatusUpdate(zt);
                        // ESP_LOGD("test","check found for zone %d,status=%d",statusFlags.zone,zt->check );
                    }
                    if (!zt->partition && zt->active)
                        assignPartitionToZone(zt);
                    zt->time = esp_timer_get_time();
                }
                // zone fault status
                // ESP_LOGD("test","armed status/system,stay,away flag is: %d , %d, %d , %d",statusFlags.armed,statusFlags.systemFlag,statusFlags.armedStay,statusFlags.armedAway);
                if (!statusFlags.systemFlag && !statusFlags.check && !statusFlags.bypass && !statusFlags.alarm && 
                    !(statusFlags.instant || statusFlags.armedAway || statusFlags.armedStay || statusFlags.night))
                {
                    if (payload[5] > 0x90)
                    {
                        getZoneFromPrompt(statusFlags.prompt1);
                    }
                    // if (statusFlags.zone==4) statusFlags.zone=997;
                    // if (promptContains(p1,FAULT,tz) && !statusFlags.systemFlag) {

                    zoneType *zt = getZone(statusFlags.zone);
                    if (!zt->open && zt->active)
                    {
                        zt->open = true;
                        zt->check = false;
                        zt->bypass = false;
                        zoneStatusUpdate(zt);
                    }
                    if (!zt->partition && zt->active)
                        assignPartitionToZone(zt);

                    // ESP_LOGD("test","fault found for zone %d,status=%d",statusFlags.zone,zt->open);
                    zt->time = esp_timer_get_time();
                }
                // zone bypass status
                if (!statusFlags.systemFlag && !statusFlags.check && statusFlags.bypass && !statusFlags.alarm && 
                    !(statusFlags.instant || statusFlags.armedAway || statusFlags.armedStay || statusFlags.night))
                {
                    if (payload[5] > 0x90)
                    getZoneFromPrompt(statusFlags.prompt1);
                    // if (promptContains(p1,BYPAS,tz) && !statusFlags.systemFlag) {

                    zoneType *zt = getZone(statusFlags.zone);

                    if (!zt->bypass && zt->active)
                    {
                        zt->bypass = true;
                        zoneStatusUpdate(zt);
                    }
                    if (!zt->partition && zt->active)
                        assignPartitionToZone(zt);
                    zt->time = esp_timer_get_time();

                    // ESP_LOGD("test","bypass found for zone %d,status=%d",statusFlags.zone,zt->bypass);
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
                // ESP_LOGE(TAG,"ac=%d,batt status = %d,systemflag=%d,lightbat status=%d,trouble=%d", 
                //currentLightState.ac,statusFlags.lowBattery,statusFlags.systemFlag,currentLightState.bat,currentLightState.trouble);

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
                        getZone(fireStatus.zone)->fire = false;
                }
                if ((chkTime - alarmStatus.time) > TTL)
                {
                    alarmStatus.state = false;
                    if (alarmStatus.zone > 0 && alarmStatus.zone <= maxZones)
                        getZone(alarmStatus.zone)->alarm = false;
                }
                if ((chkTime - panicStatus.time) > TTL)
                {
                    panicStatus.state = false;
                    if (panicStatus.zone > 0 && panicStatus.zone <= maxZones)
                        getZone(panicStatus.zone)->panic = false;
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

                        if (currentSystemState != partitionStates[partition - 1].previousSystemState || forceRefresh)
                        switch (currentSystemState)
                        { 
                            case striggered:
                                systemStatusChangeCallback(STATUS_TRIGGERED, partition);
                                break;
                            case sarmedaway:
                                systemStatusChangeCallback(STATUS_ARMED, partition);
                                break;
                            case sarmednight:
                                systemStatusChangeCallback(STATUS_NIGHT, partition);
                                break;
                            case sarmedstay:
                                systemStatusChangeCallback(STATUS_STAY, partition);
                                break;
                            case sunavailable:
                                systemStatusChangeCallback(STATUS_NOT_READY, partition);
                                break;
                            case sdisarmed:
                                systemStatusChangeCallback(STATUS_OFF, partition);
                                break;
                            default:
                                systemStatusChangeCallback(STATUS_NOT_READY, partition);
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
                        if (currentLightState.fire != previousLightState.fire || forceRefresh)
                            statusChangeCallback(sfire, currentLightState.fire, partition);
                        if (currentLightState.alarm != previousLightState.alarm || forceRefresh)
                            statusChangeCallback(salarm, currentLightState.alarm, partition);
                        if ((currentLightState.trouble != previousLightState.trouble || forceRefresh))
                            statusChangeCallback(strouble, currentLightState.trouble, partition);
                        if (currentLightState.chime != previousLightState.chime || forceRefresh)
                            statusChangeCallback(schime, currentLightState.chime, partition);
                        // if (currentLightState.check != previousLightState.check || forceRefresh)
                        //     statusChangeCallback(scheck, currentLightState.check, partition);

                        if (currentLightState.ac != previousLightState.ac || forceRefresh)
                        statusChangeCallback(sac, currentLightState.ac, partition);

                        if (statusFlags.systemFlag || updateSystemState)
                        {
                            if (currentLightState.away != previousLightState.away || forceRefresh)
                                statusChangeCallback(sarmedaway, currentLightState.away, partition);
                            if (currentLightState.stay != previousLightState.stay || forceRefresh)
                                statusChangeCallback(sarmedstay, currentLightState.stay, partition);
                            if (currentLightState.night != previousLightState.night || forceRefresh)
                                statusChangeCallback(sarmednight, currentLightState.night, partition);
                            if (currentLightState.instant != previousLightState.instant || forceRefresh)
                                statusChangeCallback(sinstant, currentLightState.instant, partition);
                            if (currentLightState.armed != previousLightState.armed || forceRefresh)
                                statusChangeCallback(sarmed, currentLightState.armed, partition);
                        }
                        if (currentLightState.bat != previousLightState.bat || forceRefresh)
                            statusChangeCallback(sbat, currentLightState.bat, partition);
                        if (currentLightState.bypass != previousLightState.bypass || forceRefresh)
                            statusChangeCallback(sbypass, currentLightState.bypass, partition);
                        if (currentLightState.ready != previousLightState.ready || forceRefresh)
                            statusChangeCallback(sready, currentLightState.ready, partition);

                        partitionStates[partition - 1].previousLightState = currentLightState;
                        partitionStates[partition - 1].refreshLights = false;
                    }
                }
                std::string zoneStatusMsg = "";
                char s1[16];
                // clears restored zones after timeout
                for (auto &x : extZones)
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
                        zoneStatusUpdate(&x);

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

                if ((zoneStatusMsg != previousZoneStatusMsg || forceRefreshZones || forceRefreshGlobal) && zoneExtendedStatusCallback != NULL)
                    zoneExtendedStatusCallback(zoneStatusMsg);

                previousZoneStatusMsg = zoneStatusMsg;
                //firstRun = false;
                //forceRefreshZones = false;
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
            prompt1[16] = 0;
            memcpy(prompt2, &cbuf[28], 16);
            prompt2[16] = 0;
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
                chksum = chksum ^ 0xFF;
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

    } //namespace
} //namespace