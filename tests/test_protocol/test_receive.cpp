#include <unity.h>

#include <span>
#include <vector>

#include "test_support.h"
#include "xbot_receive.h"

using namespace esphome::xbot;

void test_accumulator_holds_a_frame_split_across_notifications() {
  RxAccumulator<512> rx;
  std::vector<uint8_t> frame(VEHICLE_REPLY, VEHICLE_REPLY + sizeof(VEHICLE_REPLY));
  int seen = 0;
  TEST_ASSERT_EQUAL_UINT32(0, rx.append(frame.data(), 17, 0));
  rx.drain([&seen](std::span<const uint8_t>) { seen++; });
  TEST_ASSERT_EQUAL_INT(0, seen);
  TEST_ASSERT_EQUAL_UINT32(17, rx.size());

  rx.append(frame.data() + 17, frame.size() - 17, 0);
  rx.drain([&seen](std::span<const uint8_t> f) {
    seen++;
    TEST_ASSERT_EQUAL_UINT32(40, f.size());
  });
  TEST_ASSERT_EQUAL_INT(1, seen);
  TEST_ASSERT_EQUAL_UINT32(0, rx.size());
}

void test_accumulator_keeps_the_tail_and_undoes_the_key() {
  RxAccumulator<512> rx;
  std::vector<uint8_t> keyed(VEHICLE_REPLY, VEHICLE_REPLY + sizeof(VEHICLE_REPLY));
  apply_xor(keyed, XOR_KEY_VARIANT3);
  // One whole frame plus the first bytes of the next one.
  rx.append(keyed.data(), keyed.size(), XOR_KEY_VARIANT3);
  rx.append(keyed.data(), 5, XOR_KEY_VARIANT3);
  int seen = 0;
  rx.drain([&seen](std::span<const uint8_t> f) {
    seen++;
    TEST_ASSERT_EQUAL_UINT8(0xEA, f[5]);
  });
  TEST_ASSERT_EQUAL_INT(1, seen);
  // The unparsed head of the next frame survives for the notification after it.
  TEST_ASSERT_EQUAL_UINT32(5, rx.size());
}

void test_accumulator_drops_rather_than_overflows() {
  RxAccumulator<64> rx;
  std::vector<uint8_t> junk(60, 0x11);
  TEST_ASSERT_EQUAL_UINT32(0, rx.append(junk.data(), junk.size(), 0));
  TEST_ASSERT_EQUAL_UINT32(60, rx.size());

  // The next payload cannot fit beside it, so what was held is reported dropped.
  TEST_ASSERT_EQUAL_UINT32(60, rx.append(junk.data(), 10, 0));
  TEST_ASSERT_EQUAL_UINT32(10, rx.size());

  // A payload larger than the whole buffer is not stored at all.
  std::vector<uint8_t> huge(100, 0x22);
  TEST_ASSERT_EQUAL_UINT32(10, rx.append(huge.data(), huge.size(), 0));
  TEST_ASSERT_EQUAL_UINT32(0, rx.size());
}
