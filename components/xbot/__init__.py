import logging

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import ble_client
from esphome.const import (
    CONF_DEVICE_ID,
    CONF_DISABLED_BY_DEFAULT,
    CONF_ID,
    CONF_NAME,
    CONF_UPDATE_INTERVAL,
)
from esphome.core import CORE

from .const import POLL_TABLE

_LOGGER = logging.getLogger(__name__)

DOMAIN = "xbot"

CODEOWNERS = ["@dzikus"]
DEPENDENCIES = ["ble_client"]
AUTO_LOAD = [
    "sensor",
    "binary_sensor",
    "text_sensor",
    "switch",
    "number",
    "select",
    "button",
    "lock",
]
MULTI_CONF = True

CONF_XBOT_ID = "xbot_id"
CONF_NAME_PREFIX = "name_prefix"
CONF_PROFILE = "profile"
CONF_PROBE_REPEAT = "probe_repeat"
CONF_OBFUSCATION_KEY = "obfuscation_key"

MIN_UPDATE_INTERVAL_MS = 60000

xbot_ns = cg.esphome_ns.namespace("xbot")
XbotHub = xbot_ns.class_("XbotHub", ble_client.BLEClientNode, cg.PollingComponent)
Profile = xbot_ns.enum("Profile", is_class=True)

# Both the accepted yaml values and what each one compiles to. Leave the option
# unset unless a scan proved auto-detection picks the wrong one.
PROFILE_ENUM = {
    "auto": Profile.UNKNOWN,
    "nus": Profile.NUS,
    "ae00": Profile.AE00,
    "ffe0": Profile.FFE0,
    "fff0-f3f7": Profile.FFF0_F3F7,
    "fff0-f2f1": Profile.FFF0_F2F1,
}

XBOT_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_XBOT_ID): cv.use_id(XbotHub),
    }
)


def inject_entity_defaults(config, keys, hidden=frozenset(), opt_in=frozenset()):
    # Copy before mutating: the validator may run against a shared dict.
    config = dict(config)
    platform_device = config.get(CONF_DEVICE_ID)
    for key, default_name in keys:
        want = config.get(key, ...)
        if want is False or (want is ... and key in opt_in):
            # Removed so the platform's to_code can
            # tell it apart from a key that was never written.
            config.pop(key, None)
            continue
        # is, not ==: a stray 'speed: 1' equals True and must not read as one.
        sub = {} if (want is ... or want is None or want is True) else want
        if not isinstance(sub, dict):
            raise cv.Invalid(
                f"'{key}' takes true, false, or the options for one entity. "
                f"To rename it write 'name: {sub}' under it.",
                path=[key],
            )
        sub = dict(sub)
        sub.setdefault(CONF_NAME, default_name)
        if platform_device is not None and CONF_DEVICE_ID not in sub:
            sub[CONF_DEVICE_ID] = platform_device
        if key in hidden:
            sub.setdefault(CONF_DISABLED_BY_DEFAULT, True)
        config[key] = sub
    return config


def hub_name_prefix(hub_id):
    # An entity's api key is a hash of its name alone, so two vehicles taking
    # the same default name share keys. Opt-in rather than applied on sight:
    # renaming entities changes the ids that history and automations follow, and
    # a prefix chosen here would also repeat a sub-device name the user already
    # gave the vehicle.
    target = str(hub_id)
    for hub in CORE.config.get(DOMAIN, []):
        if str(hub.get(CONF_ID)) == target:
            return hub.get(CONF_NAME_PREFIX, "").strip()
    return ""


def apply_entity_prefix(config, keys, prefix):
    # Only the injected default is prefixed: a name the user wrote is theirs.
    if not prefix:
        return config
    config = dict(config)
    for key, default_name in keys:
        sub = config.get(key)
        if isinstance(sub, dict) and sub.get(CONF_NAME) == default_name:
            config[key] = {**sub, CONF_NAME: f"{prefix} {default_name}"}
    return config


def _min_interval(value):
    # never is rejected along with everything under the floor. It stops update()
    # entirely, and update() is what reconnects after a dropped link and what
    # decides when readings are old enough to reach flash.
    period = cv.positive_time_period_milliseconds(value)
    if period.total_milliseconds < MIN_UPDATE_INTERVAL_MS:
        raise cv.Invalid(
            f"update_interval must be at least {MIN_UPDATE_INTERVAL_MS // 1000}s"
        )
    return period


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(XbotHub),
            cv.Optional(CONF_UPDATE_INTERVAL, default="300s"): _min_interval,
            cv.Optional(CONF_PROFILE, default="auto"): cv.one_of(
                *PROFILE_ENUM, lower=True
            ),
            cv.Optional(CONF_NAME_PREFIX): cv.All(cv.string_strict, cv.Length(max=48)),
            # A lost request looks exactly like a register the vehicle does not
            # have, and nothing reports the loss. Two asks per block is the
            # default because a published component meets links it cannot
            # measure; drop it to one where the link is known to be good.
            cv.Optional(CONF_PROBE_REPEAT, default=2): cv.int_range(min=1, max=5),
            # The name decides this. Overridden only when the triage log says
            # a different key is the one that parses.
            cv.Optional(CONF_OBFUSCATION_KEY): cv.int_range(min=0, max=255),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(ble_client.BLE_CLIENT_SCHEMA),
    cv.require_esphome_version(2026, 1, 0),
)


def _one_hub_per_ble_client(config):
    # The vehicle grants one connection at a time, and stored state is keyed by
    # vehicle, so two hubs sharing a ble_client would fight over the link and
    # then overwrite each other in flash. Nothing upstream rejects it.
    #
    # Read from the run's own config:
    # module state outlives a run in the dashboard, and CORE.config is not yet
    # populated at this point, so there is nothing to key a reset on.
    claimed = {}
    for hub in fv.full_config.get().get(DOMAIN, []):
        ble_id = str(hub[ble_client.CONF_BLE_CLIENT_ID])
        hub_id = str(hub[CONF_ID])
        if ble_id in claimed:
            raise cv.Invalid(
                f"xbot '{hub_id}' and '{claimed[ble_id]}' both use ble_client "
                f"'{ble_id}'. Give each vehicle its own ble_client."
            )
        claimed[ble_id] = hub_id
    return config


def _warn_on_shared_default_names(config):
    hubs = fv.full_config.get().get(DOMAIN, [])
    if len(hubs) > 1 and CONF_NAME_PREFIX not in config:
        _LOGGER.warning(
            "xbot '%s' has no name_prefix and %d vehicles are configured. "
            "Their entities keep the same default names, so they share api "
            "keys and mqtt topics, and only a client that reads device_id can "
            "tell the vehicles apart. Set name_prefix per hub to give each "
            "vehicle its own names, or name_prefix: '' to keep the current "
            "ones and silence this.",
            config[CONF_ID],
            len(hubs),
        )
    return config


def _final_validate(config):
    _one_hub_per_ble_client(config)
    return _warn_on_shared_default_names(config)


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_profile_override(PROFILE_ENUM[config[CONF_PROFILE]]))
    cg.add(var.set_probe_repeat(config[CONF_PROBE_REPEAT]))
    cg.add(var.set_key_override(config.get(CONF_OBFUSCATION_KEY, -1)))
    # The registers live in const.py alone.
    for dest, cmd, reg, count in POLL_TABLE:
        cg.add(var.add_poll_entry(dest, cmd, reg, count))
