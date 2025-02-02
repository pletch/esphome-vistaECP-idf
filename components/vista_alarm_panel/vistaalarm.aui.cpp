#include "vistaalarm.h"
    
namespace esphome 
{
    namespace alarm_panel 
    {

        bool vistaECPHome::AUIsendTime()
        {
            ESPTime rtc = now();
            if (!rtc.is_valid() || statusFlags.programMode || !auiAddr || (auiCmd.state != rsidle && auiCmd.state != rsdate))
                return false;
            ESP_LOGD(TAG, "Setting AUI time...");
            char bytes[30] = {0,0x68, 0x05, 0x02, 0x45, 0x43, 0xF5, 0xEC, 0x32, 0x34, 0x31, 0x31, 0x31, 0x35, 0x31, 0x31, 0x34, 0x31, 0x30, 0x35, 0x35,0};
            auiSeq = auiSeq == 0xf ? 8 : auiSeq + 1;
            bytes[1] = 0x60 + auiSeq;
            auiCmd.state = rsdate;

            auiCmd.time=esp_timer_get_time();
            auiCmd.pending = true;

            snprintf(&bytes[8], 21, "%02d%02d%02d%02d%02d%02d%1d", rtc.year % 100, rtc.month, rtc.day_of_month, rtc.hour, rtc.minute, 
                rtc.second, rtc.day_of_week - 1);
            vistabus.writedirect(bytes, 22, auiAddr);
            return true;
        }

        void vistaECPHome::AUIprocessQueue()
        {
            uint64_t now = esp_timer_get_time();
            if (auiCmd.state != rsidle && (now - auiCmd.time) > 5*1000*1000)
            { // reset auicmd state if no f2 response after 5 seconds
                ESP_LOGD(TAG, "Setting auicmd state to idle");
                auiCmd.state = rsidle;
                auiCmd.pending = false;
            }
            if (auiQueue.size() > 0 && auiCmd.state == rsidle)
            {
                auiCmd = auiQueue.front();
                auiQueue.pop();
                switch (auiCmd.state)
                {
                    case rsdate:
                        AUIsendTime();
                        break;
                    case rsopenzones:
                        AUIsendZoneRequest();
                        break;
                    default:
                        break;
                }
            }
        }

        void vistaECPHome::AUIset_panel_time()
        {
#if defined(USE_TIME)
            if (auiAddr)
            {
                if (auiCmd.state != rsidle)
                {
                    auiCmdType c;
                    c.state = rsdate;
                    if (auiQueue.size() < 5)
                        auiQueue.push(c);
                }
                 else
                    AUIsendTime();
            }
            if (statusFlags.programMode || auiAddr)
                return;
            ESPTime rtc = now();
            if (!rtc.is_valid())
                return;
            int hour = rtc.hour;
            int year = rtc.year;
            char ampm = hour < 12 ? 2 : 1;
            if (hour > 12)
                hour -= 12;
            char cmd[30];
            sprintf(cmd, "%s#63*|%02d%02d%01d%02d%02d%02d*", accessCode, hour, rtc.minute, ampm, rtc.year % 100, 
                rtc.month, rtc.day_of_month);

            int addr = partitionKeypads[defaultPartition];
            vistabus.write(cmd,30, addr);
#endif
        }

        char *vistaECPHome::AUIparseMessage(char *cmd)
        {

            cmd[cmd[1] + 1] = 0; // 0 to terminate cmd to use as string
            char *c = &cmd[8];   // advance to start of fe xx byte
            char *f = NULL;
            for (uint8_t x = 0; x < cmd[1] - 7; x++)
            { // convert 0 to comma
                c[x] = !c[x] ? ',' : c[x];
            }
            if (auiCmd.state == rsopenzones || auiCmd.state == rsbypasszones)
            {
                char s[] = {0xfe, 0xfe, 0xfe, 0xfe, 0};
                f = strstr(c, s);
                if (f)
                {
                    f = f + strlen(s);
                    if (*f == 0xec)
                        f++;
                    return f;
                }
            }
            else if (auiCmd.state == rszoneinfo)
            {
                char s[] = {0xfe, 0xfe, 0xfe, 0};
                f = strstr(c, s);
                if (f)
                {
                    f = f + strlen(s);
                    if (*f == 0xec)
                        f++;
                    return f;
                }
            }
            else if (auiCmd.state == rszonecount)
            {
                char s[] = {0xfe, 0xfe, 0};
                f = strstr(c, s);
                if (f)
                    return f + strlen(s);
            }
            else if (auiCmd.state == rsidle)
            {
                char s[] = {0xf5, 0xec, 0};
                f = strstr(c, s);
                if (f)
                    return f + strlen(s);
            }
            else if (auiCmd.state == rsdate)
            {
                char s[] = {0xfe, 0};
                f = strstr(c, s);
                if (f)
                    return f + strlen(s);
            }
            else
            {
                char s[] = {0xfd, 0}; // error
                f = strstr(c, s);
                if (f)
                    return f + strlen(s);
            }
            return NULL;
        }
        
