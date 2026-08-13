import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_STEP,
    ENTITY_CATEGORY_CONFIG,
)

from . import (
    CONF_XBOT_ID,
    XBOT_COMPONENT_SCHEMA,
    apply_entity_prefix,
    hub_name_prefix,
    inject_entity_defaults,
    xbot_ns,
)
from .const import (
    DEST_MAIN,
    REG_WHEEL_FACTOR,
    SRC_MAIN,
    WHEEL_FACTOR_DIVISOR,
    WRITE_CMD,
)

DEPENDENCIES = ["xbot"]
CODEOWNERS = ["@dzikus"]

XbotRegisterNumber = xbot_ns.class_(
    "XbotRegisterNumber", number.Number, cg.Parented, cg.Component
)

# (yaml_key, register, scale, scale_from, min, max, step, unit, icon, name)
# Ranges are the ones this vehicle reports in 0xC2 to 0xC7. Those are read every
# sweep but not used: a number's limits are fixed at codegen. A controller with
# different ranges gets the wrong slider, and the keys below fix it.
NUMBERS = [
    (
        "sport_speed_limit",
        0xF0,
        None,
        REG_WHEEL_FACTOR,
        6.0,
        35.0,
        1.0,
        "km/h",
        "mdi:speedometer",
        "Sport Speed Limit",
    ),
    (
        "drive_speed_limit",
        0xEF,
        None,
        REG_WHEEL_FACTOR,
        6.0,
        30.0,
        1.0,
        "km/h",
        "mdi:speedometer-medium",
        "Drive Speed Limit",
    ),
    (
        "eco_speed_limit",
        0xF1,
        None,
        REG_WHEEL_FACTOR,
        6.0,
        30.0,
        1.0,
        "km/h",
        "mdi:speedometer-slow",
        "Eco Speed Limit",
    ),
    (
        "cruise_speed",
        0xF3,
        1000.0,
        None,
        1.0,
        10.0,
        1.0,
        "km/h",
        "mdi:car-cruise-control",
        "Cruise Speed",
    ),
]


_DEFAULT_NAMES = [(k, n) for k, *_r, n in NUMBERS]


def _inject_defaults(config):
    return inject_entity_defaults(config, _DEFAULT_NAMES)


def _number_schema(icon, unit, mn, mx, step):
    # Overridable because the defaults are one vehicle's ranges, and another
    # controller reports its own in 0xC2 to 0xC7.
    return number.number_schema(
        XbotRegisterNumber,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=icon,
        unit_of_measurement=unit,
    ).extend(
        {
            cv.Optional(CONF_MIN_VALUE, default=mn): cv.float_,
            cv.Optional(CONF_MAX_VALUE, default=mx): cv.float_,
            cv.Optional(CONF_STEP, default=step): cv.positive_not_null_float,
        }
    )


def _min_below_max(config):
    for key, *_r in NUMBERS:
        sub = config.get(key)
        if isinstance(sub, dict) and sub[CONF_MIN_VALUE] >= sub[CONF_MAX_VALUE]:
            raise cv.Invalid(
                f"min_value {sub[CONF_MIN_VALUE]} is not below max_value "
                f"{sub[CONF_MAX_VALUE]}",
                path=[key],
            )
    return config


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    XBOT_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            **{
                cv.Optional(key): _number_schema(icon, unit, mn, mx, step)
                for key, _reg, _sc, _sf, mn, mx, step, unit, icon, _name in NUMBERS
            },
        }
    ),
    _min_below_max,
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_XBOT_ID])
    config = apply_entity_prefix(
        config, _DEFAULT_NAMES, hub_name_prefix(config[CONF_XBOT_ID])
    )
    for key, reg, scale, scale_from, *_rest in NUMBERS:
        if key not in config:
            continue
        sub = config[key]
        var = await number.new_number(
            sub,
            min_value=sub[CONF_MIN_VALUE],
            max_value=sub[CONF_MAX_VALUE],
            step=sub[CONF_STEP],
        )
        await cg.register_component(var, sub)
        await cg.register_parented(var, hub)
        cg.add(var.set_register(reg))
        cg.add(var.set_src(SRC_MAIN))
        cg.add(var.set_dest(DEST_MAIN))
        cg.add(var.set_cmd(WRITE_CMD))
        if scale_from is not None:
            cg.add(var.set_scale_from(scale_from, WHEEL_FACTOR_DIVISOR))
        else:
            cg.add(var.set_scale(scale))
