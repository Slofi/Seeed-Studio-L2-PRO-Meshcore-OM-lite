// SPDX-License-Identifier: MIT
// OM Lite map reader — tiles.pack + tiles.idx from tools/tiles_export.py --pack.
//
// Index: sorted 24-byte records <B3xIIQI> (z, x, y, offset, length), loaded into PSRAM once and
// binary-searched. Tiles are decoded by JPEGDEC into a small PSRAM cache (8 x 256x256 RGB565), so
// panning composites from memory instead of decoding on every drag. The view is 320x200 (the tab
// bar owns the rest), composited into a canvas and pushed in 16-row slices because one SPI
// transfer cannot exceed 64 KB.
#include "map.h"

#include <JPEGDEC.h>
#include <LovyanGFX.hpp>
#include <SD_MMC.h>
#include <math.h>

#include "board.h"
#include "psram_alloc.h"
#include "radio_stats.h"
#include "ui/rgb565.h"
#include "ui/theme.h"

namespace om {
// om_display() is declared in board.h (it can hand out the off-screen frame instead of the panel).
namespace {

constexpr int kTile = 256;
constexpr int kViewW = 320;   // the map area above the tab bar
constexpr int kViewH = 214;
constexpr size_t kMaxTile = 256 * 1024;
constexpr uint32_t kRec = 24;
constexpr int kCache = 8;
constexpr int kZMin = 9;
constexpr int kZMax = 18;   // data to z17; 18 == z17 magnified 2x
// Colours come from ui/theme.h (EST-351): no palette here.
using ui::kBad;
using ui::kInk;
using ui::kMapBg;
const char* kIdxPath = "/maps/slo/tiles.idx";
const char* kPackPath = "/maps/slo/tiles.pack";


File s_pack;             // z9..z15, indexed in PSRAM

// Deeper levels live in their own packs on the card. Their indexes are searched from the file
// (kept open) because they are too big to hold in RAM comfortably.
struct Overlay {
  const char* idxPath;
  const char* packPath;
  int level;
  File idx;
  File pack;
  uint64_t count = 0;
  bool open = false;
  bool failed = false;
};
Overlay s_ov[2] = {
  {"/maps/slo16/tiles.idx", "/maps/slo16/tiles.pack", 16},
  {"/maps/slo17/tiles.idx", "/maps/slo17/tiles.pack", 17},
};
uint8_t* s_index = nullptr;
uint32_t s_count = 0;
uint8_t* s_tile = nullptr;
uint16_t* s_canvas = nullptr;   // kViewW x kViewH composite
// EST-357: 17,884 B, now one PSRAM allocation in mapBegin() instead of internal .bss. JPEGDEC reads
// its work buffers from ordinary memory (no DMA), and map tiles are small, so PSRAM is fine.
JPEGDEC* s_jpeg = nullptr;
uint16_t* s_decodeTarget = nullptr;   // frame being decoded (JPEGDEC pUser is not set)
bool s_ready = false;

struct Rec {
  uint8_t z;
  uint32_t x, y;
  uint64_t off;
  uint32_t len;
};

struct CacheEntry {
  int z = -1, tx = 0, ty = 0;
  uint16_t* frame = nullptr;
};
CacheEntry s_cache[kCache];
int s_cacheNext = 0;

// View: centre in world pixels at s_z (Web-Mercator, top-left origin).
double s_cLat = 46.0569, s_cLon = 14.5058;
int s_z = 13;
double s_cpx = 0.0, s_cpy = 0.0;

bool s_marker = false;
double s_mLat = 0.0, s_mLon = 0.0;

double lonToXf(double lon, int z) { return (lon + 180.0) / 360.0 * (double)(1UL << z); }
double latToYf(double lat, int z) {
  const double r = lat * M_PI / 180.0;
  const double n = log(tan(r) + 1.0 / cos(r));
  return (1.0 - n / M_PI) / 2.0 * (double)(1UL << z);
}
double xfToLon(double px, int z) { return px / (double)(1UL << z) * 360.0 - 180.0; }
double yfToLat(double py, int z) {
  const double n = M_PI * (1.0 - 2.0 * py / (double)(1UL << z));
  return atan(sinh(n)) * 180.0 / M_PI;
}

void centreFromLatLon() {
  s_cpx = lonToXf(s_cLon, s_z) * kTile;
  s_cpy = latToYf(s_cLat, s_z) * kTile;
}

Rec recAt(uint32_t i) {
  const uint8_t* p = s_index + (size_t)i * kRec;
  Rec r;
  r.z = p[0];
  memcpy(&r.x, p + 4, 4);
  memcpy(&r.y, p + 8, 4);
  memcpy(&r.off, p + 12, 8);
  memcpy(&r.len, p + 20, 4);
  return r;
}

bool findTile(int z, uint32_t x, uint32_t y, Rec& out) {
  uint32_t lo = 0, hi = s_count;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    const Rec r = recAt(mid);
    const bool before = (r.z < z) || (r.z == z && (r.x < x || (r.x == x && r.y < y)));
    if (before) lo = mid + 1;
    else hi = mid;
  }
  if (lo >= s_count) return false;
  const Rec r = recAt(lo);
  if (r.z != z || r.x != x || r.y != y) return false;
  out = r;
  return true;
}

// JPEGDEC does not fill pDraw->pUser (verified in the library: it only stores the value), so the
// destination frame comes from s_decodeTarget, which is set immediately before each decode.
bool readRec(File& f, uint64_t i, Rec& out) {
  if (!f.seek(i * kRec)) return false;
  uint8_t buf[kRec];
  if (f.read(buf, kRec) != kRec) return false;
  out.z = buf[0];
  memcpy(&out.x, buf + 4, 4);
  memcpy(&out.y, buf + 8, 4);
  memcpy(&out.off, buf + 12, 8);
  memcpy(&out.len, buf + 20, 4);
  return true;
}

bool findTileOv(Overlay& o, int z, uint32_t x, uint32_t y, Rec& out) {
  uint64_t lo = 0, hi = o.count;
  while (lo < hi) {
    const uint64_t mid = lo + (hi - lo) / 2;
    Rec r;
    if (!readRec(o.idx, mid, r)) return false;
    const bool before = (r.z < z) || (r.z == z && (r.x < x || (r.x == x && r.y < y)));
    if (before) lo = mid + 1; else hi = mid;
  }
  if (lo >= o.count) return false;
  if (!readRec(o.idx, lo, out)) return false;
  return out.z == z && out.x == x && out.y == y;
}

int drawCb(JPEGDRAW* p) {
  uint16_t* target = s_decodeTarget;
  if (!target) return 0;
  // Never write outside the 256x256 frame, whatever the decoder hands back.
  const int x0 = p->x < 0 ? 0 : p->x;
  const int y0 = p->y < 0 ? 0 : p->y;
  const int x1 = (p->x + p->iWidth) > kTile ? kTile : (p->x + p->iWidth);
  const int y1 = (p->y + p->iHeight) > kTile ? kTile : (p->y + p->iHeight);
  for (int row = y0; row < y1; ++row) {
    memcpy(target + (size_t)row * kTile + x0,
           p->pPixels + (size_t)(row - p->y) * p->iWidth + (x0 - p->x),
           (size_t)(x1 - x0) * 2);
  }
  return 1;
}

Overlay* ensureOverlay(int level) {
  for (Overlay& o : s_ov) {
    if (o.level != level) continue;
    if (o.open) return &o;
    if (o.failed) return nullptr;
    o.idx = SD_MMC.open(o.idxPath, FILE_READ);
    if (!o.idx) {
      o.failed = true;
      Serial.printf("[map] overlay z%d: cannot open %s\n", level, o.idxPath);
      return nullptr;
    }
    const size_t size = o.idx.size();
    if (size == 0 || size % kRec != 0) {
      o.failed = true;
      Serial.printf("[map] overlay z%d: bad index size %u\n", level, (unsigned)size);
      return nullptr;
    }
    o.pack = SD_MMC.open(o.packPath, FILE_READ);
    if (!o.pack) {
      o.failed = true;
      Serial.printf("[map] overlay z%d: cannot open %s\n", level, o.packPath);
      return nullptr;
    }
    o.count = size / kRec;
    o.open = true;
    Serial.printf("[map] overlay z%d opened: %llu tiles\n", level, (unsigned long long)o.count);
    return &o;
  }
  return nullptr;
}

uint16_t* cacheGet(int z, int tx, int ty) {
  for (int i = 0; i < kCache; ++i) {
    if (s_cache[i].frame && s_cache[i].z == z && s_cache[i].tx == tx && s_cache[i].ty == ty) {
      return s_cache[i].frame;
    }
  }
  Rec r;
  File* pack = &s_pack;
  bool found;
  if (z <= 15) {
    found = findTile(z, (uint32_t)tx, (uint32_t)ty, r);
  } else {
    Overlay* o = ensureOverlay(z);
    if (!o) return nullptr;
    found = findTileOv(*o, z, (uint32_t)tx, (uint32_t)ty, r);
    pack = &o->pack;
  }
  if (!found) return nullptr;
  if (r.len == 0 || r.len > kMaxTile) return nullptr;
  Serial.printf("[map] decode z%d %d/%d off=%llu len=%u\n", z, tx, ty,
                (unsigned long long)r.off, (unsigned)r.len);
  if (!pack->seek(r.off) || pack->read(s_tile, r.len) != r.len) return nullptr;
  CacheEntry& e = s_cache[s_cacheNext];
  s_cacheNext = (s_cacheNext + 1) % kCache;
  e.z = -1;   // invalid until the decode succeeds
  s_decodeTarget = e.frame;
  if (s_jpeg == nullptr) return nullptr;
  if (!s_jpeg->openRAM(s_tile, (int)r.len, drawCb)) return nullptr;
  s_jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
  const int ok = s_jpeg->decode(0, 0, 0);
  s_jpeg->close();
  if (!ok) return nullptr;
  e.z = z;
  e.tx = tx;
  e.ty = ty;
  return e.frame;
}

// EST-352: boot self-test for the tile colour path. Decode the tile under the view centre, take one
// pixel the JPEG decoder produced (that value IS the JPEG colour), push it through the same
// omPushRgb565 the map render uses, and read it back out of the frame. A byte-order mistake is then
// a FAILED line at boot, the same way the gold frame probe catches it, instead of a magenta map
// that has to be diagnosed from a screenshot. The chosen pixel is one whose two bytes differ, so a
// broken swap cannot pass by accident.
void tilePixelProbe() {
  const int rz = (s_z == 18) ? 17 : s_z;
  const int tx = (int)floor(lonToXf(s_cLon, rz));
  const int ty = (int)floor(latToYf(s_cLat, rz));
  const uint16_t* frame = cacheGet(rz, tx, ty);
  if (frame == nullptr) {
    Serial.printf("[map] tile probe: no tile at z%d %d/%d\n", rz, tx, ty);
    return;
  }
  int px = -1, py = -1;
  uint16_t jpeg = 0;
  for (int y = 0; y < kTile && px < 0; ++y) {
    for (int x = 0; x < kTile; ++x) {
      const uint16_t p = frame[(size_t)y * kTile + x];
      if (p != 0 && ui::rgb565Swab(p) != p) {   // content, and not byte-symmetric
        jpeg = p;
        px = x;
        py = y;
        break;
      }
    }
  }
  if (px < 0) {
    Serial.println("[map] tile probe: no byte-asymmetric pixel in the tile");
    return;
  }
  uint16_t stored = 0, back = 0;
  const bool ok = omFrameColourProbe(jpeg, stored, back);
  Serial.printf("[map] tile pixel probe z%d %d/%d at %d,%d jpeg %04x -> stored %04x -> back %04x %s\n",
                rz, tx, ty, px, py, jpeg, stored, back, ok ? "OK" : "FAILED");
}

}  // namespace

