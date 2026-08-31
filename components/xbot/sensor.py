import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_DEVICE_ID,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_DISTANCE,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_SPEED,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_EMPTY,
    UNIT_KILOMETER,
    UNIT_KILOMETER_PER_HOUR,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_VOLT,
    UNIT_WATT,
)

from . import (
    CONF_XBOT_ID,
    XBOT_COMPONENT_SCHEMA,
    apply_entity_prefix,
    hub_name_prefix,
    inject_entity_defaults,
    xbot_ns,
)
from .const import REG_WHEEL_FACTOR, SRC_MAIN

DEPENDENCIES = ["xbot"]
CODEOWNERS = ["@dzikus"]

# Raw calibration counts, so they start hidden.
HIDDEN_KEYS = frozenset({"wheel_factor"})

# Created only when asked for. Neither says anything on the vehicle this was
# tried on, in two different ways: 0x3A holds 0xFFFF moving or parked, and
# 0x3F reads a flat 0 while the controller's own sensor in the same frame reads
# 34 degC, which is what a missing probe looks like. Both are kept because the
# registers are the ones the app reads these from.
OPT_IN_KEYS = frozenset({"trip_time", "motor_temperature"})

# Registers the controller reports as two's complement. Read unsigned, a motor
# at -2.0 degC publishes 6551.6. Current goes negative under regeneration, and
# power with it.
SIGNED_KEYS = frozenset(
    {
        "temperature",
        "motor_temperature",
        "trip_distance",
        "error_code",
        "warning_code",
        "current",
        "power",
    }
)

# Registers where 0xFFFF is a reading of -1 and not the controller's
# not-available marker. Kept apart from the signed set: a trip counter is two's
# complement on the wire and still uses 0xFFFF to say it has no trip to report,
# which read as -0.01 km would be a distance counter going backwards.
SENTINEL_KEYS = frozenset({"temperature", "motor_temperature", "current", "power"})

# Bounds on the published value. A wrong reading does not pass: a persisted one
# is written to flash and comes back after every reboot. Set against one vehicle
# and left wide; registers not listed have no reading their range would rule out.
RANGES = {
    "total_distance": (0.0, 200000.0),
    "operating_time": (0.0, 100000000.0),
    "battery_level": (0.0, 100.0),
    "voltage": (0.0, 120.0),
    "current": (-300.0, 300.0),
    "temperature": (-40.0, 150.0),
    "trip_distance": (0.0, 330.0),
    "speed": (0.0, 150.0),
    "motor_temperature": (-40.0, 250.0),
    "power": (-20000.0, 20000.0),
}

# This controller reports no pack temperature and no pack capacity, so there are
# deliberately no entities for them.

XbotRegisterSensor = xbot_ns.class_(
    "XbotRegisterSensor", sensor.Sensor, cg.Parented, cg.Component
)
XbotProductSensor = xbot_ns.class_(
    "XbotProductSensor", sensor.Sensor, cg.Parented, cg.Component
)

# (yaml_key, register, wide, divisor, persist, unit, decimals, device_class,
#  state_class, icon, entity_category, default_name)
# Field order, naming and icon choices follow the fiido_bms component so the two
# vehicles read the same way in Home Assistant.
# persist keeps the last reading across reboots. Only for values that carry over
# between rides.
REGISTER_SENSORS = [
    (
        "total_distance",
        0x29,
        True,
        1000.0,
        True,
        UNIT_KILOMETER,
        3,
        DEVICE_CLASS_DISTANCE,
        STATE_CLASS_TOTAL_INCREASING,
        "mdi:counter",
        None,
        "Total Distance",
    ),
    (
        "operating_time",
        0x32,
        True,
        1.0,
        True,
        UNIT_SECOND,
        0,
        DEVICE_CLASS_DURATION,
        STATE_CLASS_TOTAL_INCREASING,
        "mdi:timer-outline",
        None,
        "Operating Time",
    ),
    (
        "battery_level",
        0x22,
        False,
        1.0,
        True,
        UNIT_PERCENT,
        0,
        DEVICE_CLASS_BATTERY,
        STATE_CLASS_MEASUREMENT,
        None,
        None,
        "Battery Level",
    ),
    (
        "voltage",
        0x47,
        False,
        100.0,
        False,
        UNIT_VOLT,
        2,
        DEVICE_CLASS_VOLTAGE,
        STATE_CLASS_MEASUREMENT,
        "mdi:car-battery",
        None,
        "Battery Voltage",
    ),
    (
        "current",
        0x48,
        False,
        100.0,
        False,
        UNIT_AMPERE,
        2,
        DEVICE_CLASS_CURRENT,
        STATE_CLASS_MEASUREMENT,
        "mdi:current-dc",
        None,
        "Battery Current",
    ),
    (
        "temperature",
        0xBB,
        False,
        10.0,
        False,
        UNIT_CELSIUS,
        1,
        DEVICE_CLASS_TEMPERATURE,
        STATE_CLASS_MEASUREMENT,
        None,
        None,
        "Temperature",
    ),
    (
        "trip_time",
        0x3A,
        False,
        1.0,
        True,
        UNIT_SECOND,
        0,
        DEVICE_CLASS_DURATION,
        STATE_CLASS_TOTAL_INCREASING,
        "mdi:timer-outline",
        None,
        "Trip Time",
    ),
    (
        "trip_distance",
        0x2F,
        False,
        100.0,
        True,
        UNIT_KILOMETER,
        2,
        DEVICE_CLASS_DISTANCE,
        STATE_CLASS_TOTAL_INCREASING,
        "mdi:map-marker-distance",
        None,
        "Trip Distance",
    ),
    (
        "speed",
        0x27,
        False,
        100.0,
        False,
        UNIT_KILOMETER_PER_HOUR,
        1,
        DEVICE_CLASS_SPEED,
        STATE_CLASS_MEASUREMENT,
        "mdi:speedometer",
        None,
        "Speed",
    ),
    (
        "motor_temperature",
        0x3F,
        False,
        10.0,
        False,
        UNIT_CELSIUS,
        1,
        DEVICE_CLASS_TEMPERATURE,
        STATE_CLASS_MEASUREMENT,
        None,
        None,
        "Motor Temperature",
    ),
    (
        "error_code",
        0x1B,
        False,
        1.0,
        False,
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:alert-circle",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Error Code",
    ),
    (
        "warning_code",
        0x1C,
        False,
        1.0,
        False,
        UNIT_EMPTY,
        0,
        None,
        None,
        "mdi:alert",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Warning Code",
    ),
    # Raw calibration count, hidden unless the speed scale looks wrong.
    (
        "wheel_factor",
        REG_WHEEL_FACTOR,
        False,
        1.0,
        True,
        UNIT_EMPTY,
        0,
        None,
        STATE_CLASS_MEASUREMENT,
        "mdi:tire",
        ENTITY_CATEGORY_DIAGNOSTIC,
        "Wheel Factor",
    ),
]

