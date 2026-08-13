#include <unity.h>

#include <array>
#include <string>
#include <vector>

#include "test_support.h"

// The one place the implementation is pulled in; the other test files see only
// the headers and link against what this compiles.
#include "xbot_protocol.cpp"

using namespace esphome::xbot;

static const char *NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";

static DiscoveredChar dc(const char *s, const char *c) {
  return DiscoveredChar{std::string(s), std::string(c)};
}

// The parser hands frames to a callback and never allocates. These put the
// results back into containers so the assertions can stay exactly as they were
// before that changed.
static std::vector<std::vector<uint8_t>> collect_frames(std::vector<uint8_t> &buf) {
  std::vector<std::vector<uint8_t>> out;
  size_t consumed = extract_frames(buf, [&out](std::span<const uint8_t> f) {
    out.emplace_back(f.begin(), f.end());
  });
  buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(consumed));
  return out;
}

static std::vector<uint8_t> make_request(uint8_t dest, uint8_t cmd, uint8_t reg,
                                         std::vector<uint8_t> payload) {
  std::array<uint8_t, MAX_FRAME> buf{};
  size_t n = build_request(buf, dest, cmd, reg, payload);
  return std::vector<uint8_t>(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
}

static std::vector<uint8_t> make_write(uint8_t dest, uint8_t cmd, uint8_t reg, int16_t value) {
  std::array<uint8_t, MAX_FRAME> buf{};
  size_t n = build_write(buf, dest, cmd, reg, value);
  return std::vector<uint8_t>(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
}

static std::vector<RegisterValue> collect_registers(std::span<const uint8_t> frame) {
  std::vector<RegisterValue> out;
  decode_registers(frame, [&out](RegisterValue rv) { out.push_back(rv); });
  return out;
}

void test_profile_ids_are_unique_and_resolve() {
  for (size_t i = 0; i < PROFILE_TABLE_LEN; i++) {
    TEST_ASSERT_EQUAL_PTR(&PROFILE_TABLE[i], profile_by_id(PROFILE_TABLE[i].id));
    for (size_t j = i + 1; j < PROFILE_TABLE_LEN; j++) {
      TEST_ASSERT_FALSE(PROFILE_TABLE[i].id == PROFILE_TABLE[j].id);
    }
  }
  TEST_ASSERT_NULL(profile_by_id(Profile::UNKNOWN));
}

// Typed out here. Building the input from the row it verifies would use a
// typo'd uuid on both sides and pass.
struct ExpectedProfile {
  Profile id;
  const char *service;
  const char *write;
  const char *notify;
};
static const ExpectedProfile EXPECTED[] = {
    {Profile::NUS, "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
     "6e400002-b5a3-f393-e0a9-e50e24dcca9e", "6e400003-b5a3-f393-e0a9-e50e24dcca9e"},
    {Profile::AE00, "0000ae00-0000-1000-8000-00805f9b34fb",
     "0000ae01-0000-1000-8000-00805f9b34fb", "0000ae02-0000-1000-8000-00805f9b34fb"},
    {Profile::FFE0, "0000ffe0-0000-1000-8000-00805f9b34fb",
     "0000fff3-0000-1000-8000-00805f9b34fb", "0000fff4-0000-1000-8000-00805f9b34fb"},
    {Profile::FFF0_F3F7, "0000fff0-0000-1000-8000-00805f9b34fb",
     "0000fff3-0000-1000-8000-00805f9b34fb", "0000fff7-0000-1000-8000-00805f9b34fb"},
    {Profile::FFF0_F2F1, "0000fff0-0000-1000-8000-00805f9b34fb",
     "0000fff2-0000-1000-8000-00805f9b34fb", "0000fff1-0000-1000-8000-00805f9b34fb"},
};

void test_profile_table_matches_the_uuids_it_should_carry() {
  TEST_ASSERT_EQUAL_UINT32(sizeof(EXPECTED) / sizeof(EXPECTED[0]), PROFILE_TABLE_LEN);
  for (size_t i = 0; i < PROFILE_TABLE_LEN; i++) {
    const TransportProfile *p = profile_by_id(EXPECTED[i].id);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING(EXPECTED[i].service, p->service_uuid);
    TEST_ASSERT_EQUAL_STRING(EXPECTED[i].write, p->write_uuid);
    TEST_ASSERT_EQUAL_STRING(EXPECTED[i].notify, p->notify_uuid);
  }
}

void test_every_profile_detects_from_its_own_pair() {
  for (size_t i = 0; i < PROFILE_TABLE_LEN; i++) {
    const ExpectedProfile &e = EXPECTED[i];
    std::vector<DiscoveredChar> found = {dc(e.service, e.write), dc(e.service, e.notify)};
    TEST_ASSERT_TRUE(detect_profile(found) == e.id);
  }
}

void test_detect_is_case_insensitive() {
  std::vector<DiscoveredChar> found = {
      dc("6E400001-B5A3-F393-E0A9-E50E24DCCA9E", "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"),
      dc("6E400001-B5A3-F393-E0A9-E50E24DCCA9E", "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"),
  };
  TEST_ASSERT_TRUE(detect_profile(found) == Profile::NUS);
}

void test_half_a_profile_is_not_a_match() {
  // write characteristic present, notify missing
  std::vector<DiscoveredChar> found = {
      dc(NUS_SVC, "6e400002-b5a3-f393-e0a9-e50e24dcca9e"),
  };
  TEST_ASSERT_TRUE(detect_profile(found) == Profile::UNKNOWN);
}

void test_right_chars_under_wrong_service_is_not_a_match() {
  std::vector<DiscoveredChar> found = {
      dc("0000180f-0000-1000-8000-00805f9b34fb", "0000fff3-0000-1000-8000-00805f9b34fb"),
      dc("0000180f-0000-1000-8000-00805f9b34fb", "0000fff7-0000-1000-8000-00805f9b34fb"),
  };
  TEST_ASSERT_TRUE(detect_profile(found) == Profile::UNKNOWN);
}

void test_empty_scan_is_unknown() {
  std::vector<DiscoveredChar> found;
  TEST_ASSERT_TRUE(detect_profile(found) == Profile::UNKNOWN);
}

void test_identify_dialect() {
  const uint8_t xiaomi[] = {0x55, 0xAA, 0x03, 0x20};
  const uint8_t ninebot[] = {0x5A, 0xA5, 0x02, 0x3D};
  const uint8_t other[] = {0x01, 0x02, 0x03};
  const uint8_t one[] = {0x55};
  TEST_ASSERT_TRUE(identify_dialect(xiaomi) == Dialect::XIAOMI);
  TEST_ASSERT_TRUE(identify_dialect(ninebot) == Dialect::NINEBOT);
  TEST_ASSERT_TRUE(identify_dialect(other) == Dialect::UNKNOWN);
  TEST_ASSERT_TRUE(identify_dialect(one) == Dialect::UNKNOWN);
}

void test_byte_diversity() {
  const uint8_t flat[] = {0, 0, 0, 0};
  const uint8_t all_distinct[] = {1, 2, 3, 4};
  const uint8_t half[] = {1, 1, 2, 2};
  TEST_ASSERT_EQUAL_UINT8(25, byte_diversity_pct(flat));
  TEST_ASSERT_EQUAL_UINT8(100, byte_diversity_pct(all_distinct));
  TEST_ASSERT_EQUAL_UINT8(50, byte_diversity_pct(half));
  TEST_ASSERT_EQUAL_UINT8(0, byte_diversity_pct(std::span<const uint8_t>{}));
}

void test_build_request_bulk_read() {
  // Bulk telemetry read on variant 3: dest 0x20, cmd 0x61, register 0x19, and a
  // payload byte asking for 0x78 bytes back.
  auto f = make_request(0x20, 0x61, 0x19, {0x78});
  const uint8_t want[] = {0x55, 0xAA, 0x03, 0x20, 0x61, 0x19, 0x78, 0xEA, 0xFE};
  TEST_ASSERT_EQUAL_UINT32(sizeof(want), f.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, f.data(), sizeof(want));

  auto g = make_request(0x21, 0x01, 0x14, {0x1E});
  const uint8_t want2[] = {0x55, 0xAA, 0x03, 0x21, 0x01, 0x14, 0x1E, 0xA8, 0xFF};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want2, g.data(), sizeof(want2));

  // empty payload still carries the +2 in the length byte
  auto e = make_request(0x20, 0x61, 0x00, {});
  TEST_ASSERT_EQUAL_UINT8(0x02, e[2]);
  TEST_ASSERT_EQUAL_UINT32(8, e.size());
}

void test_xor_covers_the_whole_frame() {
  // Header and checksum are xored too, so a frame sent in the clear is dropped
  // before the controller reads the header.
  std::vector<uint8_t> plain = {0x55, 0xAA, 0x06, 0xF0, 0xF1, 0xF2,
                                0xA1, 0xA2, 0xA3, 0xA4, 0x9C, 0xFA};
  const uint8_t wire[] = {0x61, 0x9E, 0x32, 0xC4, 0xC5, 0xC6,
                          0x95, 0x96, 0x97, 0x90, 0xA8, 0xCE};
  TEST_ASSERT_TRUE(checksum_ok(plain));
  apply_xor(plain, XOR_KEY_VARIANT3);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wire, plain.data(), sizeof(wire));

  // and it is its own inverse
  apply_xor(plain, XOR_KEY_VARIANT3);
  TEST_ASSERT_EQUAL_UINT8(0x55, plain[0]);

  std::vector<uint8_t> untouched = {0x55, 0xAA, 0x03};
  apply_xor(untouched, 0);
  TEST_ASSERT_EQUAL_UINT8(0x55, untouched[0]);
}