bool mapReady() { return s_ready; }
uint32_t mapIndexBytes() { return s_count * kRec; }
int mapZoom() { return s_z; }
bool mapHasMarker() { return s_marker; }

bool mapBegin() {
  if (s_ready) return true;
  File idx = SD_MMC.open(kIdxPath, FILE_READ);
  if (!idx) {
    Serial.printf("[map] missing %s\n", kIdxPath);
    return false;
  }
  const size_t idxSize = idx.size();
  if (idxSize == 0 || idxSize % kRec != 0) {
    Serial.printf("[map] bad index size %u\n", (unsigned)idxSize);
    idx.close();
    return false;
  }
  s_index = (uint8_t*)ps_malloc(idxSize);
  s_tile = (uint8_t*)ps_malloc(kMaxTile);
  s_canvas = (uint16_t*)ps_malloc((size_t)kViewW * kViewH * 2);
  if (s_jpeg == nullptr) s_jpeg = psramObject<JPEGDEC>();
  bool framesOk = true;
  for (int i = 0; i < kCache; ++i) {
    s_cache[i].frame = (uint16_t*)ps_malloc((size_t)kTile * kTile * 2);
    framesOk = framesOk && s_cache[i].frame;
  }
  if (!s_index || !s_tile || !s_canvas || s_jpeg == nullptr || !framesOk) {
    Serial.println("[map] PSRAM allocation failed");
    idx.close();
    return false;
  }
  Serial.printf("[map] JPEG decoder %u B in PSRAM\n", (unsigned)sizeof(JPEGDEC));
  if (idx.read(s_index, idxSize) != idxSize) {
    Serial.println("[map] index read failed");
    idx.close();
    return false;
  }
  idx.close();
  s_count = (uint32_t)(idxSize / kRec);
  s_pack = SD_MMC.open(kPackPath, FILE_READ);
  if (!s_pack) {
    Serial.printf("[map] missing %s\n", kPackPath);
    return false;
  }
  // Optional z16 overlay. One code path only: this opens and KEEPS the index handle that the
  // file-based search needs (a second, local open here left s_idx2 unopened and broke z16).
  (void)ensureOverlay(16);
  s_ready = true;
  centreFromLatLon();
  Serial.printf("[map] index %u tiles (%u bytes), cache %d x %u KB\n", s_count,
                (unsigned)mapIndexBytes(), kCache, (unsigned)(kTile * kTile * 2 / 1024));
  // EST-352: boot check of one decoded tile pixel through the same push the render uses.
  tilePixelProbe();
  return true;
}

