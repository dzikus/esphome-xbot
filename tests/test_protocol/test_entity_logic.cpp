#include <unity.h>

#include "xbot_entity_logic.h"

using namespace esphome::xbot;

// The wheel factor and divisor this vehicle reports, so the numbers below are
// the ones a real slider produces.
static const float FACTOR_245 = 5794.65283203125f / 245.0f;

void test_scale_refused_until_the_vehicle_reports_a_factor() {
  TEST_ASSERT_FALSE(scale_from_factor(0, 5794.65283203125f).has_value());
  auto s = scale_from_factor(245, 5794.65283203125f);
  TEST_ASSERT_TRUE(s.has_value());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.65f, *s);
}

void test_readback_inside_and_outside_the_declared_bounds() {
  // 827 counts is the Sport limit this vehicle holds: 35 km/h.
  auto ok = readback_value(827, FACTOR_245, 1.0f, 6.0f, 35.0f);
  TEST_ASSERT_TRUE(ok.has_value());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 35.0f, *ok);

  // Same count read with no scale at all is 827 km/h, and must not be shown.
  TEST_ASSERT_FALSE(readback_value(827, 1.0f, 1.0f, 6.0f, 35.0f).has_value());
  // Below the floor is refused just as hard as above the ceiling.
  TEST_ASSERT_FALSE(readback_value(23, FACTOR_245, 1.0f, 6.0f, 35.0f).has_value());
  // A step of zero must not divide by zero; the value passes through.
  TEST_ASSERT_TRUE(readback_value(827, FACTOR_245, 0.0f, 6.0f, 35.0f).has_value());
}

void test_write_count_truncates_and_refuses_what_will_not_fit() {
  // 35 x 23.6516 = 827.8, and the vehicle reads back 827, not 828.
  auto c = write_count(35.0f, FACTOR_245);
  TEST_ASSERT_TRUE(c.has_value());
  TEST_ASSERT_EQUAL_INT16(827, *c);

  // A factor of 1 makes the scale enormous, and every value overflows int16
  // so it cannot wrap to a small limit.
  auto huge = scale_from_factor(1, 5794.65283203125f);
  TEST_ASSERT_TRUE(huge.has_value());
  TEST_ASSERT_FALSE(write_count(6.0f, *huge).has_value());

  // Negative counts are legal for registers that take them.
  auto neg = write_count(-1.0f, 1.0f);
  TEST_ASSERT_TRUE(neg.has_value());
  TEST_ASSERT_EQUAL_INT16(-1, *neg);
}

void test_apply_bit_leaves_the_neighbours_alone() {
  // Zero start is bit 0 of a register that carries other flags.
  TEST_ASSERT_EQUAL_UINT16(0x0007, apply_bit(0x0006, 0x0001, true));
  TEST_ASSERT_EQUAL_UINT16(0x0006, apply_bit(0x0007, 0x0001, false));
  // Setting a bit already set, and clearing one already clear, change nothing.
  TEST_ASSERT_EQUAL_UINT16(0x0007, apply_bit(0x0007, 0x0001, true));
  TEST_ASSERT_EQUAL_UINT16(0x0006, apply_bit(0x0006, 0x0001, false));
}

void test_bounded_and_signed_registers() {
  TEST_ASSERT_TRUE(bounded(38.39f, 0.0f, 120.0f).has_value());
  TEST_ASSERT_FALSE(bounded(655.35f, 0.0f, 120.0f).has_value());
  TEST_ASSERT_TRUE(bounded(0.0f, 0.0f, 100.0f).has_value());

  // Read unsigned, -2.0 degC publishes 6551.6.
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 65516.0f, register_to_float(0xFFEC, false));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, register_to_float(0xFFEC, true));

  // Total distance is two registers, low word first: 348592 m is 0x0005_51B0.
  TEST_ASSERT_EQUAL_UINT32(348592, join_words(0x51B0, 0x0005));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, join_words(0xFFFF, 0xFFFF));
}
