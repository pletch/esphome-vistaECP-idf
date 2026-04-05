# Honeywell Resideo Vista ECP ESPHome IDF external component
 
## This version is a highly modified FORK of the vista-ecp project!!

Original project:  https://github.com/Dilbert66/esphome-vistaECP

Refer to original project page for background and details on installation and circuit design.
  
Why fork?
1. Project presented an opportunity for me to learn about esp-idf and about coding an external esphome component.
2. At the time of the fork, original project based on Arduino rather than native ESP-IDF which is the recommended framework. While original project now supports ESP-IDF through Arduino as a component, Arduino is an additional abstraction which introduces more opportunity for bugs etc.
3. Original project based on bit-banging-based software serial which is fragile and represents an inefficient use of purpose built dedicated hardware on a device such as the ESP32 which has dedicated UART serial devices.
4. In an effort to support many different use cases and legacy ESP8266 devices, original project contains functionality and code not needed for a device specifically targeting use of esphome for interfacing a Honeywell Vista alarm system to Home Assistant.

Fork is shared here for the benefits of any others that might want to use it.  

### Key differences from original project:
- Uses one or two (if monitoring TX wire for RF messages etc.) hardware UARTS on the ESP32 family rather than software GPIO bit-banging.
- Efficient use of limited micro CPU cycles through use of hardware UART and FreeRTOS multitasking. This includes complete fidelity of packet response handling including 2400 baud preamble bytes. New vistabus class designed using FreeRTOS tasks for UART comm and intertask communication via FreeRTOS Queues.
- Arduino dependency removed and refactored for ESP-IDF v5.4+.
- Relay board emulation has been removed. Expander emulation remains.
- AUI handling has been refactored.
- Config refactored with validation of parameters. Carefully examine the example YAML file (https://github.com/pletch/esphome-vistaECP-idf/blob/idf/vista-ecp-idf.yaml) for specifying sensors and other details as these are different from original project.
- zone emulation specifier in yaml config allows emulation of either hardwired or rf virtual zones. Specifying for hardwired zone automatically enables expander board emulation (e.g. Honeywell 4219) on appropriate address and corresponding group of eight zone numbers for virtual hardwired zones. Specifier on zones with rf serial / loop definition allows for virtual rf zone emulation when rf receiver emulation is also enabled.
- Targeted only towards ESPHome API.  Stand-alone MQTT is removed.
- Will not work with older ESP8266.
- Unused residual code, bitwise operations, and variable handling cleaned up.
- Capability to temporarily enable RMT module for outputting bus pulse pattern to log for debugging.
- Unified code base supports both older Vista SE and newer Vista 20P protocol via configuration.

### ⚠️Caution: There may be features / capabilities carried over from original project but unused by me that are minimally tested.  Will test these more thoroughly in the future when I get time but please let me know if you try and they do not work.
Some specifics include: 
- RF Receiver emulation and associated emulated RF zones.
- Virtual zone emulation and other extended functionality on older SE panels not fully tested yet.  


### YAML Configuration Options (See YAML in repo for more examples):
```
vista_alarm_panel:
    keypad_addr_1: ##
    keypad_addr_2: 0
    keypad_addr_3: 0
    rx_pin: ##
    tx_pin: ##
    uart_1: #
```
Configuration variables:
- **keypad_addr_1, ... , keypad_addr_ 8 (Required)**:  Virtual keypad address for partitions 1 - 8. Enable address in alarm panel programming via program fields *190 - *196 if using Vista 20p. Setting value to zero disables. At least one of the partition keypad must be defined non-zero and any additional are optional. The defined address must not conflict with the address of any physical keypads already present in the system.
- **rx_pin (Required, PIN)**: GPIO pin assigned to UART for data receive (yellow line)
- **tx_pin (Required, PIN)**: GPIO pin assigned to UART for data transmit (green line)
- **uart_1 (Required, UART)**: Hardware UART number associated with tx/rx 
- **access_code** (*Optional*): Alarm code used for arming / disarming.  Typically defined in secrets file.
- **aui_addr** (*Optional*, int): AUI address from program field *189 to use for faster zone closure status for zones directly hardwired to the panel and to allow time sync of panel via manual Home Assistant service. Ensure it is not assigned to a physical touchpad or to Total Connect 2.0 (assigned to address 2). Note: Older vista20 panel firmwares only have addresses 1 and 2 while newer will have 1,2,5 and 6. For Vista128,Vista250 commercial panels, ensure the address used is setup as an AUI keypad in program #93, device programming. Omission of parameter or a value of zero disables AUI handling.
- **aui_auto_clock_sync** (*Optional*, boolean): Set to true to automatically sync panel clock against esphome time using AUI command handling. Time offset is verified to be less than 60 seconds and if needed, initial sync occurs 2 minutes after boot and every 6 hrs thereafter. AUI address must be enabled in combination with this option. Defaults to false.
- **default_partition** (*Optional*, int): Set to designate main partition number.  Defaults to 1 if not defined.
- **debug_log** (*Optional*, boolean): Set to true to enable additional bus activity logging and print full ecp packet contents to the log.  Global esphome and vista-alarm component (if configured)
logging level must be set to DEBUG or higher in logging component section to output all messages.
- **debug_pulsing** (*Optional*, boolean): Enables use of ESP32 RMT peripheral to capture and output pulse pattern for diagnostics.  Pulse pattern output to log commences 60 seconds after startup and continues indefinitely.  **DO NOT** enable for routine use of component!
- **legacy_protocol** (*Optional*, boolean): Enables older protocol handling for use with Vista 15SE / Vista20SE panels.
- **lrr_supervisor** (*Optional*, boolean): Set to true to enable Long Range Radio emulation for monitoring and decoding status updates. Do not enable if the system is monitored and an actual long range radio is already present in the system. Defaults to false.
- **rf_receiver_emulation** (*Optional*, boolean): Set to true to enable RF Receiver module (5881ENH) emulation for creating virtual RF zones. Do not enable if the system already includes a physical RF receiver as these Vista systems only support a single RF receiver device. Defaults to false.
- **rf_receiver_addr** (*Optional*, int): Set to suitable address for RF Receiver per your panel installation instructions. Only permissible address on Vista 15/20 panels is 0. Defaults to 0.
- **monitor_pin** (*Optional*, PIN): GPIO pin to use for monitoring module traffic such as RF or Expanders. Leave undefined or set to -1 to disable.
- **uart_2** (*Optional*, UART): Hardware UART number to use for monitoring module traffic via monitorpin. Disabled if monitorpin undefined or monitorpin set to -1.
- **ttl** (*Optional*, int): Time to live in seconds for expiring zone/fire status. Relevant for configurations not using monitor pin and for zones hardwired to the panel. Defaults to 30 if not defined.


```
binary_sensor:
# example hard-wired zone
  - platform: vista_alarm_panel
    id: z8  
    name: "Flood Sensor"
    partition: 1
    zone: 8
    device_class: moisture

# example emulated hard-wired zone
  - platform: vista_alarm_panel
    id: z9  
    name: "Office Window"
    partition: 1
    zone: 9
    emulated: true
    device_class: moisture

# example rf wireless zone
  - platform: vista_alarm_panel
    id: z10
    name: "Front Door"
    partition: 1
    zone: 10
    rf_serial: 123456
    rf_loop: 2
    device_class: door

# example non-zone status sensors
  - platform: vista_alarm_panel
    id: rdy_1
    name: "Ready"
    partition: 1
    status_indicator: "ready"

  - platform: vista_alarm_panel
    id: ac
    name: "Power Status"
    status_indicator: "ac_power"
    device_class: plug
```
Configuration variables:  
  
[zone binary status]
- **emulated** (*Optional*, boolean) Enable virtual zone emulation.  If specified in zone with rf_serial / rf_loop options defined and rf_receiver_emulation is enabled, an RF zone is emulated. The required heartbeat/supervisory signals are handled internally.  If specified without rf options, hardwired zone is emulated through automatic expander board emulation (e.g. Honeywell 4129). Ensure that the zone number selected for emulated hardwired zone does not conflict with existing physical boards in your system.  This is useful for associating other gpio on ESP32 or other Home Assistant sensors with alarm panel zone. Defaults to **false** if not defined. 
 
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Emulated hardwired zones 9-16 will enable expander emulation on address 7.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Emulated hardwired zones 17-24 will enable expander emulation on address 8.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Emulated hardwired zones 25-32 will enable expander emulation on address 9.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Emulated hardwired zones 33-40 will enable expander emulation on address 10.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Emulated hardwired zones 41-48 will enable expander emulation on address 11.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Emulated hardwared zones 10-17 will enable expander emulation on address 1 on legacy SE protocol.

- **partition** (*Optional*, int) Partion number associated with zone.
- **rf_loop** (*Optional*, int)  Loop number of RF device (see rf_serial info below)
- **rf_serial** (*Optional*, int) Unique RF serial number of wireless device. Enroll your RF serial devices using the rf_serial and rf_loop parameters. For most devices loop1 is used such as 5800pir, other devices such as 5816 will use loop2. Please refer to your RF device programming (*56 program) to see what loop and zones are assigned to your RF devices.  
&nbsp;&nbsp;&nbsp;!!Do not include leading zeros in rf_serial!!
- **zone** (*Optional*, int):  Panel configured zone number.
  
[non-zone binary status]
- **partition** (*Optional*, int) Partion number associated with status sensor.
- **status_indicator** (*Optional*, string) Valid options are [ready, trouble, bypass, armed_away, armed_instant, armed_night, chime, alarm, fire, ac_power, and battery]. Partition specific indicators will require specifying partition.
    
```
text_sensor:
# Example text sensors
# RF zone messages 
  - platform: vista_alarm_panel
    id: rf  
    name: "RF Msg"
    type: "rf_messages"
    icon: "mdi:alert-box"    

# virtual lcd keypad line1 and line2 messages for each partition   
  - platform: vista_alarm_panel
    name: "Line1"
    id: ln1_1
    partition: 1
    type: "line1"    
  - platform: vista_alarm_panel
    name: "Line2"
    id: ln2_1
    partition: 1
    type: "line2"
```
Configuration variables:  
- **partition** (*Optional*, int) Partition number associated with text sensor.
- **type** (*Optional*, string) Zone text sensors can be specified to include alternative zone status as a text sensor using type zone.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Output values: (O=open, B=bypass,T=trbl or check,A=alarm)  
  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Other general text sensors are available.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;lrr_messages = long range radio messages  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;rf_messages = rf messages  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;line1 = keypad prompt line 1 displayed messages  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;line2 = keypad prompt line 2 displayed messages  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;zone_status = combined status for zones with messages  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;beeps = beeps output


### Example log output for working Vista20P installation:
```
[13:04:52.169][D][vista-alarm:554][pRQtask]: (PANEL-->KPD) [13:04:51.73] F7 00 00 FB 10 08 00 1C 08 02 00 00 2A 2A ...
[13:04:52.172][I][vista-alarm:063][pRQtask]: Partition: 1
[13:04:52.174][I][vista-alarm:064][pRQtask]: Prompt: ****DISARMED****
[13:04:52.177][I][vista-alarm:065][pRQtask]: Prompt:   Ready to Arm
[13:04:52.179][I][vista-alarm:066][pRQtask]: Beeps: 0
[13:05:02.042][D][vista-alarm:554][pRQtask]: (PANEL-->KPD) [13:05:01.60] F7 00 00 FB 10 08 00 1C 08 02 00 00 2A 2A ...
[13:05:02.044][I][vista-alarm:063][pRQtask]: Partition: 1
[13:05:02.046][I][vista-alarm:064][pRQtask]: Prompt: ****DISARMED****
[13:05:02.048][I][vista-alarm:065][pRQtask]: Prompt:   Ready to Arm
[13:05:02.050][I][vista-alarm:066][pRQtask]: Beeps: 0
[13:05:11.940][D][vista-alarm:554][pRQtask]: (PANEL-->KPD) [13:05:11.50] F7 00 00 FB 10 08 00 1C 08 02 00 00 2A 2A ...
[13:05:11.942][I][vista-alarm:063][pRQtask]: Partition: 1
[13:05:11.944][I][vista-alarm:064][pRQtask]: Prompt: ****DISARMED****
[13:05:11.947][I][vista-alarm:065][pRQtask]: Prompt:   Ready to Arm
[13:05:11.949][I][vista-alarm:066][pRQtask]: Beeps: 0
[13:05:13.196][D][vista-alarm:554][pRQtask]: (PANEL-->RFR) [13:05:12.75] FB 02 25 81 5D
[13:05:13.215][D][vista-alarm:554][pRQtask]: (RFR-->PANEL) [13:05:12.77] 00 21 00 DF
[13:05:14.196][D][vista-alarm:554][pRQtask]: (PANEL-->RFR) [13:05:13.75] FB 02 20 82 61
[13:05:14.203][D][vista-alarm:554][pRQtask]: (RFR-->PANEL) [13:05:13.76] 00 24 03 D9
[13:05:17.180][D][vista-alarm:554][pRQtask]: (PANEL-->LRR) [13:05:16.74] F9 03 02 53 AF
[13:05:17.199][D][vista-alarm:554][pRQtask]: (LRR-->PANEL) [13:05:16.76] 43 04 00 60 00 59
[13:05:21.839][D][vista-alarm:554][pRQtask]: (PANEL-->KPD) [13:05:21.40] F7 00 00 FB 10 08 00 1C 08 02 00 00 2A 2A ...
[13:05:21.842][I][vista-alarm:063][pRQtask]: Partition: 1
[13:05:21.844][I][vista-alarm:064][pRQtask]: Prompt: ****DISARMED****
[13:05:21.846][I][vista-alarm:065][pRQtask]: Prompt:   Ready to Arm
[13:05:21.848][I][vista-alarm:066][pRQtask]: Beeps: 0
[13:05:22.196][D][vista-alarm:554][pRQtask]: (PANEL-->EXP) [13:05:21.75] FA 01 04 25 F7 E5
[13:05:22.216][D][vista-alarm:554][pRQtask]: (EXP-->PANEL) [13:05:21.78] F0 31 00 00 DF
```

### Example log output 1 minute after startup with debug_pulsing enabled on Vista-20p panel:
```
[10:06:37][E][vistabus:388][uart_rx_tx_task]: Collecting pulse pattern at 61412112
[10:06:37][D][esp-idf:000][uart_rx_tx_task]: I (61691) gpio: GPIO[22]| InputEn: 1| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
[10:06:37]
[10:06:37][I][vistabus:161][uart_rx_tx_task]: Received 4 symbols
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 13037  High Duration(us) 02007
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 01019  High Duration(us) 02007
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 01019  High Duration(us) 02006
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 01020  High Duration(us) 00000
[10:06:37][D][esp-idf:000][uart_rx_tx_task]: I (62010) gpio: GPIO[22]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
[10:06:37]
[10:06:37][E][vistabus:388][uart_rx_tx_task]: Collecting pulse pattern at 61742110
[10:06:37][D][esp-idf:000][uart_rx_tx_task]: I (62021) gpio: GPIO[22]| InputEn: 1| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
[10:06:37]
[10:06:37][I][vistabus:161][uart_rx_tx_task]: Received 18 symbols
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 06051  High Duration(us) 06036
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 01016  High Duration(us) 00847
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00433  High Duration(us) 00420
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 02139  High Duration(us) 00421
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00716  High Duration(us) 00201
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00215  High Duration(us) 01451
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00635  High Duration(us) 00618
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00215  High Duration(us) 01034
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00635  High Duration(us) 00201
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00215  High Duration(us) 00202
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00214  High Duration(us) 00410
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00215  High Duration(us) 00410
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00634  High Duration(us) 00202
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00631  High Duration(us) 00202
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 01467  High Duration(us) 00202
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00215  High Duration(us) 00202
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 00214  High Duration(us) 00410
[10:06:37][I][vistabus:167][uart_rx_tx_task]: Low Duration(us): 01243  High Duration(us) 00000
[10:06:37][D][esp-idf:000][uart_rx_tx_task]: I (62260) gpio: GPIO[22]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0
```

### Example log output for working Vista20SE installation on legacy protocol:
```
[20:17:26.197][D][text_sensor:120][pRQtask]: 'Line1' >> '****DISARMED****'
[20:17:26.197][D][text_sensor:120][pRQtask]: 'Line2' >> '  READY TO ARM  '
[20:17:26.197][I][vista-alarm:113][pRQtask]: Partition: 1
[20:17:26.201][I][vista-alarm:115][pRQtask]: Prompt: ****DISARMED****
[20:17:26.201][I][vista-alarm:116][pRQtask]: Prompt:   READY TO ARM  
[20:17:26.201][I][vista-alarm:117][pRQtask]: Beeps: 0
[20:17:27.643][D][vista-alarm:386]: Writing keys: 12 to partition 1
[20:17:27.994][D][vista-alarm:537][pRQtask]: (KPDL-->PANEL) [20:17:27.353] 01 01 01 
[20:17:28.338][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:27.701] 00 00 5C 08 00 
[20:17:28.575][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:27.931] FE 53 59 53 54 
[20:17:28.815][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:28.167] FF 45 4D 20 4C 
[20:17:29.040][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:28.403] FF 4F 20 42 41 
[20:17:29.275][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:28.638] FF 54 20 20 20 
[20:17:29.531][D][vista-alarm:537][pRQtask]: (KPDL-->PANEL) [20:17:28.894] 02 
[20:17:29.555][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:28.918] FE 03 20 20 20 
[20:17:29.632][D][vista-alarm:537][pRQtask]: (KPDL-->PANEL) [20:17:28.990] 02 02 02 
[20:17:29.793][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:29.156] FE 03 20 20 20 
[20:17:30.029][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:29.392] FF 20 20 20 20 
[20:17:30.265][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:29.627] FF 20 20 20 20 
[20:17:32.222][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:31.585] 00 00 5C 08 00 
[20:17:32.453][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:31.815] FE 2A 2A 2A 2A 
[20:17:32.690][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:32.051] FF 44 49 53 41 
[20:17:32.924][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:32.286] FF 52 4D 45 44 
[20:17:33.159][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:32.522] FF 2A 2A 2A 2A 
[20:17:33.442][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:32.805] FF 20 20 52 45 
[20:17:33.685][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:33.040] FF 41 44 59 20 
[20:17:33.915][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:33.276] FF 54 4F 20 41 
[20:17:34.151][D][vista-alarm:537][pRQtask]: (PANEL-->KPDL) [20:17:33.511] FF 52 4D 20 20 
[20:17:34.152][D][text_sensor:120][pRQtask]: 'Line1' >> '****DISARMED****'
[20:17:34.152][D][text_sensor:120][pRQtask]: 'Line2' >> '  READY TO ARM  '
[20:17:34.152][I][vista-alarm:113][pRQtask]: Partition: 1
[20:17:34.156][I][vista-alarm:115][pRQtask]: Prompt: ****DISARMED****
[20:17:34.156][I][vista-alarm:116][pRQtask]: Prompt:   READY TO ARM  
[20:17:34.160][I][vista-alarm:117][pRQtask]: Beeps: 0
```