void mapSetView(double lat, double lon, int z) {
  if (z < kZMin) z = kZMin;
  if (z > kZMax) z = kZMax;
  s_cLat = lat;
  s_cLon = lon;
  s_z = z;
  centreFromLatLon();
}

void mapZoomBy(int delta) {
  int z = s_z + delta;
  if (z < kZMin) z = kZMin;
  if (z > kZMax) z = kZMax;
  if (z == s_z) return;
  s_z = z;
  centreFromLatLon();   // keep the same geographic centre across zooms
}

void mapPan(int dx, int dy) {
  s_cpx += dx;
  s_cpy += dy;
  s_cLon = xfToLon(s_cpx / kTile, s_z);
  s_cLat = yfToLat(s_cpy / kTile, s_z);
}

void mapScreenToLatLon(int x, int y, double& lat, double& lon) {
  const bool mag = (s_z == 18);
  const int rz = mag ? 17 : s_z;
  const double scale = mag ? 2.0 : 1.0;
  const double cpx = lonToXf(s_cLon, rz) * kTile;
  const double cpy = latToYf(s_cLat, rz) * kTile;
  const double px = cpx + (x - kViewW / 2.0) / scale;
  const double py = cpy + (y - kViewH / 2.0) / scale;
  lon = xfToLon(px / kTile, rz);
  lat = yfToLat(py / kTile, rz);
}

