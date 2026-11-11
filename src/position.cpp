// SPDX-License-Identifier: MIT
// EST-384: NVS and MeshCore glue for the one own-position source.
//
// The rule lives in position_source.h (host-tested). This file only stores it in Preferences and
// writes the effective point into the sensors MeshCore reads. LOGS NAME THE SOURCE ONLY: no format
// string here carries lat/lon (REBUILD.md rule 9 and test_est384.py's grep).
#include <Arduino.h>

#include "position.h"

#include "position_source.h"
#include "target.h"

namespace om {
namespace {

constexpr const char* kPrefsSource = "psrc";
constexpr const char* kPrefsLat = "plat";
constexpr const char* kPrefsLon = "plon";

PositionSource s_source;
Preferences* s_prefs = nullptr;
bool s_begun = false;

// The values posApplyToNode() last wrote. A different value in sensors comes from someone else (the
// phone), never from our own GPS feed.
double s_appliedLat = 0.0;
double s_appliedLon = 0.0;

void saveStored() {
  if (s_prefs == nullptr) return;
  const StoredPosition st = s_source.stored();
  s_prefs->putUChar(kPrefsSource, st.source);
  s_prefs->putDouble(kPrefsLat, st.lat);
  s_prefs->putDouble(kPrefsLon, st.lon);
}

}  // namespace

void posBegin(Preferences* prefs) {
  // One boot record. A missing key is GPS (the default); a Fixed record with a missing or invalid
  // point falls back to GPS inside restore(), never to a guessed point.
  s_prefs = prefs;
  uint8_t source = kPosSourceGps;
  double lat = 0.0;
  double lon = 0.0;
  if (prefs != nullptr) {
    source = prefs->getUChar(kPrefsSource, kPosSourceGps);
    lat = prefs->getDouble(kPrefsLat, 0.0);
    lon = prefs->getDouble(kPrefsLon, 0.0);
  }
  s_source.restore(source, lat, lon);
  s_begun = true;
  posApplyToNode();
  Serial.printf("[pos] source=%s\n", s_source.statusToken());
}

const PositionFix& posOwn() { return s_source.own(); }
const PositionSource& posSource() { return s_source; }
const char* posStatusToken() { return s_source.statusToken(); }
const char* posSourceLabel() { return s_source.sourceLabel(); }

void posApplyToNode() {
  const PositionFix& own = s_source.own();
  sensors.node_lat = own.valid ? own.lat : 0.0;
  sensors.node_lon = own.valid ? own.lon : 0.0;
  s_appliedLat = sensors.node_lat;
  s_appliedLon = sensors.node_lon;
}

bool posSelectFixed(double lat, double lon) {
  if (!s_source.selectFixed(lat, lon)) return false;
  saveStored();
  posApplyToNode();
  Serial.printf("[pos] source=%s\n", s_source.statusToken());
  return true;
}

bool posSelectGps() {
  s_source.selectGps();
  saveStored();
  posApplyToNode();
  Serial.printf("[pos] source=%s\n", s_source.statusToken());
  return true;
}

bool posFixHere() {
  if (!s_source.hasGpsFix()) return false;
  return posSelectFixed(s_source.gpsLat(), s_source.gpsLon());
}

bool posPhoneSet(double lat, double lon) {
  if (!s_source.phoneSetLocation(lat, lon)) return false;
  saveStored();
  posApplyToNode();
  // The command is MeshCore's CMD_SET_ADVERT_LATLON; the line names the result, never the point.
  Serial.println("[pos] phone set-location -> fixed");
  return true;
}

void posSetGpsFix(double lat, double lon, bool valid) {
  s_source.setGpsFix(lat, lon, valid);
  // While Fixed is on, the GPS keeps running for the clock but this is the whole of its effect on
  // the node: no write to sensors, so a fresh fix can never move a fixed node.
  if (!s_source.sourceFixed()) posApplyToNode();
}

void posPollNode() {
  if (!s_begun) return;
  // Our own GPS write updates the snapshot, so equality is the normal case. A different value came
  // from somewhere else; the only other writer in MeshCore's companion is CMD_SET_ADVERT_LATLON
  // (handled at the BLE frame too). Take it as Fixed so the device follows the phone.
  if (sensors.node_lat == s_appliedLat && sensors.node_lon == s_appliedLon) return;
  if (posCoordsValid(sensors.node_lat, sensors.node_lon)) {
    posPhoneSet(sensors.node_lat, sensors.node_lon);
  } else {
    // Someone cleared them; the source is still our truth, so put it back.
    posApplyToNode();
  }
}

}  // namespace om
