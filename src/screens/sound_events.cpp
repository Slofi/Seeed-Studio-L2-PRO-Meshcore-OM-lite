// SPDX-License-Identifier: MIT
// EST-371: the sound-event choices, moved out of main.cpp. The keys and the migration rule are the
// same NVS keys the shell always wrote; only the owner changed.
#include "screens/sound_events.h"

#include <Arduino.h>
#include <string.h>

#include "audio.h"

namespace om {
namespace sounds {
namespace {

constexpr int kEvents = (int)Event::Count;
Preferences* s_prefs = nullptr;
bool s_master = true;
uint8_t s_volume = 60;
uint8_t s_version = 0;
bool s_dnd = false;
bool s_touchClicks = false;
bool s_welcome = true;
uint8_t s_ids[kEvents] = {0};

uint8_t& defaultRef() {
  // Every real event has a slot; an out-of-range cast is treated as Message.
  return s_ids[(int)Event::Message];
}

// A stored choice from NVS. Version 2+ holds a library id (kSoundOff = Off). Version 0/1 holds the
// EST-359 per-event index: 0..2 maps to the closest new sound, 3 and the old 17 are Off, and any
// other out-of-range value falls back to the event's default. A key never written gets the default.
// EST-369: an EST-361 id whose sound was retired resolves to the kept sound it now means, so an old
// choice keeps sounding like the nearest new one instead of breaking.
uint8_t loadChoice(const char* key, Event ev) {
  const int raw = (int)s_prefs->getUChar(key, 0xFF);
  if (raw == 0xFF) return sound::defaultSound(ev);
  if (s_version >= 2) {
    const uint8_t id = (uint8_t)raw;
    if (id == sound::kSoundOff) return id;
    const uint8_t kept = sound::resolveSound(id);
    return kept != sound::kSoundOff ? kept : sound::defaultSound(ev);
  }
  if (raw == 3 || raw == 17) return sound::kSoundOff;   // EST-359 / the old 17-chime Off
  if (raw > 3) return sound::defaultSound(ev);          // an older out-of-range value
  return sound::legacyVariantSound(ev, (uint8_t)raw);
}

// Master off is absolute. DND silences every event except error and low battery (the two the ticket
// names); a preview from the picker is an explicit user action, so it bypasses DND but not master.
bool allowed(Event ev, bool preview) {
  if (!s_master) return false;
  if (s_dnd && !preview && !sound::dndExempt(ev)) return false;
  return true;
}

}  // namespace

void begin(Preferences* prefs) { s_prefs = prefs; }

bool load() {
  if (s_prefs == nullptr) return false;
  s_master = s_prefs->getBool("sound", true);
  s_volume = s_prefs->getUChar("vol", 60);
  // EST-361: one shared-library sound id per event (kSoundOff = Off). A pre-2 value is the EST-359
  // per-event index and loadChoice migrates it through sound::legacyVariantSound.
  s_version = s_prefs->getUChar("sndver", 0);
  s_ids[(int)Event::Boot] = loadChoice("boot", Event::Boot);
  s_ids[(int)Event::Message] = loadChoice("msg", Event::Message);
  s_ids[(int)Event::Dm] = loadChoice("dm", Event::Dm);
  s_ids[(int)Event::Mention] = loadChoice("men", Event::Mention);
  s_ids[(int)Event::Sent] = loadChoice("sent", Event::Sent);
  s_ids[(int)Event::Error] = loadChoice("err", Event::Error);
  s_ids[(int)Event::NewNode] = loadChoice("newn", Event::NewNode);
  s_ids[(int)Event::LowBattery] = loadChoice("batt", Event::LowBattery);
  s_ids[(int)Event::Touch] = loadChoice("tone", Event::Touch);
  s_dnd = s_prefs->getBool("dnd", false);
  // EST-359: the touch click is optional and OFF for a fresh device.
  s_touchClicks = s_prefs->getBool("tsnd", false);
  s_welcome = s_prefs->getBool("welc", true);
  // Persist the migrated ids once, so the next boot reads ids directly.
  const bool migrated = s_version < 2;
  if (migrated) {
    s_version = 2;
    save();
  }
  return migrated;
}

void save() {
  if (s_prefs == nullptr) return;
  s_prefs->putBool("sound", s_master);
  s_prefs->putUChar("vol", s_volume);
  s_prefs->putUChar("sndver", 2);   // the keys below now hold shared-library ids
  s_prefs->putUChar("boot", s_ids[(int)Event::Boot]);
  s_prefs->putUChar("msg", s_ids[(int)Event::Message]);
  s_prefs->putUChar("dm", s_ids[(int)Event::Dm]);
  s_prefs->putUChar("men", s_ids[(int)Event::Mention]);
  s_prefs->putUChar("sent", s_ids[(int)Event::Sent]);
  s_prefs->putUChar("err", s_ids[(int)Event::Error]);
  s_prefs->putUChar("newn", s_ids[(int)Event::NewNode]);
  s_prefs->putUChar("batt", s_ids[(int)Event::LowBattery]);
  s_prefs->putBool("dnd", s_dnd);
  s_prefs->putBool("tsnd", s_touchClicks);
  s_prefs->putUChar("tone", s_ids[(int)Event::Touch]);
  s_prefs->putBool("welc", s_welcome);
}

bool masterOn() { return s_master; }
void setMasterOn(bool on) { s_master = on; }
uint8_t volume() { return s_volume; }
void setVolume(uint8_t pct) { s_volume = pct; }
bool dnd() { return s_dnd; }
void setDnd(bool on) { s_dnd = on; }
bool touchClicks() { return s_touchClicks; }
void setTouchClicks(bool on) { s_touchClicks = on; }
bool welcome() { return s_welcome; }
void setWelcome(bool on) { s_welcome = on; }
uint8_t prefsVersion() { return s_version; }

uint8_t& choice(Event ev) {
  const int i = (int)ev;
  return (i >= 0 && i < kEvents) ? s_ids[i] : defaultRef();
}

// What the settings row and the picker call a stored value: the sound's name, or Off.
const char* choiceName(Event ev, uint8_t id) {
  (void)ev;
  const char* name = sound::soundName(id);
  return name != nullptr ? name : "OFF";
}

// The picker's small dim tag on a sound another event already uses: just the short event names,
// no prefix. The current event's own choice is marked selected instead, so it is not repeated here.
void useTags(Event current, uint8_t id, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = 0;
  size_t used = 0;
  for (int e = 0; e < kEvents; ++e) {
    const Event ev = (Event)e;
    if (ev == current || choice(ev) != id) continue;
    const char* tag = sound::eventTag(ev);
    const size_t len = strlen(tag);
    const size_t need = (used ? 2 : 0) + len;
    if (used + need >= cap) break;
    if (used) {
      out[used++] = ',';
      out[used++] = ' ';
    }
    memcpy(out + used, tag, len);
    used += len;
    out[used] = 0;
  }
}

bool play(Event ev, uint8_t soundId, bool preview) {
  if (!allowed(ev, preview)) return false;
  if (!sound::validSound(soundId)) return false;   // the Off choice
  audioVolume(s_volume);
  return audioPlay(ev, soundId);
}

// The per-event door every trigger uses: the stored library sound, the current volume and the DND
// rule.
void playEvent(Event ev) { (void)play(ev, choice(ev)); }

// Boot welcome (Filip, 2026-11-10): the "TK-TK" character stays as the default; the shared library
// lets him pick any other sound too. Retune in sound_synth.cpp's library only.
void playWelcome() { playEvent(Event::Boot); }

void playClick() {
  if (s_touchClicks) playEvent(Event::Touch);
}

}  // namespace sounds
}  // namespace om
