# Copyright (C) 2020 Alain Turbide
# Copyright (C) 2025-2026 Tim Pletcher
#
# This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
# (https://github.com/Dilbert66/esphome-vistaECP).
#
# Licensed under the GNU Lesser General Public License v2.1.
# See COPYING.LESSER in the project root for details.

import logging

import esphome.codegen as cg
from esphome.components import esp32, text_sensor as ts_mod
from esphome.components.ota import request_ota_state_listeners
import esphome.config_validation as cv
from esphome.const import (
    CONF_DISABLED_BY_DEFAULT,
    CONF_ENTITY_CATEGORY,
    CONF_ICON,
    CONF_ID,
    CONF_NAME,
)
from esphome.core import CORE, ID

# Declare dependency on the OTA component so that ESPHome ensures it is
# initialised before this component and get_global_ota_callback() is valid.
# request_ota_state_listeners() enables USE_OTA_STATE_LISTENER so that
# OTAGlobalStateListener and get_global_ota_callback() are compiled in.
DEPENDENCIES = ["ota"]

_LOGGER = logging.getLogger(__name__)

alarm_panel_ns = cg.esphome_ns.namespace("alarm_panel")
AlarmComponent = alarm_panel_ns.class_("VistaESPHome", cg.PollingComponent)

CONF_ACCESSCODE = "access_code"
CONF_DEFAULTPARTITION = "default_partition"
CONF_DEBUGLOG = "debug_logging"
CONF_DEBUGPULSE = "debug_pulsing"
CONF_KEYPAD1 = "keypad_addr_1"
CONF_KEYPAD2 = "keypad_addr_2"
CONF_KEYPAD3 = "keypad_addr_3"
CONF_KEYPAD4 = "keypad_addr_4"
CONF_KEYPAD5 = "keypad_addr_5"
CONF_KEYPAD6 = "keypad_addr_6"
CONF_KEYPAD7 = "keypad_addr_7"
CONF_KEYPAD8 = "keypad_addr_8"
CONF_AUIADDR = "aui_addr"
CONF_AUTOCLOCKSYNC = "aui_auto_clock_sync"
CONF_RXPIN = "rx_pin"
CONF_TXPIN = "tx_pin"
CONF_MONITORPIN = "monitor_pin"
CONF_UART1 = "uart_1"
CONF_UART2 = "uart_2"
CONF_TTL = "ttl"
CONF_QUICKARM = "quickarm"
CONF_LRR = "lrr_supervisor"
CONF_RFR = "rf_receiver_emulation"
CONF_RFRADDR = "rf_receiver_addr"
CONF_LEGACYPROTOCOL = "legacy_protocol"
CONF_CC1101_MOSI = "cc1101_mosi_pin"
CONF_CC1101_MISO = "cc1101_miso_pin"
CONF_CC1101_SCK = "cc1101_sck_pin"
CONF_CC1101_CSN = "cc1101_csn_pin"
CONF_CC1101_GDO0 = "cc1101_gdo0_pin"
CONF_CC1101_SPI_BUS = "cc1101_spi_bus"
CONF_RF_HEARTBEAT_EXTERNAL = "rf_heartbeat_external"
CONF_CC1101_RSSI_THRESHOLD = "cc1101_rssi_threshold"

CC1101_PINS = [
    CONF_CC1101_MOSI,
    CONF_CC1101_MISO,
    CONF_CC1101_SCK,
    CONF_CC1101_CSN,
    CONF_CC1101_GDO0,
]


KEYPAD_ADDR_KEYS = [
    CONF_KEYPAD1,
    CONF_KEYPAD2,
    CONF_KEYPAD3,
    CONF_KEYPAD4,
    CONF_KEYPAD5,
    CONF_KEYPAD6,
    CONF_KEYPAD7,
    CONF_KEYPAD8,
]


def validate_keypads(config):
    if all(config[key] == 0 for key in KEYPAD_ADDR_KEYS):
        raise cv.Invalid(
            "All partition keypads assigned 0. At minimum one 'keypad_addr_#' must be defined in YAML and assigned a valid address"
        )
    return config


def validate_c6_uarts(config):
    if esp32.get_esp32_variant() == esp32.const.VARIANT_ESP32C6 and (
        config[CONF_UART1] == 2 or config[CONF_UART2] == 2
    ):
        raise cv.Invalid(
            "LP UART on ESP32-C6 is not currently supported.  Please use HW UARTs 0/1"
        )
    return config