        void vistaECPHome::AUIupdateZoneState(zoneType *zt, int p, bool state, uint64_t t)
        {
            zt->partition = p;
            zt->time = t;
            if (auiCmd.state == rsopenzones)
            {
                zt->open = state;
                zoneStatusUpdate(zt);
                ESP_LOGD(TAG, "Setting open zone %d to %d,  partition %d", zt->zone, state, p);
            }
            else if (auiCmd.state == rsbypasszones)
            {
                zt->bypass = state;
                ESP_LOGD(TAG, "Setting bypass zone %d to %d, partition %d", zt->zone, state, p);
            }
        }
            
        void vistaECPHome::AUIsendZoneRequest()
        {
            if (!auiAddr || !(auiCmd.state == rsopenzones || auiCmd.state == rsbypasszones) || auiCmd.pending)
                return;
            auiSeq = auiSeq == 0xf ? 8 : auiSeq + 1;
            char bytes[21] = {0,0x68, 0x62, 0x31, 0x45, 0x49, 0xF5, 0x31, 0xFB, 0x45, 0x4A, 0xF5, 0x32, 0xFB, 0x45, 0x43, 0xF5, 0x31, 0xFB, 0x43, 0x6C};
            bytes[1] = 0x60 + auiSeq;
            bytes[7] = auiCmd.partition;
            bytes[12] = auiCmd.state == rsopenzones ? 0x32 : 0x35;
            auiCmd.pending = true;
            auiCmd.time=esp_timer_get_time();
            ESP_LOGD(TAG, "Sending zone status request %d, header %02X, auiAddr %d", auiCmd.state, bytes[1], auiAddr);
            vistabus.writedirect(bytes,21,auiAddr);
        }

        void vistaECPHome::AUIprocessZoneList(char *list)
        {
            std::string s = "";
            if (list)
            {
                s = list;
            }
            s.append(",");

            ESP_LOGD(TAG, "Zones: %s", s.c_str());
            uint8_t p = auiCmd.partition - 0x30; // set 0x31 - 0x34 to 1 - 4 range

            // Search all occurences of integers or ranges
            uint64_t t = esp_timer_get_time();;
            size_t pos;
            char buf[5], buf1[5];
            char zb_text[30]; // i am not sure how big this is at the moement
            
            while ((pos = s.find(',')) != std::string::npos)
            {
                std::string s1 = s.substr(0, pos);
                memset(zb_text,'\0',sizeof(zb_text));
                strncpy(zb_text,(char *)s1.c_str(),sizeof(zb_text));
                int start1 = 0;
                int len1 = 0;
                int start2 = 0;
                int len2 = 0;
                int z1 = -1;
                int z2 = -1;
                bool z1_found = false;
                bool z2_found = false;
                for (int i = 0; i < sizeof(zb_text); i++) 
                {
                    if(zb_text[i] >= 0x30 && zb_text[i] <= 0x39 && !z1_found && !z2_found) //find first digit in possible range
                    {
                        start1 = i;
                        len1++;
                        z1_found = true;
                    }
                    else if (zb_text[i] >= 0x30 && zb_text[i] <= 0x39 && z1_found && !z2_found) //continue parsing first zone number by looking for digits
                    {
                        len1++;
                    }
                    else if(zb_text[i] == 0x2D && z1_found) //dash found after first zone number
                    {
                        start2 = i+1;
                        z2_found = true;
                    }
                    else if(zb_text[i] != 0x2D && z1_found && !z2_found) //no dash found after first zone number. exit loop
                    {
                        break;
                    }
                    else if(zb_text[i] >= 0x30 && zb_text[i] <= 0x39 && z1_found && z2_found) //parse second zone number
                    {
                        len2++;
                    }
                    else if((zb_text[i] < 0x30 || zb_text[i] > 0x39) && z1_found && z2_found ) //found end of second zone number
                        break;
                }
                if (z1_found && len1 > 0 && len1 < 5)
                {
                    strncpy(buf,zb_text+start1,len1);
                    z1 = toInt(buf, 10);
                }
                if (z1_found && z2_found && len1 > 0 && len1 < 5 && len2 > 0 && len2 <5) // Yes, range, add all values within to the vector
                {
                    strncpy(buf1,zb_text+start2,len2);
                    z2 = toInt(buf1, 10);
                    for (int z = z1; z <= z2; ++z)
                    {
                        AUIupdateZoneState(getZone(z), p, true, t);
                    }
                }
                else if (z1_found && len1 > 0 && len1 < 5 && !z2_found) //No, not a range. Add single value to vector.
                {
                    AUIupdateZoneState(getZone(z1), p, true, t);
                }
                s.erase(0, pos + 1); /* erase() function store the current positon and move to next token. */
            }

            // clear  bypass/open zones for partition p that were not set above
            auto it = std::find_if(extZones.begin(), extZones.end(), [&p, &t](zoneType &f)
                    { return (f.partition == p && f.active && f.time != t && (f.open || f.bypass)); });

            while (it != extZones.end())
            {
                AUIupdateZoneState(&(*it), p, false, esp_timer_get_time());

                it = std::find_if(++it, extZones.end(), [&p, &t](zoneType &f)
                        { return (f.partition == p && f.active && f.time != t && (f.open || f.bypass)); });
            }
            //forceRefreshZones = true; 
        }


