import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE
from esphome.components import esp32
import os
import logging
from esphome.const import (
    CONF_ID
)

_LOGGER = logging.getLogger(__name__)

alarm_panel_ns = cg.esphome_ns.namespace('alarm_panel')
AlarmComponent = alarm_panel_ns.class_('vistaECPHome', cg.PollingComponent)

CONF_ACCESSCODE="access_code"
CONF_DEFAULTPARTITION="default_partition"
CONF_DEBUGLOG="debug_logging"
CONF_DEBUGPULSE="debug_pulsing"
CONF_KEYPAD1="keypad_addr_1"
CONF_KEYPAD2="keypad_addr_2"
CONF_KEYPAD3="keypad_addr_3"
CONF_KEYPAD4="keypad_addr_4"
CONF_KEYPAD5="keypad_addr_5"
CONF_KEYPAD6="keypad_addr_6"
CONF_KEYPAD7="keypad_addr_7"
CONF_KEYPAD8="keypad_addr_8"
CONF_AUIADDR="aui_addr"
CONF_AUTOCLOCKSYNC="aui_auto_clock_sync"
CONF_RXPIN="rx_pin"
CONF_TXPIN="tx_pin"
CONF_MONITORPIN="monitor_pin"
CONF_UART1="uart_1"
CONF_UART2="uart_2"
CONF_TTL="ttl"
CONF_QUICKARM="quickarm"
CONF_LRR="lrr_supervisor"
CONF_RFR="rf_receiver_emulation"
CONF_RFRADDR="rf_receiver_addr"

def validate_keypads(config):
    if (config[CONF_KEYPAD1] == 0 
        and config[CONF_KEYPAD2] == 0
        and config[CONF_KEYPAD3] == 0
        and config[CONF_KEYPAD4] == 0
        and config[CONF_KEYPAD5] == 0
        and config[CONF_KEYPAD6] == 0
        and config[CONF_KEYPAD7] == 0
        and config[CONF_KEYPAD8] == 0):
        raise cv.Invalid("All partition keypads assigned 0. At minimum one 'keypad_addr_#' must be defined in YAML and assigned a valid address")
    return config

def validate_c6_uarts(config):
    if (esp32.get_esp32_variant() == esp32.const.VARIANT_ESP32C6 
        and (config[CONF_UART1] == 2 
        or config[CONF_UART2] == 2)):
        raise cv.Invalid("LP UART on ESP32-C6 is not currently supported.  Please use HW UARTs 0/1")
    return config

def validate_auiaddr(config):
    valid_values = [0,1,2,5,6]
    if (config[CONF_AUIADDR] not in valid_values):
        raise cv.Invalid("Invalid value for aui_addr. Valid enabled values are 1,2,5,6 or 0 if disabled")
    return config

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AlarmComponent),
            cv.Optional(CONF_ACCESSCODE,default=""): cv.string,
            cv.Optional(CONF_DEFAULTPARTITION, default=1): cv.int_range(min=1, max=8),
            cv.Optional(CONF_DEBUGLOG,default=False): cv.boolean,
            cv.Optional(CONF_DEBUGPULSE,default=False): cv.boolean,
            cv.Optional(CONF_KEYPAD1,default=0): cv.int_, 
            cv.Optional(CONF_KEYPAD2,default=0): cv.int_, 
            cv.Optional(CONF_KEYPAD3,default=0): cv.int_,
            cv.Optional(CONF_KEYPAD4,default=0): cv.int_, 
            cv.Optional(CONF_KEYPAD5,default=0): cv.int_, 
            cv.Optional(CONF_KEYPAD6,default=0): cv.int_,
            cv.Optional(CONF_KEYPAD7,default=0): cv.int_, 
            cv.Optional(CONF_KEYPAD8,default=0): cv.int_,   
            cv.Optional(CONF_AUIADDR,default=0): cv.int_,
            cv.Optional(CONF_AUTOCLOCKSYNC): cv.boolean,
            cv.Required(CONF_RXPIN): cv.int_, 
            cv.Required(CONF_TXPIN): cv.int_,
            cv.Required(CONF_UART1): cv.int_, 
            cv.Optional(CONF_MONITORPIN, default=-1): cv.int_,
            cv.Optional(CONF_UART2, default=-1): cv.int_,
            cv.Optional(CONF_TTL,default=30): cv.int_, 
            cv.Optional(CONF_QUICKARM): cv.boolean, 
            cv.Optional(CONF_LRR): cv.boolean, 
            cv.Optional(CONF_RFR): cv.boolean,
            cv.Optional(CONF_RFRADDR, default=0): cv.int_,  
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_keypads,
    validate_c6_uarts,
    validate_auiaddr
)



async def to_code(config):

    esp32.add_idf_sdkconfig_option("CONFIG_FREERTOS_HZ", 1000)
    esp32.add_idf_sdkconfig_option("CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD", True)

    var = cg.new_Pvariable(config[CONF_ID],config[CONF_KEYPAD1],config[CONF_RXPIN],config[CONF_TXPIN],config[CONF_UART1],config[CONF_MONITORPIN],config[CONF_UART2])
    
    if CONF_ACCESSCODE in config:
        cg.add(var.set_accessCode(config[CONF_ACCESSCODE]))
    if CONF_DEFAULTPARTITION in config:
        cg.add(var.set_defaultPartition(config[CONF_DEFAULTPARTITION]))
    if config[CONF_DEBUGLOG]:
        cg.add_define("DEBUG_LOG")
    if config[CONF_DEBUGPULSE]:
        cg.add_define("DEBUG_PULSE")
    if config[CONF_KEYPAD1] != 0:
        cg.add(var.set_partitionKeypad(1,config[CONF_KEYPAD1]))
    if config[CONF_KEYPAD2] != 0:
        cg.add(var.set_partitionKeypad(2,config[CONF_KEYPAD2]))
    if config[CONF_KEYPAD3] != 0:
        cg.add(var.set_partitionKeypad(3,config[CONF_KEYPAD3]))
    if config[CONF_KEYPAD4] != 0:
        cg.add(var.set_partitionKeypad(4,config[CONF_KEYPAD4]))
    if config[CONF_KEYPAD5] != 0:
        cg.add(var.set_partitionKeypad(5,config[CONF_KEYPAD5]))
    if config[CONF_KEYPAD6] != 0:
        cg.add(var.set_partitionKeypad(6,config[CONF_KEYPAD6]))
    if config[CONF_KEYPAD7] != 0:
        cg.add(var.set_partitionKeypad(7,config[CONF_KEYPAD7]))
    if config[CONF_KEYPAD8] != 0:
        cg.add(var.set_partitionKeypad(8,config[CONF_KEYPAD8]))
    cg.add(var.initialize_partition_sensors())
    cg.add(var.set_ttl(config[CONF_TTL]))       
    if CONF_QUICKARM in config:
        cg.add(var.set_quickArm(config[CONF_QUICKARM]))       
    if CONF_LRR in config:
        cg.add(var.set_lrrSupervisor(config[CONF_LRR]))
    if CONF_RFR in config:
        cg.add(var.set_rfrEmulation(config[CONF_RFR], config[CONF_RFRADDR]))        
    if CONF_AUIADDR in config:
        cg.add(var.set_auiaddr(config[CONF_AUIADDR]))
    if CONF_AUTOCLOCKSYNC in config:
        cg.add(var.set_clocksync(config[CONF_AUTOCLOCKSYNC]))
    await cg.register_component(var, config)
        
            
    