def validate_auiaddr(config):
    valid_values = [0, 1, 2, 5, 6]
    if config[CONF_AUIADDR] not in valid_values:
        raise cv.Invalid(
            "Invalid value for aui_addr. Valid enabled values are 1,2,5,6 or 0 if explicitly disabled"
        )
    if config.get(CONF_LEGACYPROTOCOL, False) and config[CONF_AUIADDR] != 0:
        raise cv.Invalid(
            "aui_addr is not supported on legacy SE-series panels. Set aui_addr to 0 or remove it when legacy_protocol is enabled."
        )
    return config


def validate_clocksync(config):
    if config.get(CONF_AUTOCLOCKSYNC) and config[CONF_AUIADDR] == 0:
        raise cv.Invalid(
            "aui_auto_clock_sync: true requires a non-zero aui_addr (1, 2, 5, or 6)"
        )
    if config.get(CONF_AUTOCLOCKSYNC) and config.get(CONF_LEGACYPROTOCOL, False):
        raise cv.Invalid(
            "aui_auto_clock_sync is not supported on legacy SE-series panels."
        )
    return config


def validate_cc1101(config):
    present = [p for p in CC1101_PINS if p in config]
    if present and len(present) != len(CC1101_PINS):
        missing = [p for p in CC1101_PINS if p not in config]
        raise cv.Invalid(
            f"CC1101: all five SPI pins must be specified together. Missing: {missing}"
        )
    # Skip rf_receiver_emulation check when debug_pulsing is active — CC1101 will
    # be disabled at code-generation time because both features share the RMT peripheral.
    if (
        present
        and not config.get(CONF_DEBUGPULSE, False)
        and not config.get(CONF_RFR, False)
    ):
        raise cv.Invalid(
            "CC1101 pins are configured but rf_receiver_emulation is not set to true. "
            "The CC1101 requires an emulated RF receiver device on the ECP bus."
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AlarmComponent),
            cv.Optional(CONF_ACCESSCODE, default=""): cv.string,
            cv.Optional(CONF_DEFAULTPARTITION, default=1): cv.int_range(min=1, max=8),
            cv.Optional(CONF_DEBUGLOG, default=False): cv.boolean,
            cv.Optional(CONF_DEBUGPULSE, default=False): cv.boolean,
            cv.Optional(CONF_KEYPAD1, default=0): cv.int_,
            cv.Optional(CONF_KEYPAD2, default=0): cv.int_,
            cv.Optional(CONF_KEYPAD3, default=0): cv.int_,
            cv.Optional(CONF_KEYPAD4, default=0): cv.int_,
            cv.Optional(CONF_KEYPAD5, default=0): cv.int_,
            cv.Optional(CONF_KEYPAD6, default=0): cv.int_,
            cv.Optional(CONF_KEYPAD7, default=0): cv.int_,
            cv.Optional(CONF_KEYPAD8, default=0): cv.int_,
            cv.Optional(CONF_AUIADDR, default=0): cv.int_,
            cv.Optional(CONF_AUTOCLOCKSYNC): cv.boolean,
            cv.Required(CONF_RXPIN): cv.int_,
            cv.Required(CONF_TXPIN): cv.int_,
            cv.Required(CONF_UART1): cv.int_,
            cv.Optional(CONF_MONITORPIN, default=-1): cv.int_,
            cv.Optional(CONF_UART2, default=-1): cv.int_,
            cv.Optional(CONF_TTL, default=30): cv.int_,
            cv.Optional(CONF_QUICKARM): cv.boolean,
            cv.Optional(CONF_LRR): cv.boolean,
            cv.Optional(CONF_RFR): cv.boolean,
            cv.Optional(CONF_RFRADDR, default=0): cv.int_,
            cv.Optional(CONF_LEGACYPROTOCOL, default=False): cv.boolean,
            cv.Optional(CONF_CC1101_MOSI): cv.int_range(min=0, max=39),
            cv.Optional(CONF_CC1101_MISO): cv.int_range(min=0, max=39),
            cv.Optional(CONF_CC1101_SCK): cv.int_range(min=0, max=39),
            cv.Optional(CONF_CC1101_CSN): cv.int_range(min=0, max=39),
            cv.Optional(CONF_CC1101_GDO0): cv.int_range(min=0, max=39),
            cv.Optional(CONF_CC1101_SPI_BUS, default=2): cv.one_of(2, 3, int=True),
            cv.Optional(CONF_CC1101_RSSI_THRESHOLD, default=-87): cv.int_range(
                min=-95, max=-65
            ),
            cv.Optional(CONF_RF_HEARTBEAT_EXTERNAL, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(cv.polling_component_schema("5000ms")),
    validate_keypads,
    validate_c6_uarts,
    validate_auiaddr,
    validate_clocksync,
    validate_cc1101,
)


