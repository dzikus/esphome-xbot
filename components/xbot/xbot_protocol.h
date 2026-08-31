#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace esphome::xbot {

// Five (service, write, notify) triples, one per Bluetooth module family. Which
// one a vehicle answers on is only knowable from a live GATT scan.
enum class Profile : uint8_t {
  UNKNOWN = 0,
  NUS = 1,
  AE00 = 2,
  FFE0 = 3,
  FFF0_F3F7 = 4,
  FFF0_F2F1 = 5,
};

struct TransportProfile {
  Profile id;
  const char *name;
  const char *service_uuid;
  const char *write_uuid;
  const char *notify_uuid;
};

// FFE0 deliberately pairs an ffe0 service with fff-family characteristics. A
// vehicle exposing ffe0+ffe1 is a sixth case, unmapped.
extern const TransportProfile PROFILE_TABLE[];
extern const size_t PROFILE_TABLE_LEN;

const TransportProfile *profile_by_id(Profile id);

struct DiscoveredChar {
  std::string service;
  std::string characteristic;
};

// First table entry whose write and notify characteristics both appear under
// its service. Table order is the tie-break for the two fff0 profiles.
Profile detect_profile(const std::vector<DiscoveredChar> &found);

// Triage for a buffer whose framing is not yet known. Kept for bringing up a
// vehicle that answers on a profile this component has never seen.
enum class Dialect : uint8_t {
  UNKNOWN = 0,
  XIAOMI = 1,   // 55 AA
  NINEBOT = 2,  // 5A A5
};

Dialect identify_dialect(std::span<const uint8_t> buf);

// 55 AA <payload_len+2> <dest> <cmd> <reg> <payload...> <~sum LE>. The checksum
// covers everything from the length byte onward. dest and cmd are not
// constants; they follow from the variant the advertised name selects.
//
// Written into the caller's buffer. Returns the length, or 0 when out is too
// small.
size_t build_request(std::span<uint8_t> out, uint8_t dest, uint8_t cmd, uint8_t reg, std::span<const uint8_t> payload);

// Same frame with a two-byte little-endian payload and the write opcode. Read
// and write dest are not always the same, so the caller passes both.
size_t build_write(std::span<uint8_t> out, uint8_t dest, uint8_t cmd, uint8_t reg, int16_t value);

// Enough for every frame this component builds: header, six payload bytes and
// the checksum.
inline constexpr size_t MAX_FRAME = 16;

// The name a vehicle advertises picks which protocol variant it speaks, and the
// variant fixes the obfuscation key, the address reads go to and the read
// opcode. Matching is case-sensitive and by substring anywhere in the name,
// first hit wins, so a pattern has to be tested before any shorter pattern it
// contains. Not to be confused with the vehicle's riding mode.
struct ProtocolVariant {
  uint8_t id;
  uint8_t xor_key;  // 0 leaves frames in the clear
  uint8_t read_dest;
  uint8_t read_cmd;
};

// Variant 3 is the one this component decodes. Others are still recognised, so
// a vehicle can be turned away instead of answered in a language it does not
// speak. A name matching nothing is variant 3.
inline constexpr uint8_t SUPPORTED_VARIANT = 3;

ProtocolVariant variant_for_name(std::string_view name);

// The whole frame, header and checksum included, is xored byte by byte, both
// directions, with a key the variant selects. A frame sent in the clear is
// dropped before the controller looks at the header. Key 0 disables it.
inline constexpr uint8_t XOR_KEY_VARIANT3 = 0x34;

void apply_xor(std::span<uint8_t> buf, uint8_t key);

// Sum of everything from the length byte to the last payload byte, inverted,
// compared against the trailing 16-bit little-endian field.
bool checksum_ok(std::span<const uint8_t> frame);

struct RegisterValue {
  uint8_t reg;
  uint16_t value;
};

// Notifications are not frame-aligned, so the caller accumulates and hands the
// whole buffer over. Each complete, checksum-valid frame is passed to fn as a
// view into that buffer.
// Returns how many leading bytes were consumed, junk and resyncs included; the
// caller keeps the rest for the next notification.
template <typename F>
size_t extract_frames(std::span<const uint8_t> buf, F &&fn) {
  size_t pos = 0;
  while (true) {
    // Skip anything before a header: a truncated or corrupt frame must not
    // stall every frame behind it.
    while (pos + 1 < buf.size() && (buf[pos] != 0x55 || buf[pos + 1] != 0xAA))
      pos++;
    if (buf.size() - pos < 3)
      return pos;

    const size_t total = static_cast<size_t>(buf[pos + 2]) + 6;
    if (buf.size() - pos < total)
      return pos;

    const std::span<const uint8_t> frame = buf.subspan(pos, total);
    if (checksum_ok(frame)) {
      fn(frame);
      pos += total;
    } else {
      // Header bytes can appear inside a payload; resync past this one.
      pos += 2;
    }
  }
}

// A reply carries little-endian 16-bit registers numbered upward from the
// frame's register byte, handed over one at a time.
template <typename F>
void decode_registers(std::span<const uint8_t> frame, F &&fn) {
  if (frame.size() < 8)
    return;
  const uint8_t base = frame[5];
  const size_t payload = frame.size() - 8;
  for (size_t i = 0; i + 1 < payload; i += 2) {
    // A reply longer than the map would wrap the register number.
    if (static_cast<size_t>(base) + (i / 2) > 0xFF)
      break;
    fn(RegisterValue{.reg = static_cast<uint8_t>(base + (i / 2)),
                     .value = static_cast<uint16_t>(frame[6 + i] | (frame[6 + i + 1] << 8))});
  }
}

// Share of distinct byte values over the buffer, 0-100. Encrypted payloads score
// high; telemetry repeats zeros and small integers.
uint8_t byte_diversity_pct(std::span<const uint8_t> buf);

std::string hex_dump(const uint8_t *data, size_t len);

}  // namespace esphome::xbot
