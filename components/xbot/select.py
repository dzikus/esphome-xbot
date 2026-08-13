import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_DEVICE_ID, ENTITY_CATEGORY_CONFIG

from . import (
    CONF_XBOT_ID,
    XBOT_COMPONENT_SCHEMA,
    apply_entity_prefix,
    hub_name_prefix,
    inject_entity_defaults,
    xbot_ns,
)
from .const import DEST_MAIN, DEST_MODULE, SRC_MAIN, SRC_MODULE, WRITE_CMD

DEPENDENCIES = ["xbot"]
CODEOWNERS = ["@dzikus"]

XbotRegisterSelect = xbot_ns.class_(
    "XbotRegisterSelect", select.Select, cg.Parented, cg.Component
)

# (yaml_key, src, write_dest, register, options, icon, default_name)
# The register indexes the option list.
SELECTS = [
    # Display only: the limits the vehicle stores stay in the same internal
    # units whichever unit is picked.
    (
        "speed_unit",
        SRC_MODULE,
        DEST_MODULE,
        0x1B,
        ["km/h", "mph"],
        "mdi:tape-measure",
        "Speed Unit",
    ),
    # Changes how the vehicle behaves on the road. The index picks which
    # register holds the speed limit in force: 0 -> 0xF0, 1 -> 0xEF, 2 -> 0xF1.
    # Walking has no limit register of its own; the controller caps it.
    (
        "riding_mode",
        SRC_MAIN,
        DEST_MAIN,
        0x7E,
        ["Sport", "Drive", "Eco", "Walking"],
        "mdi:speedometer-medium",
        "Riding Mode",
    ),
]


_DEFAULT_NAMES = [(k, n) for k, *_r, n in SELECTS]


def _inject_defaults(config):
    return inject_entity_defaults(config, _DEFAULT_NAMES)


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    XBOT_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            **{
                cv.Optional(key): select.select_schema(
                    XbotRegisterSelect,
                    entity_category=ENTITY_CATEGORY_CONFIG,
                    icon=icon,
                )
                for key, _s, _d, _reg, _opts, icon, _name in SELECTS
            },
        }
    ),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_XBOT_ID])
    config = apply_entity_prefix(
        config, _DEFAULT_NAMES, hub_name_prefix(config[CONF_XBOT_ID])
    )
    for key, src, dest, reg, options, _icon, _name in SELECTS:
        if key not in config:
            continue
        sub = config[key]
        var = await select.new_select(sub, options=options)
        await cg.register_component(var, sub)
        await cg.register_parented(var, hub)
        cg.add(var.set_register(reg))
        cg.add(var.set_src(src))
        cg.add(var.set_dest(dest))
        cg.add(var.set_cmd(WRITE_CMD))
