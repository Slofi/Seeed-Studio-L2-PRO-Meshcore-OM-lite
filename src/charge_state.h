// SPDX-License-Identifier: MIT
// EST-402: the status bar's charging bolt, as a rule with no Arduino in it.
//
// The ticket's one signal is STATUS0 (0x40) bit 7 VBUSOK of the USB-C port controller at 0x22, the
// flag EST-401 proved on the device: 0 on battery, 1 with a power-only 100 W charger and 1 on USB to
// the PC, while AIN1..AIN3 never moved. It says ONLY "external power is present". The cell voltage
// stays about 4.19 V both while charging and once full, and BC_LVL is the port's claim rather than
// proof, so this build does NOT guess "charging" vs "full" (ticket item 3).
//
// The read is cheap but not free, and the flag can twitch while a cable is inserted, so the UI uses a
// debounced copy: a new value is adopted only after TWO equal reads in a row. One bad read neither
// raises nor drops the bolt. UNKNOWN is a state of its own and it NEVER shows a bolt: a chip that
// did not answer at boot, a POWER write that was not verified, or a STATUS0 read that failed all
// leave the bar exactly as it is today.
#pragma once

#include <stdint.h>

namespace om {

// One STATUS0 read, as the loop saw it. Unavailable is a failed I2C read, never "no cable".
enum class VbusRead : uint8_t { Absent = 0, Present, Unavailable };

// What the bar may draw. Unknown and Absent both draw no bolt; they differ only in what the state
// means to a later ticket (and in the tests here).
enum class VbusState : uint8_t { Unknown = 0, Absent, Present };

// The ticket's 2 s poll. It lives here, next to the rule it feeds, so the loop and the host test use
// one number.
constexpr uint32_t kChargePollMs = 2000;

// Two equal reads in a row, then the state follows. `begin(false)` latches UNKNOWN: a controller that
// was not ready at boot never produces a bolt, whatever a later read says (ticket item 4).
class ChargeBolt {
 public:
  void begin(bool controllerReady) {
    ready_ = controllerReady;
    state_ = VbusState::Unknown;
    candidate_ = VbusRead::Unavailable;
    streak_ = 0;
  }

  // Feed one read. Returns true exactly when the debounced state changed, i.e. when the screen needs
  // one repaint. Reads are ignored (and cannot change anything) while the controller was not ready.
  bool sample(VbusRead read) {
    if (!ready_) return false;
    if (read == candidate_) {
      if (streak_ < 2) ++streak_;
    } else {
      candidate_ = read;
      streak_ = 1;
    }
    if (streak_ < 2) return false;   // one read is not a decision
    const VbusState next = (candidate_ == VbusRead::Present) ? VbusState::Present
                           : (candidate_ == VbusRead::Absent) ? VbusState::Absent
                                                              : VbusState::Unknown;
    if (next == state_) return false;
    state_ = next;
    return true;
  }

  VbusState state() const { return state_; }
  bool bolt() const { return state_ == VbusState::Present; }
  bool unknown() const { return state_ == VbusState::Unknown; }
  bool controllerReady() const { return ready_; }

 private:
  bool ready_ = false;
  VbusState state_ = VbusState::Unknown;
  VbusRead candidate_ = VbusRead::Unavailable;
  uint8_t streak_ = 0;
};

}  // namespace om