void mapSetMarker(double lat, double lon) {
  s_marker = true;
  s_mLat = lat;
  s_mLon = lon;
}

void mapClearMarker() { s_marker = false; }

// EST-351: the map chrome projects a contact or our own fix into the same 320x214 view the tiles
// use. False only when the pack is not open; a point outside the view is still projected (the
// caller decides whether it is worth drawing).
bool mapProject(double lat, double lon, int& x, int& y) {
  if (!s_ready) return false;
  const bool mag = (s_z == 18);
  const int rz = mag ? 17 : s_z;
  const double scale = mag ? 2.0 : 1.0;
  const double cpx = lonToXf(s_cLon, rz) * kTile;
  const double cpy = latToYf(s_cLat, rz) * kTile;
  x = (int)lround((lonToXf(lon, rz) * kTile - cpx) * scale + kViewW / 2.0);
  y = (int)lround((latToYf(lat, rz) * kTile - cpy) * scale + kViewH / 2.0);
  return true;
}

void canvasFill(uint16_t* c, int w, int x, int y, int rw, int rh, uint16_t color) {
  for (int yy = y; yy < y + rh; ++yy) {
    if (yy < 0) continue;
    for (int xx = x; xx < x + rw; ++xx) {
      if (xx < 0 || xx >= w) continue;
      c[(size_t)yy * w + xx] = color;
    }
  }
}



