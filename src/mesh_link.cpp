// SPDX-License-Identifier: MIT
// MeshCore channel link for OM Lite.
//
// Every object is created lazily inside meshLinkBegin(): nothing may run before setup(), because a
// fault in a global constructor happens before USB CDC exists and is then invisible. A crash during
// init leaves a flag in NVS; the next boot sees it, skips the mesh and stays reachable over OTA.
// Send "MESHON" over serial to clear the flag and retry (the device reboots).
#include "mesh_link.h"

#if defined(OM_ENABLE_MESH)

#include <Preferences.h>
#include <SPIFFS.h>

#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/radiolib/CustomSX1262.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>

#include "audio.h"
#include "channels.h"
#include "mc_board.h"
#include "target.h"

namespace om {
namespace {

char s_lastText[96];
int s_lastChannel = -1;
bool s_ready = false;

struct Msg {
  uint8_t ch;
  uint8_t mine;
  int8_t hops;     // packet path hops, -1 = local echo / unknown
  int8_t snr4;     // SNR x 4 (MeshCore's unit), 0 = unknown
  uint32_t ts;     // uptime seconds when it was stored (no RTC on this board)
  char name[20];
  char text[96];
};
constexpr int kMsgRing = 32;
Msg s_msgs[kMsgRing];
uint32_t s_msgSeq = 0;   // messages ever stored; the next slot is s_msgSeq % kMsgRing
int s_msgCount = 0;      // readable messages, capped at kMsgRing
uint8_t s_unread[8] = {0};

// idx 0 is the oldest readable message. Deriving the slot from s_msgSeq (not from the count)
// keeps the order right after the ring wraps; the old version silently reordered at 32.
int slotFor(int idx) {
  return (int)((s_msgSeq - (uint32_t)s_msgCount + (uint32_t)idx) % (uint32_t)kMsgRing);
}

class OmChat : public BaseChatMesh {
public:
  OmChat(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
         mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : BaseChatMesh(radio, ms, rng, rtc, mgr, tables) {}

  bool beginStore() {
    Serial.println("[mc] 6a BaseChatMesh::begin");
    BaseChatMesh::begin();
    Serial.println("[mc] 6b identity store");
    IdentityStore store(SPIFFS, "/identity");
    store.begin();   // mkdir /identity
    // Two-argument load: the 4-arg overload writes display_name[n-1] and crashes with a 0-size buffer.
    if (!store.load("_main", self_id)) {
      ((StdRNG*)getRNG())->begin(millis());
      self_id = mesh::LocalIdentity(getRNG());
      int tries = 0;
      while (tries < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) {
        self_id = mesh::LocalIdentity(getRNG());
        ++tries;
      }
      Serial.println("[mc] 6c new identity");
      store.save("_main", self_id);
    }
    return true;
  }

  void sendAdvert() {
    mesh::Packet* pkt = createAdvert(self_id);
    if (pkt) sendPacket(pkt, 0, 800);
  }

  void onDiscoveredContact(ContactInfo&, bool, uint8_t, const uint8_t*) override {}
  ContactInfo* processAck(const uint8_t*) override { return nullptr; }
  void onContactPathUpdated(const ContactInfo&) override {}
  void onMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
  void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
  void onCLICommandRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*, char*) override {}
  void onSignedMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const uint8_t*,
                           const char*) override {}
  uint32_t calcFloodTimeoutMillisFor(uint32_t) const override { return 6000; }
  uint32_t calcDirectTimeoutMillisFor(uint32_t, uint8_t) const override { return 6000; }
  void onSendTimeout() override {}
  uint8_t onContactRequest(const ContactInfo&, uint32_t, const uint8_t*, uint8_t, uint8_t*) override {
    return 0;
  }
  void onContactResponse(const ContactInfo&, const uint8_t*, uint8_t) override {}

  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t,
                            const char* text) override {
    s_lastChannel = -1;
    for (int i = 0; i < mcChannelCount(); ++i) {
      ChannelDetails cd;
      if (getChannel(i, cd) && memcmp(cd.channel.hash, channel.hash, sizeof(channel.hash)) == 0) {
        s_lastChannel = i;
        break;
      }
    }
    if (s_lastChannel < 0) {   // not one of ours (or a removed channel still in the mesh table)
      Serial.printf("[mc] message for an unknown channel, ignored: %s\n", text ? text : "");
      return;
    }
    // MeshCore dispatches one packet per matching slot, and an emptied slot still has a (zero-key) hash
    // that can collide with a real channel's 1/256 of the time. Drop an immediate repeat.
    {
      static int lastCh = -1;
      static uint32_t lastTs = 0;
      static char lastText[64];
      const uint32_t now = millis() / 1000;
      if (s_lastChannel == lastCh && (now - lastTs) <= 2 && strncmp(lastText, text ? text : "", 63) == 0) {
        Serial.println("[mc] duplicate dispatch, ignored");
        return;
      }
      lastCh = s_lastChannel;
      lastTs = now;
      snprintf(lastText, sizeof(lastText), "%s", text ? text : "");
    }
    snprintf(s_lastText, sizeof(s_lastText), "%s", text ? text : "");
    const int slot = slotFor(s_msgCount);
    s_msgs[slot].ch = (uint8_t)(s_lastChannel < 0 ? 0 : s_lastChannel);
    s_msgs[slot].mine = 0;
    s_msgs[slot].ts = millis() / 1000;
    // What the air gave us: hops travelled and SNR at the last hop (Dispatcher sets _snr on RX).
    s_msgs[slot].hops = pkt ? (int8_t)pkt->getPathHashCount() : -1;
    s_msgs[slot].snr4 = pkt ? (int8_t)lroundf(pkt->getSNR() * 4.0f) : 0;
    const char* body = text ? text : "";
    const char* sep = strstr(body, ": ");
    if (sep && (size_t)(sep - body) < sizeof(s_msgs[slot].name)) {
      const size_t n = (size_t)(sep - body);
      memcpy(s_msgs[slot].name, body, n);
      s_msgs[slot].name[n] = 0;
      body = sep + 2;
    } else {
      snprintf(s_msgs[slot].name, sizeof(s_msgs[slot].name), "?");
    }
    snprintf(s_msgs[slot].text, sizeof(s_msgs[slot].text), "%s", body);
    if (s_msgCount < kMsgRing) ++s_msgCount;
    ++s_msgSeq;
    if (s_lastChannel >= 0 && s_lastChannel < 8) ++s_unread[s_lastChannel];
    const bool muted = (s_lastChannel >= 0) && mcChannelMuted(s_lastChannel);
    Serial.printf("[mc] channel %d: %s (hops %d snr %.2f)%s\n", s_lastChannel, s_lastText,
                  (int)s_msgs[slot].hops, (double)s_msgs[slot].snr4 / 4.0,
                  muted ? " [muted]" : "");
    // A muted channel still stores the message and still counts unread: mute is about the chime only.
    if (!muted) {
      audioTone(1318, 70);
      audioTone(1760, 90);
    }
  }
};

