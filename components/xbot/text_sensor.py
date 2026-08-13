import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_DEVICE_ID, ENTITY_CATEGORY_DIAGNOSTIC

from . import (
    CONF_XBOT_ID,
    XBOT_COMPONENT_SCHEMA,
    apply_entity_prefix,
    hub_name_prefix,
    inject_entity_defaults,
    xbot_ns,
)
from .const import SRC_MAIN

DEPENDENCIES = ["xbot"]
CODEOWNERS = ["@dzikus"]

XbotRegisterTextSensor = xbot_ns.class_(
    "XbotRegisterTextSensor", text_sensor.TextSensor, cg.Parented, cg.Component
)

# (yaml_key, src, registers in order, icon, entity_category, default_name)
REGISTER_TEXT_SENSORS = [
    (
        "controller_version",
        SRC_MAIN,
        [0x1E, 0x1D],
        "mdi:chip",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Controller Version",
    ),
]


_DEFAULT_NAMES = [(k, n) for k, *_r, n in REGISTER_TEXT_SENSORS]


def _inject_defaults(config):
    return inject_entity_defaults(config, _DEFAULT_NAMES)


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    XBOT_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            **{
                cv.Optional(key): text_sensor.text_sensor_schema(
                    XbotRegisterTextSensor, icon=icon, entity_category=ec
                )
                for key, _s, _r, icon, ec, _default_name in REGISTER_TEXT_SENSORS
            },
        }
    ),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_XBOT_ID])
    config = apply_entity_prefix(
        config, _DEFAULT_NAMES, hub_name_prefix(config[CONF_XBOT_ID])
    )
    for key, src, regs, _icon, _ec, _default_name in REGISTER_TEXT_SENSORS:
        if key not in config:
            continue
        sub = config[key]
        var = await text_sensor.new_text_sensor(sub)
        await cg.register_component(var, sub)
        await cg.register_parented(var, hub)
        cg.add(var.set_src(src))
        for reg in regs:
            cg.add(var.add_register(reg))
