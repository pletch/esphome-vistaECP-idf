# Copyright (C) 2020 Alain Turbide
# Copyright (C) 2025-2026 Tim Pletcher
#
# This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
# (https://github.com/Dilbert66/esphome-vistaECP).
#
# Licensed under the GNU Lesser General Public License v2.1.
# See COPYING.LESSER in the project root for details.

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from .. import (
    alarm_panel_ns,
    AlarmComponent,
)
DEPENDENCIES = ["vista_alarm_panel"]
VistaBinarySensor = alarm_panel_ns.class_("VistaBinarySensor", binary_sensor.BinarySensor)

CONF_ALARM_ID = "alarm_id"

STATUS_SENSORS = [
    "READY",
    "TROUBLE",
    "BYPASS",
    "ARMED",
    "ARMED_AWAY",
    "ARMED_STAY",
    "ARMED_INSTANT",
    "ARMED_NIGHT",
    "AC_POWER",
    "CHIME",
    "ALARM",
    "BATTERY",
    "FIRE"
]

PANEL_SENSORS = [
    "AC_POWER",
    "BATTERY"
]

CONF_ZONE = "zone"
CONF_PARTITION = "partition"
CONF_RFSERIAL = "rf_serial"
CONF_RFLOOP = "rf_loop"
CONF_STATUS_SENSOR = "status_indicator"
CONF_EMULATED_ZONE = "emulated"

def _validate(value):
    if CONF_ZONE in value:
        if ((CONF_RFSERIAL in value and CONF_RFLOOP not in value ) or 
            (CONF_RFLOOP in value and CONF_RFSERIAL not in value )) :
            raise cv.Invalid("rf_serial: and rf_loop: must both be specified for RF Zone")
    if CONF_ZONE in value and CONF_STATUS_SENSOR in value :
        raise cv.Invalid("Valid sensor config must include only one of either zone: or status_indicator:. Both are specified.")
    if CONF_ZONE not in value and CONF_STATUS_SENSOR not in value :
        raise cv.Invalid("Valid sensor config must include either zone: or status_indicator:. Neither are specified.")
    if CONF_STATUS_SENSOR in value and value[CONF_STATUS_SENSOR] not in PANEL_SENSORS and CONF_PARTITION not in value:
        raise cv.Invalid(str(value[CONF_STATUS_SENSOR]) + " must be specified with partition value")
    if CONF_ZONE in value and CONF_EMULATED_ZONE in value and value[CONF_ZONE] < 8:
        raise cv.Invalid("zone: value must be greater than 8.  First 8 zones are used for panel hardwired zones")
    return value

CONFIG_SCHEMA = cv.All(binary_sensor.binary_sensor_schema(VistaBinarySensor).extend(
        {
            cv.GenerateID(CONF_ALARM_ID): cv.use_id(AlarmComponent),
            cv.Optional(CONF_PARTITION): cv.int_range(min=1, max=8),
            cv.Optional(CONF_ZONE): cv.int_range(min=1, max=128),
            cv.Optional(CONF_RFSERIAL): cv.int_range(min=1, max=1048575),
            cv.Optional(CONF_EMULATED_ZONE): cv.boolean,
            cv.Optional(CONF_RFLOOP): cv.int_range(min=1, max=4),
            cv.Optional(CONF_STATUS_SENSOR): cv.one_of(*STATUS_SENSORS, upper=True),
        }
    ),
    _validate,
)

async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    hub = await cg.get_variable(config[CONF_ALARM_ID])
    await cg.register_parented(var, hub)

    if CONF_ZONE in config:
        if CONF_RFSERIAL in config and CONF_RFLOOP in config:
            if CONF_EMULATED_ZONE in config:
                cg.add(hub.register_zone(var,config[CONF_PARTITION],config[CONF_ZONE],config[CONF_RFSERIAL],config[CONF_RFLOOP],True))
            else:
                cg.add(hub.register_zone(var,config[CONF_PARTITION],config[CONF_ZONE],config[CONF_RFSERIAL],config[CONF_RFLOOP],False))
        else:     
            if CONF_EMULATED_ZONE in config and config[CONF_EMULATED_ZONE] == True:
                cg.add(hub.register_zone(var,config[CONF_PARTITION],config[CONF_ZONE],0,0,True))
                cg.add(hub.register_expander(config[CONF_ZONE]))
            else:
                cg.add(hub.register_zone(var,config[CONF_PARTITION],config[CONF_ZONE],0,0,False))         
    elif (config[CONF_STATUS_SENSOR] == "AC_POWER"):
        cg.add(hub.register_ac(var))
    elif (config[CONF_STATUS_SENSOR] == "BATTERY"):
        cg.add(hub.register_bat(var))
    else:
        cg.add(hub.register_status_sensor(var,config[CONF_PARTITION], config[CONF_STATUS_SENSOR]))