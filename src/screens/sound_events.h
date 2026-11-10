// SPDX-License-Identifier: MIT
// EST-371: the sound-event module. It owns the per-event library choice, the master switch, the
// volume, DND and the two touch/welcome switches, plus the one rule for playing an event (master,
// DND, Off). The synth itself stays in sound_synth.cpp; the codec stays in audio.cpp.
#pragma once

#include <Preferences.h>

#include <stddef.h>
#include <stdint.h>

#include "sound_synth.h"

namespace om {
namespace sounds {

using Event = sound::Event;

// The NVS key holder is the shell's; the module reads and writes it after begin().
void begin(Preferences* prefs);
bool load();       // reads every key, migrates an EST-359 value; true when it wrote the migration
void save();

bool masterOn();
void setMasterOn(bool on);
uint8_t volume();
void setVolume(uint8_t pct);
bool dnd();
void setDnd(bool on);
bool touchClicks();
void setTouchClicks(bool on);
bool welcome();
void setWelcome(bool on);
uint8_t prefsVersion();
uint8_t& choice(Event ev);

const char* choiceName(Event ev, uint8_t id);
void useTags(Event current, uint8_t id, char* out, size_t cap);

bool play(Event ev, uint8_t soundId, bool preview = false);
void playEvent(Event ev);
void playWelcome();
void playClick();

}  // namespace sounds
}  // namespace om
