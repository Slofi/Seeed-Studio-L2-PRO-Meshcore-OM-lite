// SPDX-License-Identifier: MIT
// Loads the five anti-aliased VLW blobs once and maps FontRole to a loaded face.
#include "ui/fonts.h"

#include <Arduino.h>

namespace om {
namespace ui {
namespace {

struct Slot {
  lgfx::VLWfont font;
  lgfx::PointerWrapper data;
  bool ok = false;
};

// Function-local statics: constructed on the first call, i.e. from setup(), never before USB CDC
// exists. A crash in a global constructor would be invisible on this board.
Slot& slotSans11() { static Slot s; return s; }
Slot& slotSans14() { static Slot s; return s; }
Slot& slotSans9()  { static Slot s; return s; }
Slot& slotMono10() { static Slot s; return s; }

bool load(Slot& s, const uint8_t* data, uint32_t len, const char* name) {
  s.data.set(data, len);
  s.ok = s.font.loadFont(&s.data);
  Serial.printf("[ui] font %s: %s (%u bytes)\n", name, s.ok ? "ok" : "FAILED", (unsigned)len);
  return s.ok;
}

bool s_loaded = false;
bool s_ok = false;

}  // namespace

void loadFonts() {
  if (s_loaded) return;
  s_loaded = true;
  s_ok = true;
  s_ok &= load(slotSans11(), kSans11Data, kSans11DataLen, "sans11");
  s_ok &= load(slotSans14(), kSans14Data, kSans14DataLen, "sans14");
  s_ok &= load(slotSans9(),  kSans9Data,  kSans9DataLen,  "sans9");
  s_ok &= load(slotMono10(), kMono10Data, kMono10DataLen, "mono10");
}

bool fontsReady() { return s_ok; }

const lgfx::IFont* fontFor(FontRole role) {
  if (!s_loaded) return nullptr;
  Slot* s = nullptr;
  switch (role) {
    case FontRole::SansBody:   s = &slotSans11(); break;
    case FontRole::SansTitle:  s = &slotSans14(); break;
    case FontRole::SansTab:    s = &slotSans9();  break;
    case FontRole::MonoMeta:   s = &slotMono10(); break;
    case FontRole::MonoStatus: s = &slotMono10(); break;
    default: return nullptr;
  }
  return s->ok ? &s->font : nullptr;
}

}  // namespace ui
}  // namespace om
