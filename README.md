# Honeywell Resideo Vista ECP ESPHome IDF external component
 
## This version is a highly modified FORK of the vista-ecp project!!

Original project:  https://github.com/Dilbert66/esphome-vistaECP

Refer to original project page for background and details on installation and circuit design.
  
Note that this fork compiles using ESP-IDF rather than Arduino framework in ESPHome.

Fork is shared here for the benefits of any others that might want to use it.  No warranty is expressed or implied with the software contained herein as explained in the license documentation.

### Key differences from original project:
- Efficient use of limited micro CPU cycles through use of hardware UART and FreeRTOS multitasking. This includes complete fidelity of packet response handling including 2400 baud preamble bytes.
- OTA updates and OTA logging work reliably without requiring disabling of keybus interaction. 
- Arduino dependency removed and refactored for ESP-IDF v5.3+. Works with newer espressif cores such as C6.
- Relay board emulation has been removed. Expander emulation remains.
- Config refactored with validation. Carefully examine the example YAML file (https://github.com/pletch/esphome-vistaECP-idf/blob/idf/vista-ecp-idf.yaml) for specifying sensors and other details as these are different from original project.
- Sensors refactored to work with modified config approach.
- Zone emulation specifier in yaml config automatically enables expander board emulation (e.g. Honeywell 4219) on appropriate address and corresponding group
    of eight zone numbers. 
- Refactored to support workflow associated with Vistabus class using FreeRTOS tasks for UART comm and intertask comm via FreeRTOS Queues.
- Use of separate FreeRTOS task for nearly all command packet processing.  Esphome Loop used only to verify connection to panel.  No need to suppress "Component xx took a long time for an operation" errors in log.
- Targeted only towards ESPHome API.  Stand-alone MQTT is removed.
- Web server interface component not integrated as is done in the upstream version.
- Uses one or two (if monitoring TX wire for RF messages etc.) hardware UARTS on the ESP32 family rather than software GPIO bit-banging.
- Will not work with older ESP8266.
- Lots of unused residual code, bitwise operations, and variable handling cleaned up.
- Capability to temporarily enable RMT module for outputting bus pulse pattern to log for debugging

### ⚠️Caution: There may be features / capabilities carried over from original project but unused by me that are not tested.
Some Specifics include: 
- Long Range Radio emulation
- AUI command handling.  

   My system has an actual LRR and I didn't want to disconnect to test this.My former Safewatch Pro 3000 doesn't seem to respond to AUI commands as expected with neither the original implementation or this fork.


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
- **keypad_addr_1, keypad_addr_2, keypad_addr_3 (Required)**:  Virtual keypad address for partitions 1, 2, & 3. Enable address in alarm panel programming via program fields *190 - *196. Setting value to zero disables. At least one of the three must be defined non-zero.
- **rx_pin (Required, PIN)**: GPIO pin assigned to UART for data receive (yellow line)
- **tx_pin (Required, PIN)**: GPIO pin assigned to UART for data transmit (green line)
- **uart_1 (Required, UART)**: Hardware UART number associated with tx/rx 
- **access_code** (*Optional*): Alarm code used for arming / disarming.  Typically defined in secrets file.
- **aui_addr** (*Optional*, int): AUI address from program field *189 to use for zone status queries (open/close and bypass). Ensure it is not assigned to a real keypad or to Total Connect 2.0. For those panels, you can select auiaddr: 5 or 6. As a final option, you can assign it to the same address as your existing AUI display address of 1. Note: Older vista20 panels only have addresses 1 and 2 while newer will have 1,2,5 and 6. For Vista128,Vista250 commercial panels, ensure the address used is setup as an AUI keypad in program *93, device programming.
- **default_partition** (*Optional*, int): Set to designate main partition number.  Defaults to 1 if not defined.
- **debug_log** (*Optional*, boolean): Set to true to enable additional bus activity logging and print full ecp packet contents to the log.  Global esphome and vista-alarm component (if configured)
logging level must be set to DEBUG or higher in logging component section to output all messages.
- **debug_pulsing** (*Optional*, boolean): Enables use of ESP32 RMT peripheral to capture and output pulse pattern for diagnostics.  Pulse pattern output to log commences 60 seconds after startup and continues indefinitely.  **DO NOT** enable for routine use of component!
- **lrr_supervisor** (*Optional*, boolean): Set to true to enable Long Range Radio emulation for monitoring and decoding status updates. Do not enable if the system is monitored and an actual long range radio is already present in the system. Defaults to false.   
- **monitor_pin** (*Optional*, PIN): GPIO pin to use for monitoring module traffic such as RF or Expanders. Leave undefined or set to -1 to disable.
- **uart_2** (*Optional*, UART): Hardware UART number to use for monitoring module traffic via monitorpin. Automatically disabled if monitorpin set to -1.
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

# example rf wireless zone
  - platform: vista_alarm_panel
    id: z9
    name: "Front Door"
    partition: 1
    zone: 9
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
- **emulated** (*Optional*, boolean) Enable zone emulation through automatic expander board emulation (e.g. Honeywell 4129). Ensure that the zone number selected for emulated zone does not conflict with existing physical boards in your system.  This is useful for associating other gpio on ESP32 or other Home Assistant sensors with alarm panel zone. Defaults to **false** if not defined.
  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Emulated zones 9-16 will enable expander emulation on address 7.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Emulated zones 17-24 will enable expander emulation on address 8.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Emulated zones 25-32 will enable expander emulation on address 9.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Emulated zones 33-40 will enable expander emulation on address 10.  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Emulated zones 41-48 will enable expander emulation on address 11.
- **partition** (*Optional*, int) Partion number associated with zone.
- **rf_loop** (*Optional*, int)  Loop number of RF device (see rf_serial info below)
- **rf_serial** (*Optional*, int) Unique RF serial number of wireless device. Enroll your RF serial devices using the rf_serial and rf_loop parameters. For most devices loop1 is used such as 5800pir, other devices such as 5816 will use loop2. Please refer to your RF device programming (*56 program) to see what loop and zones are assigned to your RF devices.  
&nbsp;&nbsp;&nbsp;!!Do not include leading zeros in rf_serial!!
- **zone** (*Optional*, int):  Panel configured zone number.
  
[non-zone binary status]
- **partition** (*Optional*, int) Partion number associated with status sensor.
- **status_sensor** (*Optional*, string) Valid options are [ready, trouble, bypass, armed_away, armed_instant, armed_night, chime, alarm, fire, ac_power, and battery]. Partition specific indicators will require specifying partition.
    
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


### Example log output for working installation:
```
[19:48:31][D][vista-alarm:509][pRQtask]: (PANEL) [19:48:31] F7 00 00 FB 10 08 00 1C 08 02 00 00 2A 2A 2A 2A 44 ...
[19:48:31][I][vista-alarm:058][pRQtask]: Partition: 1
[19:48:31][I][vista-alarm:059][pRQtask]: Prompt: ****DISARMED****
[19:48:31][I][vista-alarm:060][pRQtask]: Prompt:   Ready to Arm
[19:48:31][I][vista-alarm:061][pRQtask]: Beeps: 0
[19:48:35][D][vista-alarm:366]: Writing keys: 1 to partition 1
[19:48:36][D][vista-alarm:509][pRQtask]: (PANEL) [19:48:35] F6 14
[19:48:36][D][vista-alarm:509][pRQtask]: (EXT-D) [19:48:35] D4 02 01 29
[19:48:36][D][vista-alarm:366]: Writing keys: 2 to partition 1
[19:48:36][D][vista-alarm:509][pRQtask]: (PANEL) [19:48:35] F7 00 00 FB 10 08 00 1C 08 02 00 00 AA 2A 2A 2A 44 ...
[19:48:36][I][vista-alarm:058][pRQtask]: Partition: 1
[19:48:36][I][vista-alarm:059][pRQtask]: Prompt: ****DISARMED****
[19:48:36][I][vista-alarm:060][pRQtask]: Prompt:   Ready to Arm
[19:48:36][I][vista-alarm:061][pRQtask]: Beeps: 0
[19:48:36][D][text_sensor:064][pRQtask]: 'Line1': Sending state '****DISARMED****'
[19:48:36][D][text_sensor:064][pRQtask]: 'Line2': Sending state '  Ready to Arm  '
[19:48:36][D][text_sensor:064][pRQtask]: 'System Status': Sending state 'Disarmed'
[19:48:36][D][text_sensor:064][pRQtask]: 'Zone Status': Sending state ''
[19:48:36][D][vista-alarm:509][pRQtask]: (PANEL) [19:48:35] F6 14
[19:48:36][D][vista-alarm:509][pRQtask]: (EXT-D) [19:48:35] 94 02 02 68
[19:48:39][D][vista-alarm:509][pRQtask]: (PANEL) [19:48:38] FB 02 20 81 62
[19:48:39][D][vista-alarm:509][pRQtask]: (EXT-D) [19:48:38] 00 24 00 DC
[19:48:40][D][vista-alarm:509][pRQtask]: (PANEL) [19:48:39] FB 02 25 60 7E
[19:48:40][D][vista-alarm:509][pRQtask]: (EXT-D) [19:48:39] 00 21 00 DF
[19:48:41][D][vista-alarm:509][pRQtask]: (PANEL) [19:48:40] F7 00 00 FB 10 08 00 1C 08 02 00 00 AA 2A 2A 2A 44 ...
[19:48:41][I][vista-alarm:058][pRQtask]: Partition: 1
[19:48:41][I][vista-alarm:059][pRQtask]: Prompt: ****DISARMED****
[19:48:41][I][vista-alarm:060][pRQtask]: Prompt:   Ready to Arm
[19:48:41][I][vista-alarm:061][pRQtask]: Beeps: 0
[19:48:43][D][vista-alarm:509][pRQtask]: (PANEL) [19:48:42] F9 03 02 53 AF
[19:48:43][D][vista-alarm:509][pRQtask]: (EXT-D) [19:48:42] 43 04 00 60 00 59
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