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

CONF_ACCESSCODE="accesscode"
CONF_DEFAULTPARTITION="defaultpartition"
CONF_DEBUGLEVEL="vistadebuglevel"
CONF_KEYPAD1="keypadaddr1"
CONF_KEYPAD2="keypadaddr2"
CONF_KEYPAD3="keypadaddr3"
CONF_AUIADDR="auiaddr"
CONF_RXPIN="rxpin"
CONF_TXPIN="txpin"
CONF_MONITORPIN="monitorpin"
CONF_UART1="uart1"
CONF_UART2="uart2"
CONF_EXPANDER1="expanderaddr1"
CONF_EXPANDER2="expanderaddr2"
CONF_RELAY1="relayaddr1"
CONF_RELAY2="relayaddr2"
CONF_RELAY3="relayaddr3"
CONF_RELAY4="relayaddr4"
CONF_TTL="ttl"
CONF_QUICKARM="quickarm"
CONF_LRR="lrrsupervisor"
CONF_CLEAN="clean_build"

def validate_keypads(config):
    if (config[CONF_KEYPAD1] == 0 
        and config[CONF_KEYPAD2] == 0
        and config[CONF_KEYPAD3] == 0):
        raise cv.Invalid("All partition keypads assigned 0. At least one must be assigned a valid address")
    return config

def validate_c6_uarts(config):
    if (esp32.get_esp32_variant() == esp32.const.VARIANT_ESP32C6 
        and (config[CONF_UART1] == 2 
        or config[CONF_UART2] == 2)):
        raise cv.Invalid("LP UART on ESP32-C6 is not currently supported.  Please use HW UARTs 0/1")
    return config

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AlarmComponent),
            cv.Optional(CONF_ACCESSCODE,default=""): cv.string,
            cv.Optional(CONF_DEFAULTPARTITION, default=1): cv.int_range(min=1, max=8),
            cv.Optional(CONF_DEBUGLEVEL): cv.int_, 
            cv.Optional(CONF_KEYPAD1,default=17): cv.int_, 
            cv.Optional(CONF_KEYPAD2,default=0): cv.int_, 
            cv.Optional(CONF_KEYPAD3,default=0): cv.int_, 
            cv.Optional(CONF_AUIADDR,default=0): cv.int_,
            cv.Required(CONF_RXPIN): cv.int_, 
            cv.Required(CONF_TXPIN): cv.int_,
            cv.Required(CONF_UART1): cv.int_, 
            cv.Optional(CONF_MONITORPIN, default=-1): cv.int_,
            cv.Optional(CONF_UART2, default=-1): cv.int_,
            cv.Optional(CONF_TTL,default=30): cv.int_, 
            cv.Optional(CONF_QUICKARM): cv.boolean, 
            cv.Optional(CONF_LRR): cv.boolean, 
            cv.Optional(CONF_CLEAN,default='false'): cv.boolean,     
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_keypads,
    validate_c6_uarts,
)



async def to_code(config):

    esp32.add_idf_sdkconfig_option("CONFIG_FREERTOS_HZ", 1000)
    esp32.add_idf_sdkconfig_option("CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD", True) 
    old_dir = CORE.relative_build_path("src")    
    if config[CONF_CLEAN] or os.path.exists(old_dir+'/vistaalarm.h'):
        real_clean_build()
    var = cg.new_Pvariable(config[CONF_ID],config[CONF_KEYPAD1],config[CONF_RXPIN],config[CONF_TXPIN],config[CONF_UART1],config[CONF_MONITORPIN],config[CONF_UART2])
    
    if CONF_ACCESSCODE in config:
        cg.add(var.set_accessCode(config[CONF_ACCESSCODE]));
    if CONF_DEFAULTPARTITION in config:
        cg.add(var.set_defaultPartition(config[CONF_DEFAULTPARTITION]));
    if CONF_DEBUGLEVEL in config:
        cg.add(var.set_debug(config[CONF_DEBUGLEVEL]));
    if config[CONF_KEYPAD1] != 0:
        cg.add(var.set_partitionKeypad(1,config[CONF_KEYPAD1]));
    if config[CONF_KEYPAD2] != 0:
        cg.add(var.set_partitionKeypad(2,config[CONF_KEYPAD2]));
    if config[CONF_KEYPAD3] != 0:
        cg.add(var.set_partitionKeypad(3,config[CONF_KEYPAD3]));
    cg.add(var.initialize_partition_sensors());
    cg.add(var.set_ttl(config[CONF_TTL]));        
    if CONF_QUICKARM in config:
        cg.add(var.set_quickArm(config[CONF_QUICKARM]));        
    if CONF_LRR in config:
        cg.add(var.set_lrrSupervisor(config[CONF_LRR]));      
    if CONF_AUIADDR in config:
        cg.add(var.set_auiaddr(config[CONF_AUIADDR]));
    await cg.register_component(var, config)
    
def real_clean_build():
    import shutil
    build_dir = CORE.relative_build_path("")
    if os.path.isdir(build_dir):
        _LOGGER.info("Deleting %s", build_dir)
        shutil.rmtree(build_dir)

        
            
    