# Honeywell Resideo Vista ECP ESPHome IDF external component
- 
## This version is a highly modified FORK of the vista-ecp project!!

Original project:  https://github.com/Dilbert66/esphome-vistaECP

Refer to original project page for background and details on installation and circuit design.
  
This project compiles using ESP-IDF rather than Arduino framework in ESPHome.

Fork is tailored for the needs of my system and is shared here for the benefits of any others that might want to use it.  No warranty is expressed or implied with the software contained herein.

Key differences from original project:
- Efficient use of limited CPU cycles through use of hardware UART and FreeRTOS multitasking. This includes complete fidelity of packet response handling including 2400 baud preamble bytes.
- OTA updates and OTA logging work reliably without requiring disabling of keybus interaction.
- Arduino dependency removed and refactored for ESP-IDF v5.3. Works with newer espressif cores such as C6.
- Relay board emulation has been removed. Expander emulation remains.
- Config refactored with validation. Examine examples for specifying sensors as these are different from original project.
- Sensors refactored to work with modified config approach.
- Zone emulation specifier in yaml config automatically enables expander board emulation on appropriate address and corresponding group
    of eight zone numbers.  No need to explictly declare expander address in YAML.
- Refactored to support workflow associated with Vistabus class using FreeRTOS tasks for UART comm and intertask comm via FreeRTOS Queues.
- Use of separate FreeRTOS task for nearly all command packet processing.  Esphome Loop used only to verify connection to panel.  No more "Component xx took a long time for an operation" errors in log.
- Targeted only towards ESPHome API.  Stand-alone MQTT is removed in this version.
- Limited testing of expanded functionality such as AUI command handling.  My panel does not
    seem to respond to AUI commands from either original project or this forked version.
- Web server interface component not integrated as is done in the upstream version.
- Uses one or two (if monitoring TX wire for RF messages etc.) hardware UARTS on the ESP32 family rather than software GPIO bit-banging.
- Will not work with older ESP8266. Limited testing on single core ESP32 family devices such as S2/C3/C6.

**⚠️Caution:  There may be features / capabilities carried over from original project but unused by me that are not tested.
One specific includes Long Range Radio emulation.  My system has an actual LRR and I didn't want to disconnect to test this.  
   If you wish to use this fork and find something misbehaving, please let me know and I'll do my best to fix it!

