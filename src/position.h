// SPDX-License-Identifier: MIT
// EST-384: the firmware glue for the one own-position source. src/position_source.h holds the
// precedence (Fixed beats GPS, GPS feeds only the clock while Fixed is on, the phone's set-location
// becomes Fixed); this owns the NVS record and the ONE write to MeshCore's sensors.node_lat/lon.
//
// Every place that wants the node's own position reads posOwn() or the status token. Nothing here
// logs a coordinate.
#pragma once

#include <Preferences.h>

#include "position_source.h"

namespace om {

// Boot, after Preferences is open. Loads "psrc", "plat" and "plon"; a missing or invalid record is
// GPS (the default). Safe to call once.
void posBegin(Preferences* prefs);

// The effective own position: the Fixed point when Fixed is selected (valid even if the GPS has no
// fix), the GPS fix when GPS is selected and there is one, otherwise invalid.
const PositionFix& posOwn();
const PositionSource& posSource();
const char* posStatusToken();   // "gps" | "fixed" | "none"

// Selected-source changes. Both persist and apply to MeshCore's sensors. selectFixed refuses an
// invalid point and changes nothing then.
bool posSelectFixed(double lat, double lon);
bool posSelectGps();

// `pos fixed-here` / the settings shortcut: the current GPS fix becomes Fixed. False when there is
// no fix to take.
bool posFixHere();

// The phone's CMD_SET_ADVERT_LATLON: that point becomes Fixed, persisted, and the source switches.
bool posPhoneSet(double lat, double lon);

// The GPS parser's one door. Updates the stored GPS values; writes MeshCore's sensors only when GPS
// is the selected source. The clock path reads the parser directly and is untouched.
void posSetGpsFix(double lat, double lon, bool valid);

// Writes the effective point into MeshCore's sensors, which is what adverts, telemetry and the
// phone's self-info read. This is the ONLY writer of sensors.node_lat/lon after boot.
void posApplyToNode();

// Safety net for a phone set-location that did not pass the BLE transport hook: if something else
// changed sensors.node_lat/lon, take that point as Fixed. Never runs for our own GPS writes because
// they update the applied snapshot in posApplyToNode().
void posPollNode();

}  // namespace om