void test_build_write() {
  // Two payload bytes, little endian, write opcode, length byte counting both.
  // Settings go back to the same address they are read from, 0x20 on variant 3.
  auto f = make_write(0x20, 0x03, 0xF0, 827);
  // sum over 04 20 03 F0 3B 03 = 0x155, inverted = 0xFEAA, stored little endian
  const uint8_t want[] = {0x55, 0xAA, 0x04, 0x20, 0x03, 0xF0, 0x3B, 0x03, 0xAA, 0xFE};
  TEST_ASSERT_EQUAL_UINT32(sizeof(want), f.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, f.data(), sizeof(want));
  TEST_ASSERT_TRUE(checksum_ok(f));

  // negative values keep their two's complement encoding
  auto n = make_write(0x20, 0x03, 0x7D, -1);
  TEST_ASSERT_EQUAL_UINT8(0xFF, n[6]);
  TEST_ASSERT_EQUAL_UINT8(0xFF, n[7]);
  TEST_ASSERT_TRUE(checksum_ok(n));
}

void test_checksum_ok() {
  // Literal frame: sum over 03 20 61 19 78 = 0x115, inverted 0xFEEA.
  std::vector<uint8_t> f = {0x55, 0xAA, 0x03, 0x20, 0x61, 0x19, 0x78, 0xEA, 0xFE};
  TEST_ASSERT_TRUE(checksum_ok(f));
  f[6] ^= 0x01;
  TEST_ASSERT_FALSE(checksum_ok(f));
  std::vector<uint8_t> tooshort = {0x55, 0xAA, 0x03};
  TEST_ASSERT_FALSE(checksum_ok(tooshort));
}