void mapRender() {
  const uint32_t renderStartUs = micros();   // EST-365: the tile decode + composite, named in `radio`
  auto* d = om_display();
  if (!s_ready) {
    d->fillRect(0, 0, kViewW, kViewH, kMapBg);
    return;
  }

  // z18 is not a data level: it renders z17 at 2x so the view can go closer offline.
  const bool mag = (s_z == 18);
  const int rz = mag ? 17 : s_z;
  const double scale = mag ? 2.0 : 1.0;
  const double cpx = lonToXf(s_cLon, rz) * kTile;
  const double cpy = latToYf(s_cLat, rz) * kTile;
  const double viewW = kViewW / scale;
  const double viewH = kViewH / scale;
  const double left = cpx - viewW / 2.0;
  const double top = cpy - viewH / 2.0;

  const int tx0 = (int)floor(left / kTile), tx1 = (int)floor((left + viewW - 1) / kTile);
  const int ty0 = (int)floor(top / kTile), ty1 = (int)floor((top + viewH - 1) / kTile);

  memset(s_canvas, 0x00, (size_t)kViewW * kViewH * 2);
  int drawn = 0;
  for (int ty = ty0; ty <= ty1; ++ty) {
    for (int tx = tx0; tx <= tx1; ++tx) {
      if (tx < 0 || ty < 0) continue;
      const uint16_t* frame = cacheGet(rz, tx, ty);
      if (!frame) continue;
      const int destX = (int)lround((tx * (double)kTile - left) * scale);
      const int destY = (int)lround((ty * (double)kTile - top) * scale);
      const int span = (int)lround(kTile * scale);
      const int x0 = destX < 0 ? 0 : destX;
      const int y0 = destY < 0 ? 0 : destY;
      const int x1 = (destX + span) > kViewW ? kViewW : (destX + span);
      const int y1 = (destY + span) > kViewH ? kViewH : (destY + span);
      for (int yy = y0; yy < y1; ++yy) {
        const int sy = (int)((yy - destY) / scale);
        const uint16_t* src = frame + (size_t)sy * kTile;
        uint16_t* dst = s_canvas + (size_t)yy * kViewW;
        if (!mag) {
          memcpy(dst + x0, src + (x0 - destX), (size_t)(x1 - x0) * 2);
        } else {
          for (int xx = x0; xx < x1; ++xx) dst[xx] = src[(int)((xx - destX) / scale)];
        }
      }
      ++drawn;
    }
  }

  // EST-351: the zoom / lock keys, the markers and the traces are ui/screen_map.cpp's chrome,
  // drawn over this canvas inside the same frame. The canvas holds tiles and nothing else.

  // One pushImage is capped by the 16-bit SPI transfer length (~64 KB, ~100 rows at 320 px).
  // EST-352: the canvas holds native RGB565 words; omPushRgb565 is the one rule that makes the
  // frame store them correctly (pushing the words raw every pixel landed byte-swapped).
  for (int yy = 0; yy < kViewH; yy += 16) {
    const int h = (yy + 16 <= kViewH) ? 16 : (kViewH - yy);
    omPushRgb565(d, 0, yy, kViewW, h, s_canvas + (size_t)yy * kViewW);
  }

  // The tile attribution is NOT drawn here: it is the map's licence line and ui/screen_map.cpp
  // draws it straight on the map (EST-368), outlined, on the bottom left.

  // The on-screen state line is gone (EST-351): the chip in ui/screen_map.cpp says the zoom, and
  // the serial still says whether any tile was drawn.
  if (drawn == 0) {
    Serial.printf("[map] BLANK z%d (render z%d) tx %d..%d ty %d..%d\n", s_z, rz, tx0, tx1,
                  ty0, ty1);
  }
  Serial.printf("[map] z%d%s %.5f,%.5f -> %d tiles\n", s_z, mag ? " (2x)" : "", s_cLat, s_cLon, drawn);
  om::radio::addPart(om::radio::Part::MapDecode, micros() - renderStartUs);
}

// EST-368: one animation frame's worth of tiles. The canvas was composited by the last mapRender();
// this only pushes it again, so a moving trace dot never costs a JPEG decode or an SD read. Safe on
// a screen that was never rendered (mapScreenDraw always renders before an animation starts).
void mapRepush() {
  auto* d = om_display();
  if (!s_ready || d == nullptr || s_canvas == nullptr) return;
  for (int yy = 0; yy < kViewH; yy += 16) {
    const int h = (yy + 16 <= kViewH) ? 16 : (kViewH - yy);
    omPushRgb565(d, 0, yy, kViewW, h, s_canvas + (size_t)yy * kViewW);
  }
}

}  // namespace om
