import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import lock
from esphome.const import CONF_DEVICE_ID

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

XbotRegisterLock = xbot_ns.class_(
    "XbotRegisterLock", lock.Lock, cg.Parented, cg.Component
)

# (yaml_key, src, write_dest, register, icon, default_name)
LOCKS = [
    ("lock", SRC_MAIN, DEST_MAIN, 0x7F, "mdi:lock", "Lock"),
]


_DEFAULT_NAMES = [(k, n) for k, *_r, n in LOCKS]


def _inject_defaults(config):
    return inject_entity_defaults(config, _DEFAULT_NAMES)


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    XBOT_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            **{
                cv.Optional(key): lock.lock_schema(XbotRegisterLock, icon=icon)
                for key, _s, _d, _reg, icon, _name in LOCKS
            },
        }
    ),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_XBOT_ID])
    config = apply_entity_prefix(
        config, _DEFAULT_NAMES, hub_name_prefix(config[CONF_XBOT_ID])
    )
    for key, src, dest, reg, _icon, _name in LOCKS:
        if key not in config:
            continue
        sub = config[key]
        var = await lock.new_lock(sub)
        await cg.register_component(var, sub)
        await cg.register_parented(var, hub)
        cg.add(var.set_register(reg))
        cg.add(var.set_src(src))
        cg.add(var.set_dest(dest))
        cg.add(var.set_cmd(WRITE_CMD))
