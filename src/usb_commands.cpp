// SPDX-License-Identifier: MIT
// EST-371: the USB test commands, moved out of main.cpp. The frame reader stays in ota.cpp; this is
// the shell's own command set. Each command drives the same functions the screen uses, so a test
// proves the real path, and none of them can read a key or a secret. The shell's buffers are read
// through app_shell.h only.
#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <esp32-hal-psram.h>

#include "app_shell.h"
#include "audio.h"
#include "ble_link.h"
#include "board.h"
#include "channels.h"
#include "crash_report.h"
#include "fade.h"
#include "frame_metrics.h"
#include "history_store.h"
#include "diag_logic.h"
#include "map.h"
#include "mem_stats.h"
#include "msg_order.h"
#include "mesh_link.h"
#include "om_build.h"
#include "ota.h"
#include "power_log.h"
#include "radio.h"
#include "radio_diag.h"
#include "radio_stats.h"
#include "rptr_link.h"
#include "rptr_model.h"
#include "screens/sound_events.h"
#include "screens/toasts.h"   // EST-381: `notify test` enters the real notification door
#include "sound_synth.h"
#include "target.h"
#include "timekeeper.h"
#include "ui/age.h"
#include "ui/dm_delivery.h"
#include "ui/emoji.h"
#include "ui/notify.h"
#include "ui/screen_chat.h"
#include "ui/screen_nodes.h"
#include "ui/toast.h"
#include "usb_cmd.h"
#include "usb_cmd_logic.h"
#include "usb_frames.h"
#include "usb_touch.h"
#include "wifi_link.h"

// --------------------------------------------------------------------------- USB test commands
// The frame reader lives in ota.cpp; everything here is the shell. Each command goes through the
// same functions the screen uses, so a test proves the real path: send and dm use the exact facade
// calls the composer and the DM composer use, tap/drag/wake inject raw samples into the loop's own
// gesture and wake code, and no command can read a key or a secret. One log line per command, one
// final "[cmd] ok/error" line, and data lines only where the ticket asks for them.

constexpr unsigned kLoopStackSize = 16384;

