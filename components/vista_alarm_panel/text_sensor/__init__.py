# Copyright (C) 2020 Alain Turbide
# Copyright (C) 2025-2026 Tim Pletcher
#
# This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
# (https://github.com/Dilbert66/esphome-vistaECP).
#
# Licensed under the GNU Lesser General Public License v2.1.
# See COPYING.LESSER in the project root for details.

import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv

from .. import AlarmComponent, alarm_panel_ns

DEPENDENCIES = ["vista_alarm_panel"]
VistaTextSensor = alarm_panel_ns.class_("VistaTextSensor", text_sensor.TextSensor)

CONF_ALARM_ID = "alarm_id"

TTYPES = [
    "ZONE",
    "SYSTEM_STATUS",
    "LRR_MESSAGES",
    "RF_MESSAGES",
    "LINE1",
    "LINE2",
    "ZONE_STATUS",
    "BEEPS",
]

CONF_ZONE = "zone"
CONF_PARTITION = "partition"
CONF_TYPE = "type"


def _validate(value):
    if value[CONF_TYPE] == "ZONE" and (
        CONF_ZONE not in value or CONF_PARTITION not in value
    ):
        raise cv.Invalid('type: "zone" requires both zone: and partition:')
    if value[CONF_TYPE] != "ZONE" and CONF_ZONE in value:
        raise cv.Invalid('zone: parameter only valid with type: "zone"')
    return value


CONFIG_SCHEMA = cv.All(
    text_sensor.text_sensor_schema(VistaTextSensor).extend(
        {
            cv.GenerateID(CONF_ALARM_ID): cv.use_id(AlarmComponent),
            cv.Optional(CONF_PARTITION): cv.int_range(min=1, max=8),
            cv.Optional(CONF_ZONE): cv.int_range(min=1, max=128),
            cv.Required(CONF_TYPE): cv.one_of(*TTYPES, upper=True),
        }
    ),
    _validate,
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    hub = await cg.get_variable(config[CONF_ALARM_ID])
    await cg.register_parented(var, hub)

    if CONF_ZONE in config:
        cg.add(hub.register_zone_text(var, config[CONF_PARTITION], config[CONF_ZONE]))
    else:
        partition = config.get(CONF_PARTITION, 0)
        cg.add(hub.register_text_sensor(var, partition, config[CONF_TYPE]))
