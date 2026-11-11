// SPDX-License-Identifier: MIT
// EST-384: the node's OWN position, with one precedence rule.
//
// The L2 can show either the live GPS fix or a point picked on the map (Fixed). Fixed wins: while it
// is on, the GPS keeps running but feeds only the clock (EST-383), and a later fix can never move the
// node. The phone can set the point too (MeshCore's CMD_SET_ADVERT_LATLON); that becomes the Fixed
// position, so the phone and the glass cannot fight over the node.
//
// Plain C++ (no Arduino, no Preferences) so test/host can prove the precedence, the phone rule and
// the stored round trip. src/position.cpp owns the NVS glue and the MeshCore sensor write; this
// header never logs a coordinate.
#pragma once

#include <stdint.h>

namespace om {

// 0 = GPS (today's behaviour, the default), 1 = Fixed. The number is what NVS stores.
enum : uint8_t { kPosSourceGps = 0, kPosSourceFixed = 1 };

struct PositionFix {
  bool valid = false;   // there is a point to use right now
  bool fixed = false;   // it came from the Fixed source (the pin), not the GPS dot
  double lat = 0.0;
  double lon = 0.0;
};

// A point is usable when it is inside the map's range and is not (0, 0). MeshCore reads lat/lon 0
// as "no position", which is why the same rule is used here.
inline bool posCoordsValid(double lat, double lon) {
  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return false;
  return lat != 0.0 || lon != 0.0;
}

// What NVS holds: the source and, for Fixed, the point. Saved as one record so a power cut cannot
// leave "Fixed" with no point.
struct StoredPosition {
  uint8_t source = kPosSourceGps;
  double lat = 0.0;
  double lon = 0.0;
};

class PositionSource {
 public:
  // Boot: restores what NVS had. A Fixed source whose point is missing or out of range falls back
  // to GPS (the default), never to a guessed point.
  void restore(uint8_t storedSource, double lat, double lon) {
    source_ = kPosSourceGps;
    fixedSet_ = false;
    fixedLat_ = fixedLon_ = 0.0;
    if (storedSource == kPosSourceFixed && posCoordsValid(lat, lon)) {
      source_ = kPosSourceFixed;
      fixedSet_ = true;
      fixedLat_ = lat;
      fixedLon_ = lon;
    }
    recompute();
  }

  // The GPS parser's latest fix. It is kept even while Fixed is on (the clock still needs it), but
  // it only becomes the node's position when GPS is the selected source.
  void setGpsFix(double lat, double lon, bool valid) {
    gpsValid_ = valid && posCoordsValid(lat, lon);
    gpsLat_ = gpsValid_ ? lat : 0.0;
    gpsLon_ = gpsValid_ ? lon : 0.0;
    recompute();
  }

  bool selectFixed(double lat, double lon) {
    if (!posCoordsValid(lat, lon)) return false;
    source_ = kPosSourceFixed;
    fixedSet_ = true;
    fixedLat_ = lat;
    fixedLon_ = lon;
    recompute();
    return true;
  }

  void selectGps() {
    source_ = kPosSourceGps;
    recompute();
  }

  // MeshCore's CMD_SET_ADVERT_LATLON from the phone: the point it sent becomes Fixed. An invalid
  // point is refused and the source stays where it was (upstream also rejects it).
  bool phoneSetLocation(double lat, double lon) { return selectFixed(lat, lon); }

  const PositionFix& own() const { return own_; }
  uint8_t source() const { return source_; }
  bool sourceFixed() const { return source_ == kPosSourceFixed; }
  bool fixedSet() const { return fixedSet_; }
  bool hasGpsFix() const { return gpsValid_; }
  double gpsLat() const { return gpsLat_; }
  double gpsLon() const { return gpsLon_; }
  double fixedLat() const { return fixedLat_; }
  double fixedLon() const { return fixedLon_; }

  StoredPosition stored() const {
    StoredPosition s;
    s.source = source_;
    if (source_ == kPosSourceFixed && fixedSet_) {
      s.lat = fixedLat_;
      s.lon = fixedLon_;
    }
    return s;
  }

  // "gps" | "fixed" | "none", for `status` and the Node page. Never a coordinate.
  const char* statusToken() const {
    if (own_.fixed) return "fixed";
    return own_.valid ? "gps" : "none";
  }

 private:
  void recompute() {
    own_ = PositionFix();
    if (source_ == kPosSourceFixed) {
      if (fixedSet_) {
        own_.valid = true;
        own_.fixed = true;
        own_.lat = fixedLat_;
        own_.lon = fixedLon_;
      }
      return;   // Fixed with no point is "none", never a silent fall back to GPS
    }
    if (gpsValid_) {
      own_.valid = true;
      own_.fixed = false;
      own_.lat = gpsLat_;
      own_.lon = gpsLon_;
    }
  }

  uint8_t source_ = kPosSourceGps;
  bool fixedSet_ = false;
  double fixedLat_ = 0.0;
  double fixedLon_ = 0.0;
  bool gpsValid_ = false;
  double gpsLat_ = 0.0;
  double gpsLon_ = 0.0;
  PositionFix own_;
};

}  // namespace om