namespace {

void cmdError(char* reply, size_t cap, const char* reason) {
  snprintf(reply, cap, "[cmd] error %s", reason != nullptr ? reason : "unknown");
}

// One printable line: control bytes and the quote that would end the field become apostrophes.
void cmdSafe(char* out, size_t cap, const char* in) {
  if (out == nullptr || cap == 0) return;
  size_t n = 0;
  if (in != nullptr) {
    for (; *in != 0 && n + 1 < cap; ++in) {
      const unsigned char c = (unsigned char)*in;
      out[n++] = (c < 0x20 || c == 0x7F || c == '"') ? '\'' : (char)c;
    }
  }
  out[n] = 0;
}

int cmdChannelByName(const char* name) {
  if (name == nullptr) return -1;
  for (int i = 0; i < om::mcChannelCount(); ++i) {
    if (strcmp(om::mcChannelName(i), name) == 0) return i;
  }
  return -1;
}

int cmdHexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// A public-key prefix as hex (the first 8 hex characters a `nodes` line prints, or more). Only the
// PUBLIC key is compared; no command ever formats a private key or a channel secret.
bool cmdKeyHasPrefix(const uint8_t* key, const char* hex) {
  if (key == nullptr || hex == nullptr) return false;
  const size_t len = strlen(hex);
  if (len < 2 || (len % 2) != 0 || len > 64) return false;
  for (size_t i = 0; i < len / 2; ++i) {
    const int hi = cmdHexNibble(hex[i * 2]);
    const int lo = cmdHexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    if (key[i] != (uint8_t)((hi << 4) | lo)) return false;
  }
  return true;
}

// Contact by public-key prefix. refreshNodes() gives the current names; the key comes from the
// facade one contact at a time, so this needs no second 64-contact buffer.
bool cmdContactByKeyPrefix(const char* hex, om::MeshContact* out) {
  om::app::refreshNodesNow();
  for (int i = 0; i < om::app::nodeCount(); ++i) {
    om::MeshContact c{};
    if (!om::meshContactByName(om::app::node(i).name, &c)) continue;
    if (cmdKeyHasPrefix(c.pub_key, hex)) {
      if (out) *out = c;
      return true;
    }
  }
  return false;
}

void cmdStamp(char* out, size_t cap, uint32_t wall, uint32_t uptime, bool wallReal) {
  om::ui::Stamp stamp;
  stamp.wall = wall;
  stamp.uptime = uptime;
  stamp.wallReal = wallReal;
  om::ui::stampText(out, cap, stamp, om::timeKeeper().now(), millis() / 1000,
                    om::timeKeeper().realSinceBoot(), om::ui::StampForm::DayTime);
}

int cmdPrintNodes() {
  om::app::refreshNodesNow();
  for (int i = 0; i < om::app::nodeCount(); ++i) {
    om::MeshContact c{};
    const bool known = om::meshContactByName(om::app::node(i).name, &c);
    char key[9] = "--------";
    const char* type = "node";
    char hops[8] = "--";
    if (known) {
      snprintf(key, sizeof(key), "%02x%02x%02x%02x", c.pub_key[0], c.pub_key[1], c.pub_key[2],
               c.pub_key[3]);
      type = om::ui::advTypeName(c.type);
      // Decoded hop count, never the encoded out_path_len (EST-344): 0 = direct, "--" = unknown.
      if (c.path_hops >= 0) snprintf(hops, sizeof(hops), "%d", (int)c.path_hops);
    }
    char name[64];
    char heard[32];
    cmdSafe(name, sizeof(name), om::app::node(i).name);
    cmdSafe(heard, sizeof(heard), om::app::node(i).age);
    Serial.printf("[node] name=\"%s\" type=%s key=%s heard=%s hops=%s\n", name, type, key, heard,
                  hops);
  }
  return om::app::nodeCount();
}

// The last n messages of both rings, oldest first. The channel and DM rings each keep 32, so 64 is
// the whole history; refs only carry scalars, and each DM text is resolved at print time from the
// one shared copy buffer, which keeps this out of RAM.
struct CmdMsgRef {
  uint32_t wall;
  uint32_t seq;
  uint8_t wallReal;
  uint8_t dm;
  uint8_t thread;
  uint16_t index;
};

// EST-365: the dump is ordered by the rule in msg_order.h, not by the uptime stamp. A restored
// message carries the uptime of the boot it was recorded in, so sorting by it put the restored
// history (19:11, uptime ~5 h) after the new arrivals (20:38, uptime ~2 min).
om::MsgOrderKey cmdMsgKey(const CmdMsgRef& r) {
  om::MsgOrderKey k;
  k.wall = r.wall;
  k.wallReal = r.wallReal != 0;
  k.seq = r.seq;
  k.dm = r.dm != 0;
  k.thread = r.thread;
  return k;
}

// EST-367 rule 6: the check Claude runs after every flash. One line per chat:
//   name, the badge's unread, what that chat's own view held when it opened (how long the load took),
//   how many messages are really on the card, and the newest time on the card vs the newest time the
//   view showed. A chat whose view and log disagree ends with MISMATCH:
//   unread > 0 while it showed nothing, or the card holds a newer message than the view showed.
int cmdPrintChats() {
  om::app::refreshChatsNow();
  const uint32_t nowWall = om::timeKeeper().now();
  const uint32_t nowUp = millis() / 1000;
  const bool nowReal = om::timeKeeper().realSinceBoot();
  int mismatches = 0;
  for (int i = 0; i < om::app::chatCount(); ++i) {
    const int chan = om::app::chatChannel(i);
    const int thread = om::app::chatThread(i);
    om::history::ChatSummary sum;
    const bool haveCard = om::meshChatSummary(chan, thread, &sum);
    const uint32_t cardNewest = haveCard ? sum.logId : 0;
    // The count: ONE forward read of this chat's files, capped at 256 KB per file so the check can
    // never freeze the loop on a full 2 MB log. `card_complete` false makes the number a lower bound,
    // printed with a '+' (the count is a diagnostic; the MISMATCH rule uses ids and times, not it).
    const uint32_t t0 = micros();
    bool cardComplete = true;
    const int card = om::meshHistoryCardMessages(chan, thread, &cardComplete);
    const uint32_t cardUs = micros() - t0;
    const om::app::ChatShownInfo cs = om::app::chatShown(i);
    const bool opened = cs.shown >= 0 && strcmp(cs.key, om::app::chat(i).name) == 0;
    int shown = opened ? cs.shown : -1;
    uint32_t shownNewest = opened ? cs.newestLogId : 0;
    uint32_t shownWall = opened ? cs.newestWall : 0;
    uint32_t shownUp = opened ? cs.newestUp : 0;
    bool shownReal = opened && cs.newestReal;
    // The chat open right now: its live view is the truth, not the memory of its open.
    if (i == om::app::chatSel() && !om::app::viewLoading() && om::app::viewChat() == om::app::chatSel()) {
      shown = om::app::viewCount();
      shownNewest = om::app::viewCount() > 0 ? om::app::viewMsg(om::app::viewCount() - 1).logId : 0;
      shownWall = om::app::viewCount() > 0 ? om::app::viewMsg(om::app::viewCount() - 1).wall : 0;
      shownUp = om::app::viewCount() > 0 ? om::app::viewMsg(om::app::viewCount() - 1).uptime : 0;
      shownReal = om::app::viewCount() > 0 && om::app::viewMsg(om::app::viewCount() - 1).wallReal();
    }
    const int unread = om::app::chat(i).unread;
    const bool bad = opened && om::ui::chatViewMismatch(shown, unread, cardNewest, shownNewest);
    if (bad) ++mismatches;
    char cardAt[16] = "-";
    char shownAt[16] = "-";
    if (cardNewest != 0) {
      om::ui::stampText(cardAt, sizeof(cardAt),
                        om::ui::Stamp{sum.wall, sum.uptime, sum.wallReal}, nowWall, nowUp, nowReal,
                        om::ui::StampForm::Clock);
    }
    if (shownNewest != 0) {
      om::ui::stampText(shownAt, sizeof(shownAt), om::ui::Stamp{shownWall, shownUp, shownReal},
                        nowWall, nowUp, nowReal, om::ui::StampForm::Clock);
    }
    char name[64];
    cmdSafe(name, sizeof(name), om::app::chat(i).name);
    char shownText[8];
    if (shown < 0) snprintf(shownText, sizeof(shownText), "-");
    else snprintf(shownText, sizeof(shownText), "%d", shown);
    char open[64] = "";
    if (opened) {
      snprintf(open, sizeof(open), " open_ms=%lu.%03lu windows=%lu", (unsigned long)(cs.openUs / 1000u),
               (unsigned long)(cs.openUs % 1000u), (unsigned long)cs.windows);
    }
    char cardText[12];
    snprintf(cardText, sizeof(cardText), "%d%s", card, cardComplete ? "" : "+");
    Serial.printf("[chat] \"%s\" unread=%d shown=%s card=%s card_read_ms=%lu.%03lu newest_card=%s "
                  "newest_shown=%s%s%s\n",
                  name, unread, shownText, cardText, (unsigned long)(cardUs / 1000u),
                  (unsigned long)(cardUs % 1000u), cardAt, shownAt, open,
                  bad ? " MISMATCH" : "");
  }
  return mismatches;
}

int cmdPrintMsgs(int want) {
  if (want < 0) want = 10;
  if (want > 64) want = 64;
  CmdMsgRef refs[64];
  int n = 0;
  for (int i = 0; i < om::meshMsgCount() && n < 64; ++i) {
    // EST-382: a cleared channel's ring slots are marked dead (ch = 0xFF). They are not messages
    // any more, so the dump skips them like the screens do.
    const int ch = om::meshMsgChannel(i);
    if (ch < 0 || ch >= om::mcChannelCount()) continue;
    CmdMsgRef& r = refs[n++];
    r.wall = om::meshMsgWallTs(i);
    r.seq = om::meshMsgSeq(i);
    r.wallReal = om::meshMsgWallReal(i) ? 1 : 0;
    r.dm = 0;
    r.thread = 0;
    r.index = (uint16_t)i;
  }
  for (int t = 0; t < om::meshDmCount() && n < 64; ++t) {
    const int k = om::meshDmList(t, om::app::dmScratch(), om::ui::kMaxChatMsgs);
    for (int j = 0; j < k && n < 64; ++j) {
      CmdMsgRef& r = refs[n++];
      r.wall = om::app::dmScratch()[j].wall;
      r.seq = om::app::dmScratch()[j].seq;
      r.wallReal = om::app::dmScratch()[j].wallReal ? 1 : 0;
      r.dm = 1;
      r.thread = (uint8_t)t;
      r.index = 0;
    }
  }
  // Insertion sort by the one cross-boot rule (msg_order.h): real wall time first, this boot's
  // sequence as the stable tie-break; uptime is never a key. Newest last, exactly as the screens.
  for (int i = 1; i < n; ++i) {
    const CmdMsgRef key = refs[i];
    const om::MsgOrderKey keyOrder = cmdMsgKey(key);
    int j = i - 1;
    while (j >= 0 && om::msgOrderBefore(keyOrder, cmdMsgKey(refs[j]))) {
      refs[j + 1] = refs[j];
      --j;
    }
    refs[j + 1] = key;
  }
  const int shown = want < n ? want : n;
  int printed = 0;
  for (int at = n - shown; at < n; ++at) {
    const CmdMsgRef& r = refs[at];
    char where[64], from[64], text[192], when[24];
    const char* state = "";
    if (!r.dm) {
      const int i = (int)r.index;
      const int ch = om::meshMsgChannel(i);
      cmdSafe(where, sizeof(where), ch >= 0 ? om::mcChannelName(ch) : "?");
      cmdSafe(from, sizeof(from), om::meshMsgName(i));
      cmdSafe(text, sizeof(text), om::meshMsgText(i));
      cmdStamp(when, sizeof(when), om::meshMsgWallTs(i), om::meshMsgTs(i), om::meshMsgWallReal(i));
      state = om::meshMsgMine(i) ? "sent" : "recv";
    } else {
      const int k = om::meshDmList((int)r.thread, om::app::dmScratch(), om::ui::kMaxChatMsgs);
      const om::MeshDm* found = nullptr;
      for (int j = 0; j < k; ++j) {
        if (om::app::dmScratch()[j].seq == r.seq) {
          found = &om::app::dmScratch()[j];
          break;
        }
      }
      if (found == nullptr) continue;   // it fell out of the ring while we printed
      cmdSafe(where, sizeof(where), om::meshDmName((int)r.thread));
      cmdSafe(from, sizeof(from), found->name);
      cmdSafe(text, sizeof(text), found->text);
      cmdStamp(when, sizeof(when), found->wall, found->ts, found->wallReal);
      state = om::ui::dmStateToken((om::ui::DmState)found->state);
    }
    Serial.printf("[msg] seq=%lu where=\"%s\" from=\"%s\" at=%s state=%s text=\"%s\"\n",
                  (unsigned long)r.seq, where, from, when, state, text);
    ++printed;
  }
  return printed;
}

}  // namespace