void test_checksum_length_boundary() {
  // Seven bytes is the shortest frame that can carry one: two header, three
  // counted, two checksum. Sum over 00 23 01 is 0x24, inverted 0xFFDB.
  std::vector<uint8_t> shortest = {0x55, 0xAA, 0x00, 0x23, 0x01, 0xDB, 0xFF};
  TEST_ASSERT_TRUE(checksum_ok(shortest));

  // One byte less is refused whatever it holds. report_frames_ reads bytes 3
  // to 5 of everything that gets past here, so this bound is what keeps that
  // read inside the frame.
  std::vector<uint8_t> six(shortest.begin(), shortest.end() - 1);
  TEST_ASSERT_FALSE(checksum_ok(six));
  std::vector<uint8_t> empty;
  TEST_ASSERT_FALSE(checksum_ok(empty));
}

void test_extract_frames() {
  auto req = make_request(0x21, 0x01, 0x14, {0x1E});

  // split across two notifications, with junk in front
  std::vector<uint8_t> buf = {0xDE, 0xAD};
  buf.insert(buf.end(), req.begin(), req.begin() + 4);
  TEST_ASSERT_EQUAL_UINT32(0, collect_frames(buf).size());
  buf.insert(buf.end(), req.begin() + 4, req.end());
  auto got = collect_frames(buf);
  TEST_ASSERT_EQUAL_UINT32(1, got.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(req.data(), got[0].data(), req.size());
  TEST_ASSERT_EQUAL_UINT32(0, buf.size());

  // two back to back in one buffer
  buf.insert(buf.end(), req.begin(), req.end());
  buf.insert(buf.end(), req.begin(), req.end());
  TEST_ASSERT_EQUAL_UINT32(2, collect_frames(buf).size());

  // a bad checksum must not wedge the frame behind it
  auto bad = req;
  bad[bad.size() - 1] ^= 0xFF;
  buf.insert(buf.end(), bad.begin(), bad.end());
  buf.insert(buf.end(), req.begin(), req.end());
  auto after = collect_frames(buf);
  TEST_ASSERT_EQUAL_UINT32(1, after.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(req.data(), after[0].data(), req.size());
  TEST_ASSERT_EQUAL_UINT32(0, buf.size());
}

void test_length_byte_promising_more_than_arrives() {
  // A corrupt length byte claims more than follows. Nothing is returned and
  // nothing consumed, since the rest may still be on its way; the caller caps
  // the buffer for the case where it never is.
  auto req = make_request(0x20, 0x61, 0x19, {0x78});
  std::vector<uint8_t> buf = {0x55, 0xAA, 0xFF, 0x20, 0x61, 0x19};
  TEST_ASSERT_EQUAL_UINT32(0, collect_frames(buf).size());
  TEST_ASSERT_EQUAL_UINT32(6, buf.size());

  // Once enough bytes arrive to test it, the checksum fails, the header is
  // stepped over, and a good frame sitting behind it still comes out.
  buf.insert(buf.end(), 0x100, 0x00);
  buf.insert(buf.end(), req.begin(), req.end());
  auto got = collect_frames(buf);
  TEST_ASSERT_EQUAL_UINT32(1, got.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(req.data(), got[0].data(), req.size());
}

void test_vehicle_reply_decodes() {
  std::vector<uint8_t> frame(VEHICLE_REPLY, VEHICLE_REPLY + sizeof(VEHICLE_REPLY));
  TEST_ASSERT_TRUE(checksum_ok(frame));

  // Split, the way a notification delivers it.
  std::vector<uint8_t> buf(frame.begin(), frame.begin() + 17);
  TEST_ASSERT_EQUAL_UINT32(0, collect_frames(buf).size());
  buf.insert(buf.end(), frame.begin() + 17, frame.end());
  auto got = collect_frames(buf);
  TEST_ASSERT_EQUAL_UINT32(1, got.size());
  TEST_ASSERT_EQUAL_UINT32(0, buf.size());

  // Read off the bytes above by hand, one pair per register, low byte first.
  // Spot-checking a few left most of the only real frame in this file unused.
  static const uint16_t WANT[] = {
      0,     // 0xEA capability mask, bit 6 clear on this vehicle
      0,     // 0xEB
      1,     // 0xEC
      0,     // 0xED
      245,   // 0xEE wheel factor
      591,   // 0xEF drive limit
      591,   // 0xF0 sport limit, the same count here
      354,   // 0xF1 eco limit
      0,     // 0xF2 lights, off
      5000,  // 0xF3 cruise, thousandths of a km/h
      0,     // 0xF4
      0,     // 0xF5
      0,     // 0xF6
      141,   // 0xF7 lowest limit the vehicle allows
      0,     // 0xF8
      0,     // 0xF9
  };
  auto regs = collect_registers(got[0]);
  TEST_ASSERT_EQUAL_UINT32(sizeof(WANT) / sizeof(WANT[0]), regs.size());
  for (size_t i = 0; i < regs.size(); i++) {
    TEST_ASSERT_EQUAL_UINT8(0xEA + i, regs[i].reg);
    TEST_ASSERT_EQUAL_UINT16(WANT[i], regs[i].value);
  }
}

void test_decode_registers_edges() {
  std::vector<uint8_t> stub = {0x55, 0xAA, 0x00, 0x23, 0x01, 0x14, 0x00};
  TEST_ASSERT_EQUAL_UINT32(0, collect_registers(stub).size());

  // Fourth register from 0xFD would be 0x100, and must not wrap to 0x00.
  std::vector<uint8_t> high = {0x55, 0xAA, 0x0A, 0x23, 0x01, 0xFD};
  for (int i = 0; i < 8; i++) high.push_back(0x11);
  high.push_back(0x00);
  high.push_back(0x00);
  auto regs = collect_registers(high);
  TEST_ASSERT_EQUAL_UINT32(3, regs.size());
  TEST_ASSERT_EQUAL_UINT8(0xFF, regs[2].reg);

  // An odd trailing byte is not half a register.
  std::vector<uint8_t> odd = {0x55, 0xAA, 0x07, 0x23, 0x01, 0x14, 0x01, 0x00,
                              0x02, 0x00, 0x99, 0x00, 0x00};
  auto two = collect_registers(odd);
  TEST_ASSERT_EQUAL_UINT32(2, two.size());
  TEST_ASSERT_EQUAL_UINT16(2, two[1].value);
}

void test_hex_dump() {
  const uint8_t data[] = {0x00, 0x5A, 0xFF};
  TEST_ASSERT_EQUAL_STRING("00.5A.FF", hex_dump(data, sizeof(data)).c_str());
  TEST_ASSERT_EQUAL_STRING("", hex_dump(data, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("", hex_dump(nullptr, 3).c_str());
}
