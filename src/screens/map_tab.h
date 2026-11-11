// SPDX-License-Identifier: MIT
// EST-371: the Map tab's own module. It owns the Map's state (follow lock, the selected marker, the
// Traces queue and its animation clock, the two saved layer switches) and the drawing glue that
// turns the real contact table and our own fix into the one MapView that ui/screen_map.cpp draws.
// main.cpp keeps the tab bar and the gear; this module gets the status bar, the tab bar model and
// the GPS fix through the small Host below, so it has no reference back to the shell's globals.
#pragma once

#include "board.h"
#include "position_source.h"
#include "ui/map_layers.h"
#include "ui/press.h"
#include "ui/screen_map.h"

namespace om {
namespace map_tab {

// The four shell services the Map needs. Set once at boot (begin()).
struct Host {
  void (*buildStatus)(ui::StatusModel&) = nullptr;
  ui::TabBarModel (*tabBar)(ui::Tab) = nullptr;
  // EST-384: the effective own position (GPS or Fixed), not the raw GPS parser state. The Map's
  // own marker, locate/follow and the trace endpoint all read this one point.
  const PositionFix* (*gps)() = nullptr;
  void (*toast)(const char*) = nullptr;
};

void begin(const Host& host);
// EST-370/371: the saved Traces and Names switches, loaded through the shell's Preferences. EST-373
// adds the hidden tab column, saved in the same store.
void beginLayers(bool (*read)(void*, const char*, bool, bool&),
                 bool (*write)(void*, const char*, bool), void* ctx);
void loadLayers();
bool tracesOn();
bool namesOn();
bool tabsHidden();

// Tab entry: show the last composed canvas at once, then refresh its tiles only if the view changed.
void enter();
// EST-374: the Map is being left (showTab calls this on every route off the tab). The tucked bar is
// this screen's chrome, so the next screen's own drawTabBar() shows the bar; this clears the Map's
// transient state and logs the leave so the device log shows the rule.
void leave();
void draw();                 // the normal frame (cached on an open, full render otherwise)
void pump(bool visible, bool uiDrawOk);

// The Map's chrome hit test: true when the tap did something the Map owns. The tab bar, the gear and
// the radio mark are the shell's and are handled before this call.
bool tap(int x, int y);

// EST-390: the rectangle the pressed look goes on. Same chrome and same hit test as tap(), so the
// pressed tint and the tap can never cover different pixels.
om::ui::PressTarget pressTarget(int x, int y);

// Selection and follow state, used by the shell and by the Nodes sheet.
void clearSelection();
void select(const char* name);
void setLock(bool on);
bool locked();
void showPoint(double lat, double lon, const char* name);   // "Show on map"

// EST-368: restart the replay when the tab opens; the toggle ends or starts it in place.
void tracesResetAndSeed();
void tracesToggle();
void namesToggle();

}  // namespace map_tab
}  // namespace om
