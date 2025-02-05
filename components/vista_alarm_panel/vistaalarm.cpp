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
            uint8_t loop = toInt(token2, 10);
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
      // tg_timer_init(TIMER_GROUP_0, TIMER_0);
      //  use a pollingcomponent and change the default polling interval from 16ms to 8ms to enable
      //   the system to not miss a response window on commands.

      bMap = App.get_binary_sensors();
      tMap = App.get_text_sensors();
      set_update_interval(16); // set interval to fire in main loop task to 16ms
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
          vistabus.write(code.c_str(),4, addr);
          vistabus.write("3",1,addr);
        }
      }
      // Arm away
      else if ((state.compare("A") == 0 || state.compare("W") == 0) && !partitionStates[partition - 1].previousLightState.armed)
      {

        if (quickArm)
          vistabus.write("#2",1, addr);
        else if (code.length() == 4)
        {
          vistabus.write(code.c_str(),4, addr);
          vistabus.write("2",1, addr);
        }
      }
      else if (state.compare("I") == 0 && !partitionStates[partition - 1].previousLightState.armed)
      {
        if (quickArm)
          vistabus.write("#7",1, addr);
        else if (code.length() == 4)
        {
          vistabus.write(code.c_str(),4, addr);
          vistabus.write("7",1, addr);
        }
      }
      else if (state.compare("N") == 0 && !partitionStates[partition - 1].previousLightState.armed)
      {

        if (quickArm)
          vistabus.write("#33",1, addr);
        else if (code.length() == 4)
        {
          vistabus.write(code.c_str(),4, addr);
          vistabus.write("33",1, addr);
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
          vistabus.write(code.c_str(),4, addr);
          vistabus.write("6#",1, addr);
        }
      }
      else if (state.compare("Y") == 0)
      {
        if (code.length() == 4)
        {
          vistabus.write(code.c_str(),4, addr);
          vistabus.write("600",1, addr);
        }
      }
      else if (state.compare("D") == 0)
      {
        if (code.length() == 4)
        { // ensure we get 4 digit code
          vistabus.write(code.c_str(),4, addr);
          vistabus.write("1",1, addr);
          vistabus.write(code.c_str(),4, addr);
          vistabus.write("1",1, addr);
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
      {
        statusFlags->alarm = ((cbuf[8] & BIT_MASK_BYTE3_ZONE_ALARM) > 0);
      }

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
      //expectByte = lcbuf[0];
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

    void vistaECPHome::update()
    {
      
      if (!vistabus.connected() && esp_timer_get_time() - last_refresh > 30*1000*1000)
      {
        ESP_LOGE(TAG, "Data timeout. Is the panel connected?");
        last_refresh = esp_timer_get_time();
        return;
      }
      if (auiAddr)
        AUIprocessQueue();

      //static unsigned long long refreshLrrTime, refreshRfTime;   <--Not used right now
  
      char payload[48];
      int size;
      int type;
      memset(payload,'\0',sizeof(payload));
      bool F7_no_change = true;

      if (vistabus.read_packet(payload,size,type)) 
      {
        if (debug > 0 && type == 0)
        {
          if (payload[0] == 0xF7)
            printPacket("CMD", payload, 13);
          else if ((size == 4) && (payload[0] == (payload[0]+payload[1]+payload[2]+payload[3]))) //1 byte response
          {
            printPacket("CMD", payload, 1);
            return;
          }
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
                ESP_LOGI(TAG, "Partition: %02X", partition);

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
            ESP_LOGI(TAG, "Prompt: %s", statusFlags.prompt1);
            ESP_LOGI(TAG, "Prompt: %s", statusFlags.prompt2);
            ESP_LOGI(TAG, "Beeps: %d", statusFlags.beeps);
            //forceRefreshZones = true;
          }
          if (payload[0]==0xF2)
          {
            if (auiAddr)
              AUIprocessF2(payload);
            return;
          }
          else if ((payload[0] == 0xf9))
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
                return;
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
          else if (payload[0] == 0xFE && size == 7)
          {
            char rf_serial_char[14];
            char rf_serial_char_out[20];
            // FB 04 06 18 98 B0 00 00 00 00 00 00  <-- Pattern from original upstream.
            // FE 00 54 83 8f 89 a0 = Open / Active for door sensor.  Hardware UART produces FE after UART break rather than FB header.
            // FE 00 54 83 8f 89 80 = Closed / Inactive
            // fe 00 51 85 f4 03 04 = heartbeat
            uint32_t device_serial = ((payload[3] & 0xF) << 16) + (payload[4] << 8) + payload[5];
            snprintf(rf_serial_char, 14, "%03lu%04lu", device_serial / 10000, device_serial % 10000);
            serialType rf = getRfSerialLookup(rf_serial_char);
            int z = rf.zone;
            if (debug > 0)
            {
              ESP_LOGI(TAG, "RFX: %s,%02x", rf_serial_char, payload[6]);
            }
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
                8 -	Loop 1

            */
        }
      }
        // done other cmd processing.  Process f7 now
        if (!forceRefreshGlobal)
          return;
        
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
        // clear alarm statuses  when timer expires
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
        //  if ((millis() - systemPrompt.time) > TTL) systemPrompt.state = false;
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
            //   statusChangeCallback(scheck, currentLightState.check, partition);

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

            //  if (currentLightState.canceled != previousLightState.canceled)
            //   statusChangeCallback(scanceled,currentLightState.canceled,partition);

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

        if ((zoneStatusMsg != previousZoneStatusMsg || forceRefreshZones || forceRefreshGlobal) && zoneExtendedStatusCallback != NULL)
          zoneExtendedStatusCallback(zoneStatusMsg);

        previousZoneStatusMsg = zoneStatusMsg;
        //firstRun = false;
        //forceRefreshZones = false;
        forceRefreshGlobal = false;
      
    }
  }
} // namespaces