namespace om {

void usbCommandExecute(char* text, char* reply, size_t cap) {
  if (reply == nullptr || cap == 0) return;
  reply[0] = 0;

  // Exactly one log line per executed command. The ">" keeps it from ever looking like the answer
  // line: a command whose text began with "ok" must not be read as [cmd] ok by the host tool. The
  // text is made printable so a stray control byte cannot split the log, and it is printed before
  // the parser edits the line in place.
  char logged[om::usb::kMaxCommandBytes + 1];
  // EST-365: `rptr login <name> <password>` is logged in its redacted form; the password can never
  // reach the serial, a reply or an error line. Every other command is printed as before.
  om::usb::redactCommand(text, logged, sizeof(logged));
  Serial.printf("[cmd] > %s\n", logged[0] != 0 ? logged : "(empty)");

  const om::usb::Command cmd = om::usb::parseCommand(text);
  if (cmd.error != nullptr) {
    cmdError(reply, cap, cmd.error);
    return;
  }

  switch (cmd.kind) {
    case om::usb::Kind::Send: {
      if (!om::meshLinkReady()) {
        cmdError(reply, cap, "mesh down");
        return;
      }
      const int chan = cmdChannelByName(cmd.target);
      if (chan < 0) {
        cmdError(reply, cap, "no such channel");
        return;
      }
      const int allow = om::meshOwnTextMaxFor(om::meshNodeName());
      const int len = (int)strlen(cmd.text);
      if (len > allow) {
        snprintf(reply, cap, "[cmd] error text too long (%d > %d)", len, allow);
        return;
      }
      // The same facade call the composer's Send key reaches (via chatSend), so the stored ring,
      // the budget and the air path are the real ones.
      if (!om::meshLinkSend(chan, cmd.text)) {
        cmdError(reply, cap, "send failed");
        return;
      }
      char channel[64];
      cmdSafe(channel, sizeof(channel), om::mcChannelName(chan));
      snprintf(reply, cap, "[cmd] ok send channel=\"%s\" bytes=%d", channel, len);
      return;
    }

    case om::usb::Kind::Dm: {
      if (!om::meshLinkReady()) {
        cmdError(reply, cap, "mesh down");
        return;
      }
      om::MeshContact contact{};
      bool known = om::meshContactByName(cmd.target, &contact);
      if (!known) known = cmdContactByKeyPrefix(cmd.target, &contact);
      if (!known) {
        cmdError(reply, cap, "no such contact");
        return;
      }
      // The DM composer's own door: the same thread the message sheet's "Direct message" row
      // opens, then the same sendMessage() path chatSend() calls.
      int thread = -1;
      if (!om::meshDmOpenByName(contact.name, &thread)) {
        cmdError(reply, cap, "no dm thread");
        return;
      }
      const int len = (int)strlen(cmd.text);
      if (len > om::kMeshDmTextMax) {
        snprintf(reply, cap, "[cmd] error text too long (%d > %d)", len, om::kMeshDmTextMax);
        return;
      }
      if (!om::meshDmSend(thread, cmd.text)) {
        cmdError(reply, cap, "dm send failed");
        return;
      }
      const int n = om::meshDmList(thread, om::app::dmScratch(), om::ui::kMaxChatMsgs);
      const uint32_t seq = n > 0 ? om::app::dmScratch()[n - 1].seq : 0;
      char name[64];
      cmdSafe(name, sizeof(name), contact.name);
      snprintf(reply, cap, "[cmd] ok dm to=\"%s\" seq=%lu state=sending", name,
               (unsigned long)seq);
      return;
    }

    case om::usb::Kind::Status: {
      om::app::refreshNodesNow();
      char name[64];
      cmdSafe(name, sizeof(name), om::meshLinkReady() ? om::meshNodeName() : "");
      const char* path = om::meshLinkReady() ? om::meshPathHashLabel() : "?";
      // EST-348: the history store's own line (sd / spiffs, bytes, worst flush).
      const om::history::HistoryStatus& hs = om::history::status();
      snprintf(reply, cap,
               "[cmd] ok status mesh=%s name=\"%s\" build=%s path=%s channels=%d contacts=%d dm=%d "
               "unread=%d clock=%s history=%s hist_bytes=%lu hist_chats=%d hist_records=%lu "
               "hist_worst_us=%lu hist_dropped=%lu hist_write_fail=%lu loop_stack_free=%u "
               "loop_stack_size=%u internal=%u internal_max=%u heap=%u psram=%u",
               om::meshLinkReady() ? "up" : "down", name, OM_BUILD_ID, path, om::mcChannelCount(), om::app::nodeCount(),
               om::meshDmCount(), om::app::unreadTotal(),
               om::timeSourceName(om::timeKeeper().source()),
               !hs.ready ? "off" : (hs.sd ? "sd" : "spiffs"), (unsigned long)hs.bytes, hs.slots,
               (unsigned long)hs.records, (unsigned long)hs.worstFlushUs,
               (unsigned long)hs.dropped, (unsigned long)hs.writeFail,
               (unsigned)uxTaskGetStackHighWaterMark(nullptr),
               kLoopStackSize, (unsigned)om::internalFreeBytes(),
               (unsigned)om::internalLargestBlock(), (unsigned)ESP.getFreeHeap(),
               (unsigned)ESP.getFreePsram());
      return;
    }

    case om::usb::Kind::Nodes: {
      // Data lines first, then the one terminating answer; tools/cmd.py prints until that line.
      const int count = cmdPrintNodes();
      snprintf(reply, cap, "[cmd] ok nodes %d", count);
      return;
    }

    case om::usb::Kind::Msgs: {
      const int count = cmdPrintMsgs(cmd.count);
      snprintf(reply, cap, "[cmd] ok msgs %d", count);
      return;
    }

    case om::usb::Kind::Chats: {
      // EST-367 rule 6: one [chat] line per chat, then one answer line. MISMATCH on a line means the
      // chat's own view and its log disagree (see cmdPrintChats).
      const int bad = cmdPrintChats();
      snprintf(reply, cap, "[cmd] ok chats %d mismatches=%d", om::app::chatCount(), bad);
      return;
    }

    case om::usb::Kind::History: {
      // The data lines are the stored log itself; the answer line terminates them for cmd.py.
      const int count = om::meshHistoryDump(cmd.target, cmd.count);
      if (count < 0) {
        cmdError(reply, cap, "no such chat");
        return;
      }
      char chat[64];
      cmdSafe(chat, sizeof(chat), cmd.target);
      snprintf(reply, cap, "[cmd] ok history %d chat=\"%s\"", count, chat);
      return;
    }

    case om::usb::Kind::Tap: {
      const int x = cmd.nums[0];
      const int y = cmd.nums[1];
      om::usbTouchTap((uint16_t)x, (uint16_t)y);
      snprintf(reply, cap, "[cmd] ok tap %d %d", x, y);
      return;
    }

    case om::usb::Kind::Drag: {
      // The injector clamps only the duration; coordinates outside the glass simply hit nothing,
      // exactly as a real finger there would.
      om::usbTouchDrag((uint16_t)cmd.nums[0], (uint16_t)cmd.nums[1], (uint16_t)cmd.nums[2],
                       (uint16_t)cmd.nums[3], (uint32_t)cmd.nums[4]);
      snprintf(reply, cap, "[cmd] ok drag %d %d %d %d %d", cmd.nums[0], cmd.nums[1], cmd.nums[2],
               cmd.nums[3], cmd.nums[4]);
      return;
    }

    case om::usb::Kind::Wifi: {
      // EST-354. Read and switch only: no word here carries an SSID or a password, and no path
      // prints one. `wifi scan` starts an asynchronous scan and answers at once; once the page (or
      // a second call) sees the finished list, the same command prints one [wifi] line per network.
      const char* sub = cmd.target;
      if (sub == nullptr) {
        cmdError(reply, cap, "args");
        return;
      }
      if (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0) {
        const bool on = strcmp(sub, "on") == 0;
        om::wifiSetEnabled(on);
        snprintf(reply, cap,
                 "[cmd] ok wifi switch=%s state=%s internal=%u internal_max=%u heap=%u psram=%u",
                 on ? "on" : "off", om::wifi::phaseToken(om::wifiStatus().phase),
                 (unsigned)om::internalFreeBytes(), (unsigned)om::internalLargestBlock(),
                 (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
        return;
      }
      if (strcmp(sub, "scan") == 0) {
        const int count = om::wifiScanCount();
        if (count > 0) {
          for (int i = 0; i < count; ++i) {
            const om::wifi::ScanResult* r = om::wifiScanAt(i);
            if (r == nullptr) continue;
            char ssid[96];
            cmdSafe(ssid, sizeof(ssid), r->ssid);
            Serial.printf("[wifi] ssid=\"%s\" rssi=%d secured=%d\n", ssid, (int)r->rssi,
                          r->secured ? 1 : 0);
          }
          snprintf(reply, cap, "[cmd] ok wifi scan %d", count);
          return;
        }
        if (!om::wifiIsEnabled()) {
          cmdError(reply, cap, "wifi off");
          return;
        }
        if (!om::wifiIsScanning()) om::wifiStartScan();
        // The loop keeps running while the driver scans. Call `wifi scan` again in a few seconds to
        // print the list; this line says so instead of blocking on the radio.
        snprintf(reply, cap, "[cmd] ok wifi scan 0 state=scanning");
        return;
      }
      const om::wifi::Status& st = om::wifiStatus();
      char ssid[96];
      cmdSafe(ssid, sizeof(ssid), st.ssid);
      snprintf(reply, cap,
               "[cmd] ok wifi switch=%s state=%s ssid=\"%s\" ip=%s rssi=%d reason=\"%s\" "
               "saved=%d scan=%d source=%s internal=%u internal_max=%u heap=%u psram=%u "
               "service_us_max=%lu mesh_serial=%lu",
               om::wifiIsEnabled() ? "on" : "off", om::wifi::phaseToken(st.phase), ssid,
               st.ip[0] ? st.ip : "-", (int)st.rssi, st.reason, om::wifiSavedCount(),
               om::wifiScanCount(), om::timeSourceName(om::timeKeeper().source()),
               (unsigned)om::internalFreeBytes(), (unsigned)om::internalLargestBlock(),
               (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
               (unsigned long)om::wifiServiceWorstUs(), (unsigned long)om::meshMsgSerial());
      return;
    }

    case om::usb::Kind::Ble: {
      // EST-356. The PIN is never printed on this surface: `pin_set` answers whether one exists.
      const char* sub = cmd.target;
      if (sub == nullptr) {
        cmdError(reply, cap, "args");
        return;
      }
      const bool on = strcmp(sub, "on") == 0;
      if (on || strcmp(sub, "off") == 0) {
        om::bleSetEnabled(on);
      }
      // The ESP32 BLE transport keeps no peer name (the phone never sends one in MeshCore's protocol
      // and the upstream class does not store the GAP name), so a connected peer is reported as
      // unknown rather than invented.
      const char* peer = om::bleConnected() ? "unknown" : "-";
      snprintf(reply, cap,
               "[cmd] ok ble switch=%s state=%s peer=\"%s\" pin_set=%d internal=%u internal_max=%u "
               "need=%u need_block=%u heap=%u psram=%u wifi=%s mesh_serial=%lu",
               om::bleIsEnabled() ? "on" : "off", om::ble::phaseToken(om::blePhase()), peer,
               om::blePin() != 0 ? 1 : 0, (unsigned)om::internalFreeBytes(),
               (unsigned)om::internalLargestBlock(), (unsigned)om::bleStartNeedFree(),
               (unsigned)om::bleStartNeedBlock(), (unsigned)om::bleFreeHeap(),
               (unsigned)om::bleFreePsram(),
               om::wifiIsEnabled() ? om::wifi::phaseToken(om::wifiStatus().phase) : "off",
               (unsigned long)om::meshMsgSerial());
      return;
    }

    case om::usb::Kind::Rptr: {
      // EST-358: the repeater tab from the host. Nothing here prints a stored password: `login=` says
      // only whether a remembered one was used.
      if (cmd.target == nullptr) {
        cmdError(reply, cap, "args");
        return;
      }
      if (strcmp(cmd.target, "scan") == 0) {
        const bool started = om::rptrScanStart();
        const om::rptr::FoundList& f = om::rptrScan();
        char state[40];
        f.stateText(state, sizeof(state));
        for (int i = 0; i < f.count; ++i) {
          const om::rptr::Found& r = f.rows[i];
          char snr[32] = "";
          om::rptr::snrBothText(snr, sizeof(snr), r.hasSnrIn, r.snrIn4, r.hasSnrOut, r.snrOut4);
          Serial.printf(
              "[rptr] row %d name=\"%s\" key=%02x%02x%02x key_bytes=%u snr=\"%s\" hops=%u "
              "saved=%d regions=\"%s\" clock=%lu\n",
              i, r.name[0] ? r.name : "?", r.prefix[0], r.prefix[1], r.prefix[2],
              (unsigned)r.keyLen, snr, (unsigned)r.hops, r.inContacts ? 1 : 0,
              r.anon.valid ? (r.anon.count > 0 ? r.anon.text : "none") : "?",
              (unsigned long)(r.anon.valid ? r.anon.clock : 0));
        }
        snprintf(reply, cap, "[cmd] ok rptr scan started=%d state=\"%s\" found=%d", started ? 1 : 0,
                 state, f.count);
        return;
      }
      // rptr regions [name]: the EST-360 anonymous nearby-region read (no login). Without a name it
      // asks every direct responder of the last scan; with a name it asks that one and prints the
      // stored answer when it already has one. The answer arrives asynchronously; the device serial
      // logs it the moment it lands.
      if (strcmp(cmd.target, "regions") == 0) {
        const om::rptr::FoundList& f = om::rptrScan();
        if (cmd.text == nullptr) {
          const int asked = om::rptrRegionsQueryAll();
          snprintf(reply, cap, "[cmd] ok rptr regions requested=%d busy=%d", asked,
                   om::rptrRegionsBusy() ? 1 : 0);
          return;
        }
        int idx = -1;
        char hex[8] = "";
        for (int i = 0; i < f.count; ++i) {
          snprintf(hex, sizeof(hex), "%02x%02x%02x", f.rows[i].prefix[0], f.rows[i].prefix[1],
                   f.rows[i].prefix[2]);
          if (f.rows[i].name[0] && strcmp(f.rows[i].name, cmd.text) == 0) {
            idx = i;
            break;
          }
          if (strcmp(hex, cmd.text) == 0) {   // an unnamed responder: the scan shows this hex
            idx = i;
            break;
          }
        }
        if (idx < 0) {
          snprintf(reply, cap,
                   "[cmd] error rptr regions \"%s\" is not in the last scan: run rptr scan first",
                   cmd.text);
          return;
        }
        const om::rptr::Found& r = f.rows[idx];
        if (r.anon.valid) {
          snprintf(reply, cap,
                   "[cmd] ok rptr regions name=\"%s\" clock=%lu clock_real=%d regions=\"%s\" "
                   "count=%d clean=%d",
                   cmd.text, (unsigned long)r.anon.clock, r.anon.clockValid ? 1 : 0, r.anon.text,
                   r.anon.count, r.anon.clean ? 1 : 0);
          return;
        }
        const bool asked = om::rptrRegionsQuery(idx);
        snprintf(reply, cap,
                 "[cmd] ok rptr regions name=\"%s\" requested=%d busy=%d (the answer is logged, then "
                 "run this again to print it)",
                 cmd.text, asked ? 1 : 0, om::rptrRegionsBusy() ? 1 : 0);
        return;
      }
      // EST-365: `rptr login <name> <password>`, through the screen's own path. The password is
      // filtered like the sheet filters it, handed to rptrLoginStart() (which stores it only when
      // the login succeeds, the screen's remember rule), and zeroed here. No reply field ever
      // carries it; the serial log above is the redacted form.
      if (strcmp(cmd.target, "login") == 0) {
        if (!om::meshLinkReady()) {
          cmdError(reply, cap, "mesh down");
          return;
        }
        if (cmd.text == nullptr || cmd.text[0] == 0 || cmd.text2 == nullptr) {
          cmdError(reply, cap, "args");
          return;
        }
        char pw[om::rptr::kPasswordMax + 1] = "";
        if (om::rptr::passwordFilter(cmd.text2, pw, sizeof(pw)) <= 0) {
          cmdError(reply, cap, "password");
          return;
        }
        const om::rptr::LoginMachine& lm = om::rptrLogin();
        if (om::rptrLoggedIn() && strcmp(om::rptrCurrentName(), cmd.text) == 0) {
          snprintf(reply, cap, "[cmd] ok rptr login name=\"%s\" result=%s rtt=%lu", cmd.text,
                   om::rptr::loginStateText(lm.state), (unsigned long)lm.rttMs);
          memset(pw, 0, sizeof(pw));
          return;
        }
        if (lm.busy() && strcmp(om::rptrCurrentName(), cmd.text) == 0) {
          snprintf(reply, cap, "[cmd] ok rptr login name=\"%s\" result=sending rtt=0",
                   cmd.text);
          memset(pw, 0, sizeof(pw));
          return;
        }
        const bool sent = om::rptrLoginStart(cmd.text, pw);
        memset(pw, 0, sizeof(pw));
        if (!sent) {
          snprintf(reply, cap,
                   "[cmd] error rptr login name=\"%s\": no such repeater contact, run rptr scan "
                   "first",
                   cmd.text);
          return;
        }
        snprintf(reply, cap, "[cmd] ok rptr login name=\"%s\" result=sent rtt=0", cmd.text);
        return;
      }
      // rptr status <name>
      const char* name = cmd.text;
      if (!om::rptrLoggedIn() || strcmp(om::rptrCurrentName(), name) != 0) {
        char pw[om::rptr::kPasswordMax + 1];
        if (!om::rptrPasswordFor(name, pw, sizeof(pw))) {
          snprintf(reply, cap,
                   "[cmd] error rptr no_login: no remembered password for \"%s\", log in on the "
                   "RPTR tab first",
                   name);
          return;
        }
        const bool sent = om::rptrLoginStart(name, pw);
        memset(pw, 0, sizeof(pw));
        snprintf(reply, cap, "[cmd] ok rptr status name=\"%s\" login=%s", name,
                 sent ? "sent" : "failed");
        return;
      }
      const om::rptr::Status& st = om::rptrStatus();
      const uint32_t now = millis();
      if (!st.valid || (uint32_t)(now - st.atMs) > 20000) {
        const bool asked = om::rptrStatusRequest();
        snprintf(reply, cap, "[cmd] ok rptr status name=\"%s\" requested=%d", name, asked ? 1 : 0);
        return;
      }
      char up[20], air[20], snr[12];
      om::rptr::uptimeText(up, sizeof(up), st.uptimeSec);
      om::rptr::uptimeText(air, sizeof(air), st.airtimeSec);
      om::rptr::snrText(snr, sizeof(snr), st.lastSnr4);
      Serial.printf(
          "[rptr] status name=\"%s\" uptime=\"%s\" battery=%umV airtime=\"%s\" rx=%lu tx=%lu "
          "noise=%ddBm snr=\"%s\" firmware=%u clock=%lu rtt=%lums\n",
          name, up, (unsigned)st.battMv, air, (unsigned long)st.rxPackets,
          (unsigned long)st.txPackets, (int)st.noiseFloor, snr, (unsigned)st.fwLevel,
          (unsigned long)st.clock, (unsigned long)st.rttMs);
      snprintf(reply, cap,
               "[cmd] ok rptr status name=\"%s\" uptime=%lu battery_mv=%u airtime=%lu rx=%lu "
               "tx=%lu noise=%d snr4=%d firmware=%u rtt_ms=%lu login=%s",
               name, (unsigned long)st.uptimeSec, (unsigned)st.battMv,
               (unsigned long)st.airtimeSec, (unsigned long)st.rxPackets,
               (unsigned long)st.txPackets, (int)st.noiseFloor, (int)st.lastSnr4,
               (unsigned)st.fwLevel, (unsigned long)st.rttMs,
               om::rptr::loginStateText(om::rptrLogin().state));
      return;
    }

    // EST-365: what the L2 hears. The numbers are upstream's own counters (meshRadioStats) plus the
    // model's noise window and reception ring; the loop histogram names the slowest block. One
    // answer line terminates the data lines for cmd.py, exactly like `nodes`.
    case om::usb::Kind::Radio: {
      // EST-376: the three A/B forms only RECORD the request here. The RadioLib calls, the warm
      // sleep + calibrate RX re-entry and the noise restart are radio work, so rdiag::loop() runs
      // them on the loop task (EST-350); their status codes are logged by that loop line and kept
      // for `radio status`.
      if (cmd.target != nullptr && strcmp(cmd.target, "tcxo") == 0) {
        if (cmd.text == nullptr ||
            (strcmp(cmd.text, "1.8") != 0 && strcmp(cmd.text, "3.0") != 0)) {
          cmdError(reply, cap, "value");
          return;
        }
        const float volts = strcmp(cmd.text, "3.0") == 0 ? 3.0f : 1.8f;
        if (!om::rdiag::requestTcxo(volts)) {
          cmdError(reply, cap, "value");
          return;
        }
        snprintf(reply, cap, "[cmd] ok radio tcxo volts=%.1f pending=1", volts);
        return;
      }
      if (cmd.target != nullptr && strcmp(cmd.target, "boost") == 0) {
        if (cmd.text == nullptr ||
            (strcmp(cmd.text, "on") != 0 && strcmp(cmd.text, "off") != 0)) {
          cmdError(reply, cap, "value");
          return;
        }
        const bool on = strcmp(cmd.text, "on") == 0;
        om::rdiag::requestBoost(on);
        snprintf(reply, cap, "[cmd] ok radio boost on=%d pending=1", on ? 1 : 0);
        return;
      }
      if (cmd.target != nullptr && strcmp(cmd.target, "sample") == 0) {
        const uint32_t now = millis();
        if (om::rdiag::sampleActive()) {
          snprintf(reply, cap, "[cmd] error sample busy (%d s left)",
                   om::rdiag::sampleSecondsLeft(now));
          return;
        }
        if (!om::rdiag::sampleStart(cmd.count, now)) {
          cmdError(reply, cap, "value");
          return;
        }
        snprintf(reply, cap, "[cmd] ok radio sample secs=%d every=5s", cmd.count);
        return;
      }

      om::radio::UpstreamCounters c;
      const bool up = om::meshRadioStats(&c);
      const om::radio::Snapshot snap = om::radio::snapshot();
      if (cmd.target != nullptr && strcmp(cmd.target, "packets") == 0) {
        const int n = om::radio::packetCount();
        for (int i = n - 1; i >= 0; --i) {   // oldest first, newest last
          om::radio::PacketSample p;
          if (!om::radio::packetAt(i, &p)) continue;
          char when[24];
          cmdStamp(when, sizeof(when), p.atWall, p.atMs / 1000, p.atWallReal);
          char hash[9] = "--------";
          if (p.hashValid) {
            snprintf(hash, sizeof(hash), "%02x%02x%02x%02x", p.hash[0], p.hash[1], p.hash[2],
                     p.hash[3]);
          }
          Serial.printf(
              "[pkt] at=%s type=%s route=%s from=%s rssi=%d snr=%d.%02d path=%u size=%u len=%u\n",
              when, om::radio::payloadTypeName(p.type), om::radio::routeName(p.route), hash,
              (int)p.rssi, (int)(p.snr4 / 4),
              (int)(((p.snr4 < 0 ? -p.snr4 : p.snr4) % 4) * 25), p.pathCount, p.pathHashSize,
              (unsigned)p.len);
        }
        snprintf(reply, cap, "[cmd] ok radio packets %d", n);
        return;
      }
      // EST-376: `radio status` names the two diagnostics next to the line `radio` prints. The
      // values come from the loop's own state (the TCXO voltage last sent, the RX gain register
      // last read back), so this command does no radio work either.
      if (cmd.target != nullptr && strcmp(cmd.target, "status") == 0) {
        char tcxoStatus[12], boostStatus[12];
        int st = om::rdiag::lastTcxoStatus();
        if (st == om::rdiag::kStatusNone) snprintf(tcxoStatus, sizeof(tcxoStatus), "-");
        else snprintf(tcxoStatus, sizeof(tcxoStatus), "%d", st);
        st = om::rdiag::lastBoostStatus();
        if (st == om::rdiag::kStatusNone) snprintf(boostStatus, sizeof(boostStatus), "-");
        else snprintf(boostStatus, sizeof(boostStatus), "%d", st);
        Serial.printf("[radio] tcxo=%.1f boost=%d tcxo_status=%s boost_status=%s\n",
                      om::rdiag::tcxoVolts(), om::rdiag::boostOn() ? 1 : 0, tcxoStatus,
                      boostStatus);
      }
      const int noise = snap.noise.samples > 0 ? snap.noise.last : c.noiseFloor;
      // The USB reply buffer is 512 B, so the line is compact; the bucket boundaries are the
      // kHistogramBoundsUs list in radio_stats.h (1,2,5,10,20,50,100,200,500 ms, then "lots").
      char hist[128];
      int at = 0;
      hist[0] = 0;
      static const char* const kBucketNames[om::radio::kHistogramBuckets] = {
          "1", "2", "5", "10", "20", "50", "100", "200", "500", "lots"};
      for (int i = 0; i < om::radio::kHistogramBuckets && at < (int)sizeof(hist); ++i) {
        at += snprintf(hist + at, sizeof(hist) - (size_t)at, "%s%s:%lu", i == 0 ? "" : ",",
                       kBucketNames[i], (unsigned long)om::radio::histogramBucket(i));
      }
      snprintf(reply, cap,
               "[cmd] ok radio noise=%d min=%d avg=%d n=%d rssi=%d snr=%d.%02d pkts=%lu "
               "heard=%lu dup=%lu crc=%lu tx=%lu air=%lu rxa=%lu rxf=%lu rxd=%lu txf=%lu "
               "txd=%lu gain=%s loop_worst=%lu by=%s minute=%lu minute_by=%s last=%lu hist=%s",
               noise, snap.noise.min, snap.noise.avg, snap.noise.samples, c.lastRssi,
               c.lastSnr4 / 4, ((c.lastSnr4 < 0 ? -c.lastSnr4 : c.lastSnr4) % 4) * 25,
               (unsigned long)c.rxPackets, (unsigned long)snap.packetsHeard,
               (unsigned long)c.duplicates, (unsigned long)c.crcErrors,
               (unsigned long)c.txPackets, (unsigned long)(c.txAirMs / 1000),
               (unsigned long)(c.rxAirMs / 1000), (unsigned long)c.recvFlood,
               (unsigned long)c.recvDirect, (unsigned long)c.sentFlood,
               (unsigned long)c.sentDirect,
               !up ? "?" : (c.boostedKnown ? (c.boosted ? "boosted" : "power") : "?"),
               (unsigned long)snap.worstUs, om::radio::partName(snap.worstPart),
               (unsigned long)snap.worstMinuteUs, om::radio::partName(snap.worstMinutePart),
               (unsigned long)snap.lastPassUs, hist);
      return;
    }

    // EST-376: the read-only charge probe. Reads 0x01..0x10, 0x3C, 0x3D, 0x40 and 0x41 of the
    // USB-C port controller at 0x22 and the ADS1115's four single-ended inputs. NOTHING is written
    // to 0x22; 0x3E/0x3F/0x42 (interrupt, clear on read) and 0x43 (FIFO, a read pops a byte) are
    // not in the list, so the probe cannot change the chip's state.
    case om::usb::Kind::Pmu: {
      om::diag::RegisterSet regs;
      int read = 0;
      for (int i = 0; i < om::diag::kFusbReadCount; ++i) {
        const uint8_t reg = om::diag::kFusbReadRegs[i];
        if (!om::diag::fusbReadable(reg)) continue;   // the rule is diag_logic.h's, re-checked here
        regs.valid[reg] = om::usbPdReadRegister(reg, &regs.value[reg]);
        if (regs.valid[reg]) ++read;
      }
      const om::diag::Fusb302 f = om::diag::fusb302Fields(regs);

      // Data line 1: every register byte, in the order the list names them.
      char dump[192];
      int at = 0;
      dump[0] = 0;
      for (int i = 0; i < om::diag::kFusbReadCount && at < (int)sizeof(dump); ++i) {
        const uint8_t reg = om::diag::kFusbReadRegs[i];
        if (regs.valid[reg]) {
          at += snprintf(dump + at, sizeof(dump) - (size_t)at, "%s%02X=%02X", i == 0 ? "" : " ",
                         reg, regs.value[reg]);
        } else {
          at += snprintf(dump + at, sizeof(dump) - (size_t)at, "%s%02X=--", i == 0 ? "" : " ",
                         reg);
        }
      }
      Serial.printf("[pmu] 0x22 regs %s\n", dump);

      // Data line 2: the FUSB302 fields that can be named, or "-" when a register did not answer.
      char id[8] = "-";
      char ver[8] = "-";
      char product[8] = "-";
      char power[8] = "-";
      char pwr[8] = "-";
      char status0[8] = "-";
      char vbus[4] = "-";
      char bc[24] = "-";
      char comp[4] = "-";
      if (f.haveId) {
        snprintf(id, sizeof(id), "0x%02X", f.deviceId);
        snprintf(ver, sizeof(ver), "0x%X", f.versionId);
        snprintf(product, sizeof(product), "0x%X", f.productId);
      }
      if (f.havePower) {
        snprintf(power, sizeof(power), "0x%02X", f.power);
        snprintf(pwr, sizeof(pwr), "%d%d%d%d", (f.power >> 3) & 1, (f.power >> 2) & 1,
                 (f.power >> 1) & 1, f.power & 1);
      }
      if (f.haveStatus0) {
        snprintf(status0, sizeof(status0), "0x%02X", f.status0);
        snprintf(vbus, sizeof(vbus), "%d", f.vbusOk ? 1 : 0);
        snprintf(bc, sizeof(bc), "%d(%s)", (int)f.bcLvl, om::diag::bcLevelName(f.bcLvl));
        snprintf(comp, sizeof(comp), "%d", f.comp ? 1 : 0);
      }
      Serial.printf(
          "[pmu] 0x22 id=%s version=%s product=%s power=%s pwr=%s status0=%s vbusok=%s "
          "bc_lvl=%s comp=%s\n",
          id, ver, product, power, pwr, status0, vbus, bc, comp);

      // Data line 3: AIN0..AIN3 at the pin, AIN0 read LAST so the chip is left exactly as a battery
      // read leaves it. The numbers are the ADC's mV (batteryMilliVolts doubles AIN0: x2 divider).
      om::AdcProbe adc;
      om::adcProbe(adc);
      char a[4][24];
      int adcOk = 0;
      for (int ch = 0; ch < 4; ++ch) {
        if (adc.ok[ch]) {
          snprintf(a[ch], sizeof(a[ch]), "%umV(%u)", (unsigned)adc.mv[ch], (unsigned)adc.raw[ch]);
          ++adcOk;
        } else {
          snprintf(a[ch], sizeof(a[ch]), "-");
        }
      }
      Serial.printf("[pmu] adc ain0=%s ain1=%s ain2=%s ain3=%s\n", a[0], a[1], a[2], a[3]);
      snprintf(reply, cap,
               "[cmd] ok pmu probe usb=0x22 regs=%d/%d vbusok=%s adc=%d/4",
               read, om::diag::kFusbReadCount, vbus, adcOk);
      return;
    }

    // EST-377: the battery log's switch and its tail. The switch is NVS (safe on the command path,
    // like `wifi on|off`); the tail read is the same shape as `history`, i.e. from usbCommandExecute,
    // which otaCommandPoll runs on the loop task after otaPoll() returns (EST-350), with a static
    // buffer so the handler's stack does not grow.
    case om::usb::Kind::Log: {
      if (cmd.text != nullptr && (strcmp(cmd.text, "on") == 0 || strcmp(cmd.text, "off") == 0)) {
        const bool on = strcmp(cmd.text, "on") == 0;
        const bool saved = om::plog::setEnabled(on);
        snprintf(reply, cap, "[cmd] ok log power on=%d saved=%d", on ? 1 : 0, saved ? 1 : 0);
        return;
      }
      if (cmd.text != nullptr && strcmp(cmd.text, "tail") == 0) {
        const int lines = om::plog::printTail();
        // EST-380: the failure count is part of the answer, so a lost minute shows in `log power
        // tail` itself and not only in a serial line nobody reads.
        if (lines < 0) {
          snprintf(reply, cap, "[cmd] error log power tail: no file on the card write_fail=%lu",
                   (unsigned long)om::plog::writeFailures());
          return;
        }
        snprintf(reply, cap, "[cmd] ok log power tail %d write_fail=%lu", lines,
                 (unsigned long)om::plog::writeFailures());
        return;
      }
      cmdError(reply, cap, "args");
      return;
    }

    // EST-359/361: `play <event> [sound-id]` is the reviewer's door to the shared library. It is a
    // diagnostic, so it bypasses master-off and DND (the codec volume still follows the Sound setting)
    // and says exactly what it queued. The audio task renders and streams it; the reply never waits.
    case om::usb::Kind::Play: {
      om::sound::Event ev;
      if (!om::sound::parseEvent(cmd.target, &ev)) {
        cmdError(reply, cap, "args");
        return;
      }
      int id = cmd.count;
      if (id < 0) id = (int)om::sound::defaultSound(ev);
      if (!om::sound::validSound((uint8_t)id)) {
        cmdError(reply, cap, "args");
        return;
      }
      om::audioVolume(om::sounds::volume());
      if (!om::audioPlay(ev, (uint8_t)id)) {
        snprintf(reply, cap, "[cmd] error play audio_down dropped=%lu",
                 (unsigned long)om::audioDroppedCount());
        return;
      }
      snprintf(reply, cap,
               "[cmd] ok play event=%s sound=%d name=\"%s\" ms=%d queued=1 played=%lu dropped=%lu",
               om::sound::eventKey(ev), id, om::sound::soundName((uint8_t)id),
               om::sound::soundMs((uint8_t)id), (unsigned long)om::audioPlayedCount(),
               (unsigned long)om::audioDroppedCount());
      return;
    }

    case om::usb::Kind::KeyWake:
      om::usbWakePress();
      snprintf(reply, cap, "[cmd] ok key wake");
      return;

    // EST-381: the message pop-up, with no radio traffic. It enters at toasts::notifyNotice() with
    // kind Channel and chatOpen=false, i.e. the exact call a received channel message makes on every
    // screen except its own chat, so this hook cannot test a path a real message does not take. The
    // decision, its `[notice] ...` line and the redraw are the module's; the reply repeats what it
    // answered so the host tool can assert it (a DND or a screen-off refusal is visible here).
    case om::usb::Kind::Notify: {
      om::ui::Notice n;
      om::ui::noticeCopy(n.title, sizeof(n.title), cmd.target);
      om::ui::noticeCopy(n.body, sizeof(n.body), cmd.text);
      const char* reason = nullptr;
      const om::ui::NoticeDelivery out =
          om::toasts::notifyNotice(om::ui::NoticeKind::Channel, n, false, om::sounds::dnd(), &reason);
      char title[96];
      cmdSafe(title, sizeof(title), n.title);
      char result[48];
      if (out == om::ui::NoticeDelivery::Skipped) {
        snprintf(result, sizeof(result), "skip(%s)", reason != nullptr ? reason : "?");
      } else {
        snprintf(result, sizeof(result), "%s", om::ui::noticeDeliveryToken(out));
      }
      snprintf(reply, cap,
               "[cmd] ok notify test kind=channel title=\"%s\" bytes=%d result=%s dnd=%d",
               title, (int)strlen(n.body), result, om::sounds::dnd() ? 1 : 0);
      return;
    }

    // EST-382: the same manual advert the Chat page's key sends, through the same facade function
    // and the same 30 s per-kind guard. The reply repeats the outcome so a host tool can assert it.
    case om::usb::Kind::Advert: {
      const bool flood = cmd.target != nullptr && strcmp(cmd.target, "flood") == 0;
      const int r = om::meshSendAdvert(flood);
      if (r == 0) {
        snprintf(reply, cap, "[cmd] ok advert %s sent=1", flood ? "flood" : "local");
      } else if (r > 0) {
        snprintf(reply, cap, "[cmd] ok advert %s sent=0 wait=%d", flood ? "flood" : "local", r);
      } else {
        snprintf(reply, cap, "[cmd] error advert radio not ready");
      }
      return;
    }

    case om::usb::Kind::Unknown:
    default:
      cmdError(reply, cap, "unknown");
      return;
  }
}

}  // namespace om
