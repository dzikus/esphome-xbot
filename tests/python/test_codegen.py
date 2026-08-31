"""Schema helpers and validators from components/xbot/__init__.py.

Needs an interpreter with esphome importable:

    python -m unittest discover -s tests/python
"""

import os
import re
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "components"))

import esphome.config_validation as cv
import esphome.final_validate as fv
import xbot
from esphome.core import CORE

KEYS = [("speed", "Speed"), ("voltage", "Battery Voltage")]


class InjectEntityDefaults(unittest.TestCase):
    def test_a_key_left_out_gets_its_default_name(self):
        out = xbot.inject_entity_defaults({}, KEYS)
        self.assertEqual(out["speed"]["name"], "Speed")
        self.assertEqual(out["voltage"]["name"], "Battery Voltage")

    def test_true_and_none_read_as_the_default(self):
        for written in (True, None):
            out = xbot.inject_entity_defaults({"speed": written}, KEYS)
            self.assertEqual(out["speed"]["name"], "Speed", written)

    def test_false_removes_the_entity(self):
        out = xbot.inject_entity_defaults({"speed": False}, KEYS)
        self.assertNotIn("speed", out)

    def test_a_name_written_in_yaml_survives(self):
        out = xbot.inject_entity_defaults({"speed": {"name": "How fast"}}, KEYS)
        self.assertEqual(out["speed"]["name"], "How fast")

    def test_one_is_not_true(self):
        # 1 == True in Python, so an identity check is the only thing keeping
        # 'speed: 1' from reading as a request for the defaults.
        with self.assertRaises(cv.Invalid):
            xbot.inject_entity_defaults({"speed": 1}, KEYS)

    def test_the_refusal_says_how_to_rename(self):
        with self.assertRaises(cv.Invalid) as caught:
            xbot.inject_entity_defaults({"speed": "How fast"}, KEYS)
        self.assertIn("name: How fast", str(caught.exception))

    def test_an_opt_in_entity_stays_out_until_it_is_asked_for(self):
        out = xbot.inject_entity_defaults({}, KEYS, opt_in={"speed"})
        self.assertNotIn("speed", out)
        self.assertIn("voltage", out)

    def test_an_opt_in_entity_appears_when_written(self):
        out = xbot.inject_entity_defaults({"speed": True}, KEYS, opt_in={"speed"})
        self.assertEqual(out["speed"]["name"], "Speed")

    def test_a_hidden_entity_is_created_disabled(self):
        out = xbot.inject_entity_defaults({}, KEYS, hidden={"speed"})
        self.assertTrue(out["speed"]["disabled_by_default"])
        self.assertNotIn("disabled_by_default", out["voltage"])

    def test_yaml_can_enable_a_hidden_entity(self):
        config = {"speed": {"disabled_by_default": False}}
        out = xbot.inject_entity_defaults(config, KEYS, hidden={"speed"})
        self.assertFalse(out["speed"]["disabled_by_default"])

    def test_the_platform_device_reaches_every_entity(self):
        out = xbot.inject_entity_defaults({"device_id": "vehicle"}, KEYS)
        self.assertEqual(out["speed"]["device_id"], "vehicle")
        self.assertEqual(out["voltage"]["device_id"], "vehicle")

    def test_a_device_set_on_the_entity_wins(self):
        config = {"device_id": "vehicle", "speed": {"device_id": "other"}}
        out = xbot.inject_entity_defaults(config, KEYS)
        self.assertEqual(out["speed"]["device_id"], "other")

    def test_the_caller_dict_is_not_touched(self):
        config = {"speed": {}}
        xbot.inject_entity_defaults(config, KEYS)
        self.assertEqual(config, {"speed": {}})


class ApplyEntityPrefix(unittest.TestCase):
    def test_the_injected_default_is_prefixed(self):
        config = xbot.inject_entity_defaults({}, KEYS)
        out = xbot.apply_entity_prefix(config, KEYS, "Left")
        self.assertEqual(out["speed"]["name"], "Left Speed")
        self.assertEqual(out["voltage"]["name"], "Left Battery Voltage")

    def test_a_name_written_in_yaml_is_left_alone(self):
        config = xbot.inject_entity_defaults({"speed": {"name": "How fast"}}, KEYS)
        out = xbot.apply_entity_prefix(config, KEYS, "Left")
        self.assertEqual(out["speed"]["name"], "How fast")

    def test_no_prefix_changes_nothing(self):
        config = xbot.inject_entity_defaults({}, KEYS)
        self.assertIs(xbot.apply_entity_prefix(config, KEYS, ""), config)

    def test_a_removed_entity_is_skipped(self):
        config = xbot.inject_entity_defaults({"speed": False}, KEYS)
        out = xbot.apply_entity_prefix(config, KEYS, "Left")
        self.assertNotIn("speed", out)


