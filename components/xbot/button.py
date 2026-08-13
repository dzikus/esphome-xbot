import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_DEVICE_ID, ENTITY_CATEGORY_DIAGNOSTIC

from . import (
    CONF_XBOT_ID,
    XBOT_COMPONENT_SCHEMA,
    apply_entity_prefix,
    hub_name_prefix,
    inject_entity_defaults,
    xbot_ns,
)

DEPENDENCIES = ["xbot"]
CODEOWNERS = ["@dzikus"]

XbotPollButton = xbot_ns.class_("XbotPollButton", button.Button, cg.Parented)

CONF_POLL_NOW = "poll_now"


_DEFAULT_NAMES = [(CONF_POLL_NOW, "Poll Now")]


def _inject_defaults(config):
    return inject_entity_defaults(config, _DEFAULT_NAMES)


CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    XBOT_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            cv.Optional(CONF_POLL_NOW): button.button_schema(
                XbotPollButton,
                icon="mdi:refresh",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    ),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_XBOT_ID])
    config = apply_entity_prefix(
        config, _DEFAULT_NAMES, hub_name_prefix(config[CONF_XBOT_ID])
    )
    if CONF_POLL_NOW not in config:
        return
    var = await button.new_button(config[CONF_POLL_NOW])
    await cg.register_parented(var, hub)
