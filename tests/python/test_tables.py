"""The entity tables against the poll table they depend on.

Every entity reads its value from a register the sweep asks for. Nothing at
runtime says an entity is watching a register no request covers: the entity
simply never publishes, and a write to one is refused with a log line. These
tests are where that is caught instead.

Needs an interpreter with esphome importable:

    python -m unittest discover -s tests/python
"""

import os
import sys
import unittest
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "components"))

import xbot
import xbot.binary_sensor
import xbot.button
import xbot.lock
import xbot.number
import xbot.select
import xbot.sensor
import xbot.switch
import xbot.text_sensor
from xbot.const import (
    DEST_MAIN,
    DEST_MODULE,
    READ_CMD_MAIN,
    READ_CMD_MODULE,
    REG_WHEEL_FACTOR,
    SRC_MAIN,
    SRC_MODULE,
)

# A reply carries the source the request was addressed to.
DEST_FOR_SRC = {SRC_MAIN: DEST_MAIN, SRC_MODULE: DEST_MODULE}

PLATFORMS = (
    xbot.sensor,
    xbot.binary_sensor,
    xbot.text_sensor,
    xbot.switch,
    xbot.number,
    xbot.select,
    xbot.button,
    xbot.lock,
)


def polled_registers(src):
    covered = set()
    for dest, _cmd, reg, count in xbot.POLL_TABLE:
        if dest != DEST_FOR_SRC[src]:
            continue
        # count is a byte count and a register is two bytes wide.
        covered.update(range(reg, reg + count // 2))
    return covered


class Coverage(unittest.TestCase):
    def assert_polled(self, src, registers, what):
        covered = polled_registers(src)
        for reg in registers:
            self.assertIn(
                reg,
                covered,
                f"{what} reads register 0x{reg:02X} from source 0x{src:02X}, "
                f"which no poll table block asks for",
            )

    def test_register_sensors(self):
        for key, reg, wide, *_rest in xbot.sensor.REGISTER_SENSORS:
            wanted = [reg, reg + 1] if wide else [reg]
            self.assert_polled(SRC_MAIN, wanted, f"sensor '{key}'")

    def test_product_sensors(self):
        for key, reg_a, reg_b, *_rest in xbot.sensor.PRODUCT_SENSORS:
            self.assert_polled(SRC_MAIN, [reg_a, reg_b], f"sensor '{key}'")

    def test_text_sensors(self):
        for key, src, registers, *_rest in xbot.text_sensor.REGISTER_TEXT_SENSORS:
            self.assert_polled(src, registers, f"text sensor '{key}'")

    def test_switches(self):
        for key, src, _dest, reg, *_rest in xbot.switch.REGISTER_SWITCHES:
            self.assert_polled(src, [reg], f"switch '{key}'")

    def test_selects(self):
        for key, src, _dest, reg, *_rest in xbot.select.SELECTS:
            self.assert_polled(src, [reg], f"select '{key}'")

    def test_locks(self):
        for key, src, _dest, reg, *_rest in xbot.lock.LOCKS:
            self.assert_polled(src, [reg], f"lock '{key}'")

    def test_numbers_and_the_register_that_scales_them(self):
        for key, reg, _scale, scale_from, *_rest in xbot.number.NUMBERS:
            wanted = [reg] if scale_from is None else [reg, scale_from]
            self.assert_polled(SRC_MAIN, wanted, f"number '{key}'")


class PollTableShape(unittest.TestCase):
    def test_byte_counts_are_even(self):
        for dest, _cmd, reg, count in xbot.POLL_TABLE:
            self.assertEqual(
                count % 2,
                0,
                f"block 0x{reg:02X} at 0x{dest:02X} asks for {count} bytes, "
                f"which is not a whole number of registers",
            )

    def test_no_block_runs_past_the_register_map(self):
        for dest, _cmd, reg, count in xbot.POLL_TABLE:
            last = reg + count // 2 - 1
            self.assertLessEqual(
                last,
                0xFF,
                f"block 0x{reg:02X} at 0x{dest:02X} ends at 0x{last:02X}, "
                f"past the last register the decoder will hand over",
            )

    def test_each_address_uses_its_own_read_opcode(self):
        opcode = {DEST_MAIN: READ_CMD_MAIN, DEST_MODULE: READ_CMD_MODULE}
        for dest, cmd, reg, _count in xbot.POLL_TABLE:
            self.assertIn(dest, opcode, f"block 0x{reg:02X} uses address 0x{dest:02X}")
            self.assertEqual(cmd, opcode[dest])

    def test_the_wheel_factor_is_read_first(self):
        # Every speed limit is a count scaled by it, so a limit read or written
        # before it arrives means a different speed.
        _dest, _cmd, reg, count = xbot.POLL_TABLE[0]
        self.assertLessEqual(reg, REG_WHEEL_FACTOR)
        self.assertIn(REG_WHEEL_FACTOR, range(reg, reg + count // 2))


class DefaultNames(unittest.TestCase):
    def all_default_names(self):
        names = []
        for platform in PLATFORMS:
            names += [name for _key, name in platform._DEFAULT_NAMES]
        return names

    def test_no_two_entities_share_a_default_name(self):
        # An entity's api key is a hash of its name alone, so two entities
        # taking one name are one entity to a client.
        repeated = [
            n for n, count in Counter(self.all_default_names()).items() if count > 1
        ]
        self.assertEqual(repeated, [])

    def test_every_platform_declares_some(self):
        for platform in PLATFORMS:
            self.assertTrue(platform._DEFAULT_NAMES, platform.__name__)


class Ranges(unittest.TestCase):
    def sensor_keys(self):
        return {row[0] for row in xbot.sensor.REGISTER_SENSORS} | {
            row[0] for row in xbot.sensor.PRODUCT_SENSORS
        }

    def test_every_range_names_a_sensor_that_exists(self):
        for key in xbot.sensor.RANGES:
            self.assertIn(key, self.sensor_keys())

    def test_every_range_is_ordered(self):
        for key, (low, high) in xbot.sensor.RANGES.items():
            self.assertLess(low, high, key)


if __name__ == "__main__":
    unittest.main()
