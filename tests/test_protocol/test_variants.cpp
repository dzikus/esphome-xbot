#include <unity.h>

#include "xbot_protocol.h"

using namespace esphome::xbot;

static void expect_variant(const char *name, uint8_t id, uint8_t key, uint8_t dest, uint8_t cmd) {
  ProtocolVariant v = variant_for_name(name);
  TEST_ASSERT_EQUAL_UINT8(id, v.id);
  TEST_ASSERT_EQUAL_UINT8(key, v.xor_key);
  TEST_ASSERT_EQUAL_UINT8(dest, v.read_dest);
  TEST_ASSERT_EQUAL_UINT8(cmd, v.read_cmd);
}

void test_variant_from_name() {
  // The vehicle this was tried on, and the fallback for a name that matches
  // nothing, are the same variant.
  expect_variant("M0Robot", 3, 0x34, 0x20, 0x61);
  // The serial suffix a vehicle appends must not change the match.
  expect_variant("M0Robot_with_a_suffix", 3, 0x34, 0x20, 0x61);
  expect_variant("", 3, 0x34, 0x20, 0x61);
  expect_variant("something else entirely", 3, 0x34, 0x20, 0x61);

  // Same framing, no obfuscation.
  expect_variant("X0Robot123", 3, 0x00, 0x20, 0x61);
  expect_variant("MIScooter", 3, 0x00, 0x20, 0x61);

  expect_variant("MiniRobot", 1, 0x55, 0x0A, 0x01);
  expect_variant("M5Robot", 1, 0xD8, 0x0A, 0x01);
  expect_variant("A6Robot", 2, 0xD8, 0x0A, 0x01);
  expect_variant("W1", 5, 0x37, 0x0A, 0x01);
  expect_variant("GoKart", 4, 0x00, 0x0A, 0x01);
}

void test_variant_ordering_traps() {
  // Each of these contains a shorter pattern that must not win.
  expect_variant("M0Clean77", 6, 0x00, 0x32, 0x01);   // contains M0
  expect_variant("TK2-S99", 3, 0x34, 0x20, 0x61);     // contains TK
  expect_variant("TK2_Y99", 3, 0x34, 0x20, 0x61);     // contains TK
  expect_variant("TK9000", 7, 0x00, 0x0A, 0x01);      // bare TK still lands
  expect_variant("miniPLUS_9", 1, 0x00, 0x04, 0x01);  // not the later Plus rule

  // Matching is case-sensitive, so a lowercase name falls through.
  expect_variant("ninebot", 3, 0x34, 0x20, 0x61);
  expect_variant("Ninebot", 1, 0x00, 0x0A, 0x01);
}

void test_variant_from_name_shape() {
  // Long form and short form are different variants, and which pattern matched
  // picks the key.
  expect_variant("M1abcdef", 1, 0x7A, 0x0A, 0x01);
  expect_variant("M1abcd", 3, 0x34, 0x20, 0x61);

  // An X in the first two characters counts; the third does not.
  expect_variant("XYabcde", 1, 0x18, 0x0A, 0x01);
  expect_variant("aXbcdefg", 1, 0x18, 0x0A, 0x01);
  expect_variant("XYabcd", 3, 0x34, 0x20, 0x61);
  expect_variant("abXcdefg", 3, 0x34, 0x20, 0x61);
}