// Lazily created; the constructor only runs inside meshLinkBegin(), after USB is up.
struct Ctx {
  Ctx() : mod(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi), radio(&mod) {}

  SPIClass spi{FSPI};
  Module mod;
  CustomSX1262 radio;
  OmBoard board;
  StdRNG rng;
  ArduinoMillis ms;
  VolatileRTCClock rtc;
  StaticPoolPacketManager mgr{16};
  SimpleMeshTables tables;
  CustomSX1262Wrapper* wrapper = nullptr;
  OmChat* mesh = nullptr;
};

Ctx* s_ctx = nullptr;
Preferences s_netPrefs;

}  // namespace

bool meshLinkReady() { return s_ready; }
const char* meshLinkLastMessage() { return s_lastText; }
int meshLinkLastChannel() { return s_lastChannel; }

bool meshLinkBegin() {
  if (s_ready) return true;

  s_netPrefs.begin("mcnet", false);
  if (s_netPrefs.getBool("crashed", false)) {
    const uint8_t fails = (uint8_t)(s_netPrefs.getUChar("fails", 0) + 1);
    s_netPrefs.putUChar("fails", fails);
    Serial.printf("[mc] previous init crashed (fail %u/3)\n", fails);
    if (fails < 3) {
      // Retry once automatically: clear the flag, give OTA a moment, reboot.
      s_netPrefs.putBool("crashed", false);
      delay(1500);
      ESP.restart();
    }
    s_netPrefs.putBool("crashed", true);   // parked: stays up for OTA
    return false;
  }
  s_netPrefs.putBool("crashed", true);   // cleared only after a full successful init

  Serial.println("[mc] 1 spiffs");
  if (!SPIFFS.begin(true)) {
    Serial.println("[mc] SPIFFS mount failed");
    return false;
  }

  Serial.println("[mc] 2 objects");
  s_ctx = new Ctx();
  if (!s_ctx) return false;

  Serial.println("[mc] 3 radio std_init");
  s_ctx->spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  if (!s_ctx->radio.std_init(&s_ctx->spi)) {
    Serial.println("[mc] radio init failed");
    return false;
  }

  Serial.println("[mc] 4 tx power");
  s_ctx->radio.setOutputPower(LORA_TX_POWER);

  Serial.println("[mc] 5 wrapper");
  s_ctx->wrapper = new CustomSX1262Wrapper(s_ctx->radio, s_ctx->board);
  if (!s_ctx->wrapper) return false;

  Serial.println("[mc] 6 chat object");
  s_ctx->mesh = new OmChat(*s_ctx->wrapper, s_ctx->ms, s_ctx->rng, s_ctx->rtc, s_ctx->mgr,
                           s_ctx->tables);
  if (!s_ctx->mesh) return false;

  s_ctx->mesh->beginStore();

  // The reference firmware sets the radio params AFTER mesh.begin(); doing it before left the
  // receive path unconfigured (TX worked, RX never delivered).
  Serial.println("[mc] 6d setParams + tx power (after begin)");
  s_ctx->wrapper->setParams(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR);
  s_ctx->wrapper->setTxPower(LORA_TX_POWER);

  Serial.println("[mc] 7 channels");
  meshLinkReloadChannels();

  s_ready = true;   // before advert/hello, because meshLinkSend() checks it

  Serial.println("[mc] 8 advert");
  s_ctx->mesh->sendAdvert();

  // No automatic "OM Lite online" on boot: it put a message on a real channel on every power cycle
  // (Filip, S492). The advert above still announces the node without occupying a channel.

  s_netPrefs.putBool("crashed", false);
  s_netPrefs.putUChar("fails", 0);
  Serial.printf("[mc] up: %.3f MHz SF%u\n", LORA_FREQ, (unsigned)LORA_SF);
  return true;
}

