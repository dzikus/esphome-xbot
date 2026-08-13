#pragma once

#ifdef USE_ESP32

#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace xbot {

// The vehicle is in range only for the few minutes around a ride, so without
// this every entity reads unknown for the days in between. Cumulative and slow
// moving values are kept; instantaneous ones are not, since a restored one
// would look current.
template<typename T> class PersistedValue {
 public:
  // The key comes from the hub, which folds in the vehicle address: renaming an
  // entity in yaml keeps its history, and a second vehicle on the same node
  // does not overwrite the first one's.
  void init(uint32_t key) { this->pref_ = global_preferences->make_preference<T>(key); }

  bool load(T *out) {
    if (!this->pref_.load(out)) return false;
    this->stored_ = *out;
    this->have_stored_ = true;
    return true;
  }

  void stage(T value) {
    if (this->have_stored_ && this->stored_ == value) {
      this->dirty_ = false;
      return;
    }
    this->staged_ = value;
    this->dirty_ = true;
  }

  // One write per usage cycle: the caller decides when the vehicle has been out
  // of reach long enough for the staged value to be the final one.
  bool flush() {
    if (!this->dirty_) return false;
    // Cleared only once the save worked. A full flash would otherwise drop the
    // value for good, because nothing stages it again until it next changes.
    if (!this->pref_.save(&this->staged_)) return false;
    this->dirty_ = false;
    this->stored_ = this->staged_;
    this->have_stored_ = true;
    return true;
  }

 protected:
  ESPPreferenceObject pref_;
  T staged_{};
  T stored_{};
  bool have_stored_{false};
  bool dirty_{false};
};

}  // namespace xbot
}  // namespace esphome

#endif  // USE_ESP32