# (yaml_key, reg_a, reg_b, divisor, unit, decimals, device_class, state_class,
#  icon, entity_category, default_name)
PRODUCT_SENSORS = [
    (
        "power",
        0x47,
        0x48,
        10000.0,
        UNIT_WATT,
        1,
        DEVICE_CLASS_POWER,
        STATE_CLASS_MEASUREMENT,
        None,
        None,
        "Power",
    ),
]


def _sensor_schema(
    cls, unit, decimals, device_class, state_class, icon, entity_category
):
    # The helpers take cv.UNDEFINED as their sentinel, so a None passed through
    # would be validated as a real value. Omitted keys are left out entirely.
    kwargs = {"unit_of_measurement": unit, "accuracy_decimals": decimals}
    if device_class is not None:
        kwargs["device_class"] = device_class
    if state_class is not None:
        kwargs["state_class"] = state_class
    if icon is not None:
        kwargs["icon"] = icon
    if entity_category is not None:
        kwargs["entity_category"] = entity_category
    return sensor.sensor_schema(cls, **kwargs)


_DEFAULT_NAMES = [(k, n) for k, *_r, n in REGISTER_SENSORS] + [
    (k, n) for k, *_r, n in PRODUCT_SENSORS
]


def _inject_defaults(config):
    return inject_entity_defaults(
        config, _DEFAULT_NAMES, hidden=HIDDEN_KEYS, opt_in=OPT_IN_KEYS
    )


_REGISTER_SCHEMAS = {
    cv.Optional(key): _sensor_schema(XbotRegisterSensor, unit, dec, dc, sc, icon, ec)
    for key, _r, _w, _d, _p, unit, dec, dc, sc, icon, ec, _n in REGISTER_SENSORS
}

_PRODUCT_SCHEMAS = {
    cv.Optional(key): _sensor_schema(XbotProductSensor, unit, dec, dc, sc, icon, ec)
    for key, _a, _b, _d, unit, dec, dc, sc, icon, ec, _n in PRODUCT_SENSORS
}

CONFIG_SCHEMA = cv.All(
    _inject_defaults,
    XBOT_COMPONENT_SCHEMA.extend(
        {
            cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
            **_REGISTER_SCHEMAS,
            **_PRODUCT_SCHEMAS,
        }
    ),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_XBOT_ID])
    config = apply_entity_prefix(
        config, _DEFAULT_NAMES, hub_name_prefix(config[CONF_XBOT_ID])
    )

    for key, reg, wide, divisor, persist, *_rest in REGISTER_SENSORS:
        if key not in config:
            continue
        sub = config[key]
        var = await sensor.new_sensor(sub)
        await cg.register_component(var, sub)
        await cg.register_parented(var, hub)
        cg.add(var.set_src(SRC_MAIN))
        cg.add(var.set_register(reg))
        cg.add(var.set_wide(wide))
        cg.add(var.set_signed(key in SIGNED_KEYS))
        cg.add(var.set_accepts_sentinel(key in SENTINEL_KEYS))
        cg.add(var.set_divisor(divisor))
        cg.add(var.set_persist(persist))
        if key in RANGES:
            cg.add(var.set_range(*RANGES[key]))

    for key, reg_a, reg_b, divisor, *_rest in PRODUCT_SENSORS:
        if key not in config:
            continue
        sub = config[key]
        var = await sensor.new_sensor(sub)
        await cg.register_component(var, sub)
        await cg.register_parented(var, hub)
        cg.add(var.set_src(SRC_MAIN))
        cg.add(var.set_registers(reg_a, reg_b))
        # Both name the second register: reg_a is an unsigned magnitude.
        cg.add(var.set_b_signed(key in SIGNED_KEYS))
        cg.add(var.set_b_accepts_sentinel(key in SENTINEL_KEYS))
        cg.add(var.set_divisor(divisor))
        if key in RANGES:
            cg.add(var.set_range(*RANGES[key]))
