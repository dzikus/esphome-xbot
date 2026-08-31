#include "xbot_triage.h"

#include <vector>

#include "esphome/core/log.h"
#include "xbot_protocol.h"

namespace esphome::xbot {

static const char *const TAG = "xbot";

static const char *dialect_str(Dialect d) {
  switch (d) {
    case Dialect::XIAOMI:
      return "55AA";
    case Dialect::NINEBOT:
      return "5AA5";
    default:
      return "none";
  }
}

void log_unparsed(std::span<const uint8_t> raw, uint8_t key) {
  const Dialect plain = identify_dialect(raw);

  std::vector<uint8_t> keyed(raw.begin(), raw.end());
  apply_xor(keyed, key);
  const Dialect unkeyed = identify_dialect(keyed);

  const uint8_t diversity = byte_diversity_pct(raw);
  const char *verdict;
  if (unkeyed != Dialect::UNKNOWN) {
    verdict = "framing found under the current key, wait for a full frame";
  } else if (plain != Dialect::UNKNOWN) {
    verdict = "framing found in the clear, this vehicle wants xor key 0";
  } else if (diversity > 80) {
    verdict = "no framing either way and bytes look random, likely a different key";
  } else {
    verdict = "no framing either way, likely a different variant or command set";
  }

  ESP_LOGW(TAG, "nothing parses: raw=%s xored=%s diversity=%u%% -> %s", dialect_str(plain), dialect_str(unkeyed),
           diversity, verdict);
  ESP_LOGW(TAG, "first bytes: %s", hex_dump(raw.data(), raw.size() > 16 ? 16 : raw.size()).c_str());
}

}  // namespace esphome::xbot
