import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_DEVICE_ID, ENTITY_CATEGORY_CONFIG

from . import (
    CONF_XBOT_ID,
    XBOT_COMPONENT_SCHEMA,
    apply_entity_prefix,
    hub_name_prefix,
    inject_entity_defaults,
    xbot_ns,
)
from .const import DEST_MAIN, SRC_MAIN, WRITE_CMD

DEPENDENCIES = ["xbot"]
CODEOWNERS = ["@dzikus"]

CONF_BLUETOOTH = "bluetooth"

# Component-derived so its setup() gets a slot to apply the restored state.
XbotBleSwitch = xbot_ns.class_(
    "XbotBleSwitch", switch.Switch, cg.Parented, cg.Component
)
XbotRegisterSwitch = xbot_ns.class_(
    "XbotRegisterSwitch", switch.Switch, cg.Parented, cg.Component
)

# (yaml_key, src, write_dest, register, bit_mask, icon, entity_category,
#  default_name)
# A non-zero bit_mask marks a register that holds several flags at once. The
# vehicle expects the other bits to survive a write, so those go
# read-modify-write; writing 0 or 1 whole would clear the neighbours.
# Cruise control is something a rider turns on, so it stays a primary control.
REGISTER_SWITCHES = [
    (
        "zero_start",
        SRC_MAIN,
        DEST_MAIN,
        0x7D,
        0x0001,
        "mdi:play-speed",
        ENTITY_CATEGORY_CONFIG,
        "Zero Start",
    ),
    (
        "cruise_control",
        SRC_MAIN,
        DEST_MAIN,
        0x7C,
        0x0000,
        "mdi:car-cruise-control",
        None,
        "Cruise Control",
    ),
    (
        "light",
        SRC_MAIN,
        DEST_MAIN,
        0xF2,
        0x0001,
        "mdi:car-light-high",
        None,
        "Light",
    ),
]


def _register_switch_schema(icon, entity_category):
    # switch_schema rejects a None category, so leave it out.
    kwargs = {"icon": icon, "default_restore_mode": "DISABLED"}
    if entity_category is not None:
        kwargs["entity_category"] = entity_category
    return switch.switch_schema(XbotRegisterSwitch, **kwargs)


_DEFAULT_NAMES = [(CONF_BLUETOOTH, "Bluetooth")] + [
    (k, n) for k, *_r, n in REGISTER_SWITCHES
]


def _inject_defaults(config):
    return inject_entity_defaults(config, _DEFAULT_NAMES)


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    XBOT_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            # Restores ON when nothing is persisted, so a reboot cannot quietly
            # leave the vehicle unreachable.
            cv.Optional(CONF_BLUETOOTH): switch.switch_schema(
                XbotBleSwitch,
                icon="mdi:bluetooth",
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="RESTORE_DEFAULT_ON",
            ),
            # No restore at all: the vehicle holds the real state, and restoring
            # one would route through write_state and push it back on every boot.
            **{
                cv.Optional(key): _register_switch_schema(icon, ec)
                for key, _s, _d, _reg, _m, icon, ec, _name in REGISTER_SWITCHES
            },
        }
    ),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_XBOT_ID])
    config = apply_entity_prefix(
        config, _DEFAULT_NAMES, hub_name_prefix(config[CONF_XBOT_ID])
    )
    if CONF_BLUETOOTH in config:
        sub = config[CONF_BLUETOOTH]
        var = await switch.new_switch(sub)
        await cg.register_component(var, sub)
        await cg.register_parented(var, hub)
        # Tells the hub something else owns the enable flag. Left out, the hub
        # runs enabled, since no switch will arrive to do it.
        cg.add(hub.set_ble_switch_present())

    for key, src, dest, reg, mask, _icon, _ec, _name in REGISTER_SWITCHES:
        if key not in config:
            continue
        entry = config[key]
        reg_sw = await switch.new_switch(entry)
        await cg.register_component(reg_sw, entry)
        await cg.register_parented(reg_sw, hub)
        cg.add(reg_sw.set_register(reg))
        cg.add(reg_sw.set_src(src))
        cg.add(reg_sw.set_dest(dest))
        cg.add(reg_sw.set_cmd(WRITE_CMD))
        cg.add(reg_sw.set_bit_mask(mask))