void meshLinkLoop() {
  if (s_ready && s_ctx && s_ctx->mesh) s_ctx->mesh->loop();

}

bool meshLinkReloadChannels() {
  // Deliberately not gated on s_ready: this is also called from meshLinkBegin() while the mesh is
  // being brought up, before s_ready flips (a gate here left the node with no channels at all).
  if (!s_ctx || !s_ctx->mesh) return false;
  const int count = mcChannelCount();
  for (int i = 0; i < kMaxChannels; ++i) {
    ChannelDetails cd;
    memset(&cd, 0, sizeof(cd));
    if (i < count) {
      snprintf(cd.name, sizeof(cd.name), "%s", mcChannelName(i));
      memcpy(cd.channel.secret, mcChannel(i).secret, 16);
    }
    s_ctx->mesh->setChannel(i, cd);
    if (i < count) {
      // Print OUR hash: setChannel() recomputes it inside the mesh, so cd still holds zeros here.
      Serial.printf("[mc] channel %d '%s' hash %02x\n", i, mcChannelName(i), mcChannel(i).hash);
    }
  }
  return true;
}

int meshMsgCount() { return s_msgCount; }
uint32_t meshMsgSerial() { return s_msgSeq; }

const char* meshMsgText(int idx) {
  if (idx < 0 || idx >= s_msgCount) return "";
  return s_msgs[slotFor(idx)].text;
}

int meshMsgChannel(int idx) {
  if (idx < 0 || idx >= s_msgCount) return -1;
  return s_msgs[slotFor(idx)].ch;
}

const char* meshMsgName(int idx) {
  if (idx < 0 || idx >= s_msgCount) return "";
  return s_msgs[slotFor(idx)].name;
}

bool meshMsgMine(int idx) {
  if (idx < 0 || idx >= s_msgCount) return false;
  return s_msgs[slotFor(idx)].mine != 0;
}

uint32_t meshMsgTs(int idx) {
  if (idx < 0 || idx >= s_msgCount) return 0;
  return s_msgs[slotFor(idx)].ts;
}

int meshMsgHops(int idx) {
  if (idx < 0 || idx >= s_msgCount) return -1;
  return s_msgs[slotFor(idx)].hops;
}

int meshMsgSnrQ(int idx) {
  if (idx < 0 || idx >= s_msgCount) return 0;
  return s_msgs[slotFor(idx)].snr4;
}

int meshUnread(int channelIdx) {
  if (channelIdx < 0 || channelIdx >= 8) return 0;
  return s_unread[channelIdx];
}

void meshClearUnread(int channelIdx) {
  if (channelIdx >= 0 && channelIdx < 8) s_unread[channelIdx] = 0;
}

bool meshLinkSend(int channelIdx, const char* text) {
  Serial.printf("[mc] send ch%d ready=%d ctx=%d mesh=%d\n", channelIdx, (int)s_ready,
                s_ctx != nullptr, s_ctx && s_ctx->mesh != nullptr);
  if (!s_ready || !s_ctx || !s_ctx->mesh || !text || !*text) return false;
  ChannelDetails cd;
  if (!s_ctx->mesh->getChannel(channelIdx, cd)) {
    Serial.println("[mc] send: no such channel");
    return false;
  }
  Serial.printf("[mc] send: hash %02x secret %02x textlen %d\n", cd.channel.hash[0],
                cd.channel.secret[0], (int)strlen(text));
  const bool ok = s_ctx->mesh->sendGroupMessage((uint32_t)(millis() / 1000), cd.channel, "OM Lite",
                                                text, (int)strlen(text));
  Serial.printf("[mc] sendGroupMessage -> %d\n", (int)ok);
  if (ok) {
    // Locally echo what we sent, so the channel view shows the outgoing message too.
    const int slot = slotFor(s_msgCount);
    s_msgs[slot].ch = (uint8_t)channelIdx;
    s_msgs[slot].mine = 1;
    s_msgs[slot].ts = millis() / 1000;
    s_msgs[slot].hops = -1;   // we know it is local; the air has not told us anything yet
    s_msgs[slot].snr4 = 0;
    snprintf(s_msgs[slot].name, sizeof(s_msgs[slot].name), "OM Lite");
    snprintf(s_msgs[slot].text, sizeof(s_msgs[slot].text), "%s", text);
    if (s_msgCount < kMsgRing) ++s_msgCount;
    ++s_msgSeq;
  }
  return ok;
}

}  // namespace om

#endif  // OM_ENABLE_MESH
