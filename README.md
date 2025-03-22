# Honeywell Resideo Vista ECP ESPHome IDF external component
 
## This version is a highly modified FORK of the vista-ecp project!!

Original project:  https://github.com/Dilbert66/esphome-vistaECP

Refer to original project page for background and details on installation and circuit design.
  
Note that this fork compiles using ESP-IDF rather than Arduino framework in ESPHome. ESPHome Arduino is currently stuck on ESP-IDF v4.4 base for forseeable future. Many of my newer gen ESP32 (C3, C6, S3) show improved connection stability on ESP-IDF v5+.

Fork is shared here for the benefits of any others that might want to use it.  No warranty is expressed or implied with the software contained herein as explained in the license documentation.

### Key differences from original project:
- Efficient use of limited embedded CPU cycles through use of hardware UART and FreeRTOS multitasking. This includes complete fidelity of packet response handling including 2400 baud preamble bytes.
- OTA updates and OTA logging work reliably without requiring disabling of keybus interaction. 
- Arduino dependency removed and refactored for ESP-IDF v5.3. Works with newer espressif cores such as C6.
- Relay board emulation has been removed. Expander emulation remains.
- Config refactored with validation. Examine examples for specifying sensors as these are different from original project.
- Sensors refactored to work with modified config approach.
- Zone emulation specifier in yaml config automatically enables expander board emulation on appropriate address and corresponding group
    of eight zone numbers.  No need to explictly declare expander address in YAML.
- Refactored to support workflow associated with Vistabus class using FreeRTOS tasks for UART comm and intertask comm via FreeRTOS Queues.
- Use of separate FreeRTOS task for nearly all command packet processing.  Esphome Loop used only to verify connection to panel.  No more "Component xx took a long time for an operation" errors in log!
- Targeted only towards ESPHome API.  Stand-alone MQTT is removed.
- Web server interface component not integrated as is done in the upstream version.
- Uses one or two (if monitoring TX wire for RF messages etc.) hardware UARTS on the ESP32 family rather than software GPIO bit-banging.
- Will not work with older ESP8266. Limited testing on single core ESP32 family devices such as S2/C3/C6.
- Lots of unused residual code, bitwise operations, and variable handling cleaned up.

>[!Warning]  There may be features / capabilities carried over from original project but unused by me that are not tested.
> Specifics include:
> - Long Range Radio emulation
> - AUI command handling.  
>     - My system has an actual LRR and I didn't want to disconnect to test this. My former Safewatch Pro 3000 doesn't seem to respond to AUI commands as expected with neither the original implementation or this fork.

If you wish to use this fork and find something misbehaving, please let me know and I'll do my best to fix it!

