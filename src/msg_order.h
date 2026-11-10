// SPDX-License-Identifier: MIT
// EST-365: the order of the `msgs` USB dump. The channel ring and every DM thread are restored from
// the card at boot with the uptime stamp of the boot they were recorded in, so sorting a mixed list
// by `ts` (uptime) put a restored 19:11 message AFTER messages that arrived at 20:38 in the new
// boot. The screens show a chat in ring order (oldest first, newest last); across a restart the only
// stamp that survives both boots is the wall clock the screens print. This header is the one rule,
// plain C++, so test/host can build a restored ring plus new arrivals.
#pragma once

#include <stdint.h>

namespace om {

struct MsgOrderKey {
  uint32_t wall = 0;    // system-clock seconds when the message was stored (0 = not known)
  bool wallReal = false;  // a real source had set the clock then; a fake 2024 stamp is not real
  uint32_t uptime = 0;  // seconds since THAT boot
  uint32_t seq = 0;     // the ring's per-boot sequence: restored entries get theirs at boot
  bool dm = false;      // channel messages before DMs only as the final, stable tie-break
  uint8_t thread = 0;
};

// True when `a` is older than `b`, i.e. `a` must be printed first.
//
// Primary key: a REAL wall time. It is the field both boots share, so a restored 19:11 entry sorts
// before an arrival at 20:38 whatever their uptimes were. A message whose clock was not real sorts
// before every real one (it is either old or has no date at all; printing it first keeps the
// newest-last rule). When both have no real wall, the sequence decides: restore allocated the
// restored entries first, so this boot's arrivals always follow them. `uptime` is deliberately NOT
// a key: it is the field that was wrong across the reboot.
constexpr bool msgOrderBefore(const MsgOrderKey& a, const MsgOrderKey& b) {
  const uint32_t aw = a.wallReal ? a.wall : 0;
  const uint32_t bw = b.wallReal ? b.wall : 0;
  if (aw != bw) return aw < bw;
  if (a.seq != b.seq) return a.seq < b.seq;
  if (a.dm != b.dm) return !a.dm;
  return a.thread < b.thread;
}

}  // namespace om