async def to_code(config):

    # Enable USE_OTA_STATE_LISTENER so OTAGlobalStateListener and
    # get_global_ota_callback() are compiled into the firmware.
    request_ota_state_listeners()

    esp32.add_idf_sdkconfig_option("CONFIG_FREERTOS_HZ", 1000)
    esp32.add_idf_sdkconfig_option(
        "CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD", True
    )

    # Place the UART ISR in IRAM so it keeps running while the flash cache is
    # disabled.  Any SPI flash write -- an NVS commit from the preferences
    # component or safe_mode's boot-counter reset, both of which happen a few
    # seconds into every boot -- masks every interrupt handler that lives in
    # flash.  With the UART ISR among them, no UART_DATA events are posted for
    # the duration, the in-progress frame read times out, and the bytes surface
    # afterwards as a desynchronised stream.
    #
    # This is not optional: init_uart() passes ESP_INTR_FLAG_IRAM to
    # uart_driver_install(), which rejects the call unless this is set.  The two
    # are deliberately coupled here rather than left to the user's YAML.
    esp32.add_idf_sdkconfig_option("CONFIG_UART_ISR_IN_IRAM", True)

    var = cg.new_Pvariable(
        config[CONF_ID],
        config[CONF_KEYPAD1],
        config[CONF_RXPIN],
        config[CONF_TXPIN],
        config[CONF_UART1],
        config[CONF_MONITORPIN],
        config[CONF_UART2],
    )

    cg.add(var.set_accessCode(config[CONF_ACCESSCODE]))
    cg.add(var.set_defaultPartition(config[CONF_DEFAULTPARTITION]))
    if config[CONF_DEBUGLOG]:
        cg.add_define("DEBUG_LOG")
    if config[CONF_DEBUGPULSE]:
        cg.add_define("DEBUG_PULSE")
    if config[CONF_LEGACYPROTOCOL]:
        cg.add_define("LEGACY_SE_PROTOCOL")
        _LOGGER.info("Enabling Legacy Protocol Option")
    if config[CONF_KEYPAD1] != 0:
        cg.add(var.set_partitionKeypad(1, config[CONF_KEYPAD1]))
    if config[CONF_KEYPAD2] != 0:
        cg.add(var.set_partitionKeypad(2, config[CONF_KEYPAD2]))
    if config[CONF_KEYPAD3] != 0:
        cg.add(var.set_partitionKeypad(3, config[CONF_KEYPAD3]))
    if config[CONF_KEYPAD4] != 0:
        cg.add(var.set_partitionKeypad(4, config[CONF_KEYPAD4]))
    if config[CONF_KEYPAD5] != 0:
        cg.add(var.set_partitionKeypad(5, config[CONF_KEYPAD5]))
    if config[CONF_KEYPAD6] != 0:
        cg.add(var.set_partitionKeypad(6, config[CONF_KEYPAD6]))
    if config[CONF_KEYPAD7] != 0:
        cg.add(var.set_partitionKeypad(7, config[CONF_KEYPAD7]))
    if config[CONF_KEYPAD8] != 0:
        cg.add(var.set_partitionKeypad(8, config[CONF_KEYPAD8]))
    cg.add(var.initialize_partition_sensors())
    cg.add(var.set_ttl(config[CONF_TTL]))
    if CONF_QUICKARM in config:
        cg.add(var.set_quickArm(config[CONF_QUICKARM]))
    if CONF_LRR in config:
        cg.add(var.set_lrrSupervisor(config[CONF_LRR]))
    if CONF_RFR in config:
        cg.add(var.set_rfrEmulation(config[CONF_RFR], config[CONF_RFRADDR]))
    if CONF_CC1101_MOSI in config:
        if config[CONF_DEBUGPULSE]:
            # debug_pulsing uses the RMT peripheral — CC1101 reception is incompatible.
            # Skip CC1101_RECEIVER define and hardware init so both can coexist in YAML.
            _LOGGER.warning(
                "CC1101 hardware receiver disabled because debug_pulsing is enabled "
                "(both use the RMT peripheral and cannot run simultaneously)."
            )
        else:
            cg.add_define("CC1101_RECEIVER")
            cg.add(
                var.init_cc1101(
                    config[CONF_CC1101_MOSI],
                    config[CONF_CC1101_MISO],
                    config[CONF_CC1101_SCK],
                    config[CONF_CC1101_CSN],
                    config[CONF_CC1101_GDO0],
                    config[CONF_CC1101_SPI_BUS],
                )
            )
            cg.add(var.set_cc1101_rssi_threshold(config[CONF_CC1101_RSSI_THRESHOLD]))
            _LOGGER.info(
                "CC1101 hardware receiver enabled (RSSI threshold: %d dBm)",
                config[CONF_CC1101_RSSI_THRESHOLD],
            )
    if CONF_AUIADDR in config:
        cg.add(var.set_auiaddr(config[CONF_AUIADDR]))
    if CONF_AUTOCLOCKSYNC in config:
        cg.add(var.set_clocksync(config[CONF_AUTOCLOCKSYNC]))
    if config[CONF_RF_HEARTBEAT_EXTERNAL]:
        cg.add(var.set_rf_heartbeat_external(True))
        _LOGGER.info(
            "RF virtual zone heartbeats set to external mode — use set_rf_zone_heartbeat service"
        )

    await cg.register_component(var, config)

    # --- Diagnostic text sensors for configuration info ---
    # Create one text_sensor::TextSensor per config property.  These are
    # registered via the standard ESPHome codegen path so entity metadata
    # (name, entity_category) is emitted into the generated ::setup() where
    # the protected configure_entity_() call is accessible.  Each sensor
    # publishes its value immediately in setup() and then never changes,
    # giving the user a permanent at-a-glance view of the component config
    # in HA under the device's Diagnostic section.

    text_sensor_cls = cg.esphome_ns.namespace("text_sensor").class_("TextSensor")

    async def add_diag(name: str, value: str, icon: str = "mdi:expansion-card-variant"):
        slug = "".join(c if c.isalnum() else "_" for c in name.lower()).strip("_")
        sensor_id = ID(f"vista_diag_{slug}", is_declaration=True, type=text_sensor_cls)
        sensor_config = {
            CONF_ID: sensor_id,
            CONF_NAME: name,
            CONF_ENTITY_CATEGORY: "diagnostic",
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_ICON: icon,
        }
        sensor_var = await ts_mod.new_text_sensor(sensor_config)
        cg.add(sensor_var.publish_state(value))
        return sensor_var

    protocol = (
        "Vista SE (Legacy)" if config.get(CONF_LEGACYPROTOCOL, False) else "Vista 20P"
    )
    await add_diag("Panel Protocol", protocol, "mdi:alarm-panel")

    keypad_map = [
        (1, CONF_KEYPAD1),
        (2, CONF_KEYPAD2),
        (3, CONF_KEYPAD3),
        (4, CONF_KEYPAD4),
        (5, CONF_KEYPAD5),
        (6, CONF_KEYPAD6),
        (7, CONF_KEYPAD7),
        (8, CONF_KEYPAD8),
    ]
    active_parts = [(p, config[c]) for p, c in keypad_map if config[c] != 0]
    kp_str = ", ".join(f"P{p}: {addr}" for p, addr in active_parts)
    await add_diag("Active Partitions (keypad addr)", kp_str, "mdi:alarm-panel")

    cc1101_active = CONF_CC1101_MOSI in config and not config.get(
        CONF_DEBUGPULSE, False
    )
    if config.get(CONF_RFR, False):
        rfr_addr = config[CONF_RFRADDR]
        rf_str = (
            f"CC1101 HW @ addr:{rfr_addr}"
            if cc1101_active
            else f"SW emulated @ addr:{rfr_addr}"
        )
    else:
        rf_str = "Disabled"
    await add_diag("RF Receiver Emulation", rf_str)

    lrr = config.get(CONF_LRR, False)
    await add_diag("LRR Supervisor", "Enabled" if lrr else "Disabled")

    aui = config[CONF_AUIADDR]
    await add_diag("AUI Address", f"{aui}" if aui != 0 else "Disabled")

    panel_bs = [
        entry
        for entry in CORE.config.get("binary_sensor", [])
        if entry.get("platform") == "vista_alarm_panel"
    ]

    expander_zones = sorted(
        entry["zone"]
        for entry in panel_bs
        if entry.get("emulated") is True and "rf_serial" not in entry
    )
    exp_str = (
        ", ".join(str(z) for z in expander_zones) if expander_zones else "Disabled"
    )
    await add_diag("Expander Emulation (zones)", exp_str)

    rf_emulated_zones = sorted(
        entry["zone"]
        for entry in panel_bs
        if entry.get("emulated") is True and "rf_serial" in entry
    )
    rf_emu_str = (
        ", ".join(str(z) for z in rf_emulated_zones)
        if rf_emulated_zones
        else "Disabled"
    )
    await add_diag("RF Emulated Zones", rf_emu_str)

    chksum_sensor = await add_diag("ECP Bus Checksum Failures", "0", "mdi:counter")
    cg.add(var.set_chksum_fail_sensor(chksum_sensor))