class HubNamePrefix(unittest.TestCase):
    def use_config(self, hubs):
        previous = CORE.config
        CORE.config = {"xbot": hubs}
        self.addCleanup(setattr, CORE, "config", previous)

    def test_it_reads_what_the_hub_declared(self):
        self.use_config([{"id": "left", "name_prefix": "Left"}])
        self.assertEqual(xbot.hub_name_prefix("left"), "Left")

    def test_surrounding_space_is_dropped(self):
        self.use_config([{"id": "left", "name_prefix": "  Left  "}])
        self.assertEqual(xbot.hub_name_prefix("left"), "Left")

    def test_a_hub_that_declared_none_gets_none(self):
        self.use_config([{"id": "left"}])
        self.assertEqual(xbot.hub_name_prefix("left"), "")

    def test_an_id_that_is_not_there_gets_none(self):
        self.use_config([{"id": "left", "name_prefix": "Left"}])
        self.assertEqual(xbot.hub_name_prefix("right"), "")


class MinInterval(unittest.TestCase):
    def test_the_floor_itself_passes(self):
        period = xbot._min_interval("60s")
        self.assertEqual(period.total_milliseconds, xbot.MIN_UPDATE_INTERVAL_MS)

    def test_above_the_floor_passes(self):
        self.assertEqual(xbot._min_interval("5min").total_milliseconds, 300000)

    def test_under_the_floor_is_refused(self):
        for written in ("0s", "59s"):
            with self.assertRaises(cv.Invalid, msg=written):
                xbot._min_interval(written)

    def test_never_is_refused(self):
        # It would stop update(), which is what reconnects a dropped link and
        # what decides when a reading is old enough to reach flash.
        with self.assertRaises(cv.Invalid):
            xbot._min_interval("never")


class FullConfigCase(unittest.TestCase):
    def use_full_config(self, hubs):
        token = fv.full_config.set({"xbot": hubs})
        self.addCleanup(fv.full_config.reset, token)


class OneHubPerBleClient(FullConfigCase):
    def test_two_hubs_on_one_ble_client_are_refused(self):
        self.use_full_config(
            [
                {"id": "left", "ble_client_id": "shared"},
                {"id": "right", "ble_client_id": "shared"},
            ]
        )
        with self.assertRaises(cv.Invalid):
            xbot._one_hub_per_ble_client({"id": "left"})

    def test_the_refusal_names_both_hubs(self):
        self.use_full_config(
            [
                {"id": "left", "ble_client_id": "shared"},
                {"id": "right", "ble_client_id": "shared"},
            ]
        )
        with self.assertRaises(cv.Invalid) as caught:
            xbot._one_hub_per_ble_client({"id": "left"})
        message = str(caught.exception)
        self.assertIn("left", message)
        self.assertIn("right", message)

    def test_a_ble_client_each_passes(self):
        self.use_full_config(
            [
                {"id": "left", "ble_client_id": "one"},
                {"id": "right", "ble_client_id": "two"},
            ]
        )
        config = {"id": "left"}
        self.assertIs(xbot._one_hub_per_ble_client(config), config)

    def test_a_lone_hub_passes(self):
        self.use_full_config([{"id": "left", "ble_client_id": "one"}])
        config = {"id": "left"}
        self.assertIs(xbot._one_hub_per_ble_client(config), config)


class WarnOnSharedDefaultNames(FullConfigCase):
    def two_hubs(self):
        return [{"id": "left"}, {"id": "right"}]

    def test_two_hubs_with_no_prefix_warn(self):
        self.use_full_config(self.two_hubs())
        with self.assertLogs("xbot", level="WARNING") as caught:
            xbot._warn_on_shared_default_names({"id": "left"})
        self.assertIn("name_prefix", caught.output[0])

    def test_a_prefix_silences_it(self):
        self.use_full_config(self.two_hubs())
        with self.assertNoLogs("xbot", level="WARNING"):
            xbot._warn_on_shared_default_names({"id": "left", "name_prefix": "Left"})

    def test_an_empty_prefix_silences_it(self):
        # Written out on purpose is an answer, so it must not keep warning.
        self.use_full_config(self.two_hubs())
        with self.assertNoLogs("xbot", level="WARNING"):
            xbot._warn_on_shared_default_names({"id": "left", "name_prefix": ""})

    def test_a_lone_hub_is_silent(self):
        self.use_full_config([{"id": "left"}])
        with self.assertNoLogs("xbot", level="WARNING"):
            xbot._warn_on_shared_default_names({"id": "left"})


class ProfileEnum(unittest.TestCase):
    def transport_profile_names(self):
        source = os.path.join(
            os.path.dirname(__file__),
            "..",
            "..",
            "components",
            "xbot",
            "xbot_protocol.cpp",
        )
        with open(source, encoding="ascii") as handle:
            return set(re.findall(r'\.name = "([^"]+)"', handle.read()))

    def test_auto_is_accepted(self):
        self.assertIn("auto", xbot.PROFILE_ENUM)

    def test_every_accepted_value_maps_to_something(self):
        for name, value in xbot.PROFILE_ENUM.items():
            self.assertIsNotNone(value, name)

    def test_the_yaml_values_are_the_profiles_the_component_carries(self):
        self.assertEqual(
            set(xbot.PROFILE_ENUM) - {"auto"}, self.transport_profile_names()
        )


if __name__ == "__main__":
    unittest.main()