        void vistaECPHome::AUIprocessF2(char *cbuf)
        {
            if (auiCmd.state != rsidle)
              ESP_LOGD(TAG, "AUI cmd state: %d, pending: %d", auiCmd.state, auiCmd.pending);
            if (((cbuf[2] >> 1) & auiAddr) && (cbuf[7] & 0xf0) == 0x60 && cbuf[8] == 0x63 && cbuf[9] == 0x02)
            { // partition update broadcast
              char *m = AUIparseMessage(cbuf);
              if (m == NULL)
                return;
              size_t l = &cbuf[1] + cbuf[1] - m;
              // ESP_LOGD(TAG, "m length = %d,byte=%02X", l, m[0]);
              // if (m[0] & 1)
              // {
              if (auiCmd.state == rsidle)
              {
                auiCmd.state = rsopenzones;
                auiCmd.partition = cbuf[13];
                auiCmd.pending = false;
                AUIsendZoneRequest();
              }
              else if (auiCmd.state != rsopenzones && auiCmd.state != rsbypasszones)
              {
                auiCmdType c;
                c.state = rsopenzones;
                c.partition = cbuf[13];
                if (auiQueue.size() < 5)
                  auiQueue.push(c);
              }
              //   }
              // else
              if (l > 4 && m[0] == 2)
              {
                // we have an exit delay
                // exitDelay=m[5] for partition partitionRequest
              }
            }
            else if (((cbuf[2] >> 1) & auiAddr) && (cbuf[7] & 0xf0) == 0x50 && cbuf[8] == 0xfe && cbuf[10] != 0xfd)
            { // response data from request
              char *m = AUIparseMessage(cbuf);
              if (m == NULL)
                return;
              auiCmd.time = esp_timer_get_time();
              ESP_LOGD(TAG, "success message from %d", auiCmd.state);
              auiCmd.pending = false;
              if (auiCmd.state == rsopenzones || auiCmd.state == rsbypasszones)
              {
                AUIprocessZoneList(m);

                if (auiCmd.state == rsopenzones)
                {
                  auiCmd.state = rsbypasszones;
                  AUIsendZoneRequest();
                }
                else
                  auiCmd.state = rsidle;
              }
              else if (auiCmd.state == rsdate)
              {
                auiCmd.state = rsidle;
              }
            }
            else if (((cbuf[2] >> 1) & auiAddr) && (cbuf[7] & 0xf0) == 0x50 && (cbuf[8] == 0xfd || cbuf[10] == 0xfd))
            {
              char *m = AUIparseMessage(cbuf);
              if (m == NULL)
                return;
              auiCmd.time = esp_timer_get_time();
              auiCmd.pending = false;
              ESP_LOGD(TAG, "failure message from %d", auiCmd.state);
              if (auiCmd.state == rszoneinfo)
              {
                auiCmd.record++;
                if (auiCmd.record > auiCmd.records)
                  auiCmd.state = rsidle;
              }
              else if (auiCmd.state == rsdate)
              {
                auiCmd.state = rsidle;
              }
              else
                auiCmd.state = rsidle;
            }
        }
    }
}