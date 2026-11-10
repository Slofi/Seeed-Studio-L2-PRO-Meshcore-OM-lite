// SPDX-License-Identifier: MIT
// EST-362: the RPTR interaction model's implementation. Plain arithmetic and text, host-tested.
#include "rptr_page.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace om {
namespace rptr {
namespace {

void copyText(char* dst, size_t cap, const char* src) {
  if (dst == nullptr || cap == 0) return;
  if (src == nullptr) src = "";
  size_t n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = 0;
}

void addRow(ScreenOut* out, const char* label, const char* value, Act act, bool on = false,
            uint8_t index = 0) {
  if (out == nullptr || out->count >= kUiRowCap) return;
  UiRow& r = out->rows[out->count++];
  copyText(r.label, sizeof(r.label), label);
  copyText(r.value, sizeof(r.value), value == nullptr ? "" : value);
  r.act = act;
  r.on = on;
  r.index = index;
}

// The region a row stands for: the wildcard region is shown as "*" like CommonCLI does.
const char* regionName(const Region& r) { return r.name[0] ? r.name : "*"; }

bool regionIsWildcard(const Region& r) { return r.wildcard || strcmp(regionName(r), "*") == 0; }

// "46.25000" from MeshCore's micro-degrees: five decimals (the store is a float) and always the same
// width. EST-364: the old "%05u" printed SIX digits whenever the fraction started with a digit, so
// Argus's "14.501452" and "46.05000" did not line up and the wrong field read as a real number.
void coordText(char* out, size_t cap, int32_t e6) {
  if (out == nullptr || cap == 0) return;
  const bool neg = e6 < 0;
  const int64_t micro = neg ? -(int64_t)e6 : (int64_t)e6;
  const int64_t five = (micro + 5) / 10;   // 1e-5 degrees, rounded
  snprintf(out, cap, "%s%lld.%05u", (neg && five != 0) ? "-" : "", (long long)(five / 100000),
           (unsigned)(five % 100000));
}

void positionText(char* out, size_t cap, const Facts& f) {
  if (out == nullptr || cap == 0) return;
  if (!f.posKnown) {
    copyText(out, cap, "not set");
    return;
  }
  char lat[20];
  char lon[20];
  coordText(lat, sizeof(lat), f.latE6);
  coordText(lon, sizeof(lon), f.lonE6);
  // EST-364: a repeater that shares no position answers two zeroes. "0.00000, 0.00000" looks like a
  // place in the Gulf of Guinea, so the row says what it means.
  if (positionNotShared(f.latE6, f.lonE6)) {
    copyText(out, cap, "not shared");
    return;
  }
  snprintf(out, cap, "%s, %s", lat, lon);
}

void regionSummary(char* out, size_t cap, const RegionList& list) {
  if (out == nullptr || cap == 0) return;
  out[0] = 0;
  if (!list.valid) {
    copyText(out, cap, "not read");
    return;
  }
  if (list.count == 0) {
    copyText(out, cap, "none");
    return;
  }
  size_t n = 0;
  for (int i = 0; i < list.count; ++i) {
    const char* name = regionName(list.rows[i]);
    const int w = snprintf(out + n, cap - n, "%s%s", i == 0 ? "" : ", ", name);
    if (w < 0) break;
    const size_t room = cap - n;
    n += (size_t)w < room ? (size_t)w : room - 1;
    if (n + 1 >= cap) break;
  }
}

void radioText(char* out, size_t cap, const RadioValues& v) {
  char f[16];
  char b[16];
  freqText(f, sizeof(f), v.freq_khz);
  bwText(b, sizeof(b), v.bw_hz);
  snprintf(out, cap, "%s MHz, SF%u, BW %s", f, (unsigned)v.sf, b);
}

void advertText(char* out, size_t cap, const Facts& f) {
  if (f.advertLocalKnown && f.advertFloodKnown) {
    snprintf(out, cap, "local %u min, flood %u h", (unsigned)f.advertLocal,
             (unsigned)f.advertFlood);
  } else {
    copyText(out, cap, "not read");
  }
}

void moreText(char* out, size_t cap, const Facts& f) {
  if (f.floodMaxKnown && f.loopKnown) {
    snprintf(out, cap, "flood max %u, loop %s", (unsigned)f.floodMax, loopDetectText(f.loop));
  } else {
    copyText(out, cap, "not read");
  }
}

// "12 s ago" / "3 min ago": the neighbour page's fallback when the shell has no clock time.
void agoText(char* out, size_t cap, int32_t secs) {
  if (secs < 60) snprintf(out, cap, "%ds ago", (int)secs);
  else if (secs < 3600) snprintf(out, cap, "%d min ago", (int)(secs / 60));
  else snprintf(out, cap, "%d h ago", (int)(secs / 3600));
}

}  // namespace

// ---------------------------------------------------------------------------------------------- pages
const char* pageName(Page p) {
  switch (p) {
    case Page::List: return "Repeaters";
    case Page::Repeater: return "Repeater";
    case Page::Name: return "Name";
    case Page::Position: return "Position";
    case Page::Radio: return "Radio";
    case Page::Tx: return "TX power";
    case Page::Repeat: return "Repeat";
    case Page::Regions: return "Regions";
    case Page::AddRegion: return "Add region";
    case Page::HomePicker: return "Home region";
    case Page::DefaultPicker: return "Default scope";
    case Page::Neighbours: return "Neighbours";
    case Page::Advertising: return "Advertising";
    case Page::More: return "More";
    case Page::Console: return "Console";
    default: return "";
  }
}

// ------------------------------------------------------------------------------------------- facts
void Facts::reset() {
  *this = Facts();
  radio = RadioValues();
  clock = 0;
}

// ------------------------------------------------------------------------------------------ drafts
void Draft::reset(const Facts& f, const RegionList& regions, const char* homeName,
                  const char* defScope) {
  v.radio = f.radio;
  v.radio.tx = f.tx;
  v.repeat = f.repeat;
  v.advertLocal = f.advertLocal;
  v.advertFlood = f.advertFlood;
  v.floodMax = f.floodMax;
  v.loop = f.loop;
  v.pathHash = f.pathHash;
  copyText(name, sizeof(name), f.name);
  latE6 = f.latE6;
  lonE6 = f.lonE6;
  for (int i = 0; i < kMaxRegions; ++i) {
    flood[i] = i < regions.count ? regions.rows[i].allow : false;
    floodChanged[i] = false;
  }
  hasAdd = false;
  addName[0] = 0;
  addParent[0] = 0;
  parentSel = -1;
  homeChanged = false;
  copyText(home, sizeof(home), homeName == nullptr ? "*" : homeName);
  defaultChanged = false;
  defaultNone = true;
  defaultScope[0] = 0;
  if (defScope != nullptr) {
    defaultNone = defScope[0] == 0;
    copyText(defaultScope, sizeof(defaultScope), defScope);
  }
}

// ----------------------------------------------------------------------------------------- the list
int unnamedText(const uint8_t* prefix6, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  if (prefix6 == nullptr) {
    copyText(out, cap, "unnamed");
    return (int)strlen(out);
  }
  snprintf(out, cap, "unnamed %02x%02x%02x", prefix6[0], prefix6[1], prefix6[2]);
  return (int)strlen(out);
}

const char* signalWord(int8_t snr4) {
  // The dB value rounds to the nearest dB; the words are the ticket's own scale.
  const float db = (float)snr4 / 4.0f;
  if (db >= 12.0f) return "excellent";
  if (db >= 6.0f) return "good";
  if (db >= 0.0f) return "fair";
  if (db >= -4.0f) return "weak";
  return "poor";
}

bool signalText(bool has, int8_t snr4, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = 0;
  if (!has) return false;
  const int db = (int)lround((double)snr4 / 4.0);
  snprintf(out, cap, "%s %d dB", signalWord(snr4), db);
  return true;
}

int listRows(const FoundList& found, const char* const* remembered, int rememberedCount,
             ListRow* out, int cap) {
  if (out == nullptr || cap <= 0) return 0;
  int n = 0;
  const int rememberedCap = rememberedCount < cap ? rememberedCount : cap;
  for (int i = 0; i < rememberedCap; ++i) {
    if (remembered[i] == nullptr || remembered[i][0] == 0) continue;
    ListRow& row = out[n++];
    copyText(row.name, sizeof(row.name), remembered[i]);
    row.saved = true;
    row.foundIndex = 255;
    for (int k = 0; k < found.count; ++k) {
      if (strcmp(found.rows[k].name, remembered[i]) == 0) {
        row.foundIndex = (uint8_t)k;
        row.inContacts = found.rows[k].inContacts;
        const Found& f = found.rows[k];
        signalText(f.hasSnrOut || f.hasSnrIn, f.hasSnrOut ? f.snrOut4 : f.snrIn4, row.signal,
                   sizeof(row.signal));
        break;
      }
    }
    if (n >= cap) return n;
  }
  for (int k = 0; k < found.count && n < cap; ++k) {
    bool already = false;
    for (int i = 0; i < rememberedCap; ++i) {
      if (remembered[i] != nullptr && strcmp(found.rows[k].name, remembered[i]) == 0) {
        already = true;
        break;
      }
    }
    if (already) continue;
    ListRow& row = out[n++];
    if (found.rows[k].name[0]) copyText(row.name, sizeof(row.name), found.rows[k].name);
    else unnamedText(found.rows[k].prefix, row.name, sizeof(row.name));
    row.foundIndex = (uint8_t)k;
    row.inContacts = found.rows[k].inContacts;
    const Found& f = found.rows[k];
    signalText(f.hasSnrOut || f.hasSnrIn, f.hasSnrOut ? f.snrOut4 : f.snrIn4, row.signal,
               sizeof(row.signal));
  }
  return n;
}

// --------------------------------------------------------------------------------------- the screen
void ScreenOut::reset() {
  // EST-363: in place, no temporary. Every member's default is zero or empty, so the bulk clear IS
  // the whole reset; the list of fields that are NOT zero is deliberately empty and must stay so.
  // The cast silences -Wclass-memaccess (the type has default member initialisers but is trivially
  // copyable, which is the property a bulk clear needs).
  memset(static_cast<void*>(this), 0, sizeof(*this));
}

// EST-363's size guard. The loop task's stack is 16,384 B (main.cpp kLoopStackSize); this model is a
// static (RptrView::screen inside g_rptrViewStore) and must never be copied wholesale again. Sizes
// measured on the EST-362 ELF's DWARF: ScreenOut 3,812 B, UiRow 98 B, ScreenIn 60 B. The caps leave a
// little room to grow but stop the kind of growth that forces a copy back onto the stack.
static_assert(sizeof(ScreenOut) <= 4096, "ScreenOut over 4 KB: check the RPTR stack budget (EST-363)");
static_assert(sizeof(UiRow) <= 128, "UiRow over 128 B: 26 rows would no longer fit ScreenOut");
static_assert(sizeof(ScreenIn) <= 128, "ScreenIn over 128 B: it is passed by reference, keep it small");
// The row text buffers the drawing reads; a change here is a change to every row's fit.
static_assert(sizeof(UiRow::label) == 24, "row label buffer changed");
static_assert(sizeof(UiRow::value) == 64, "row value buffer changed");
static_assert(sizeof(ScreenOut::title) == 40, "page title buffer changed");
static_assert(sizeof(ScreenOut::banner) == 120, "page banner buffer changed");

int buildScreen(const ScreenIn& in, ScreenOut* out) {
  if (out == nullptr) return 0;
  out->reset();
  out->title[0] = 0;

  switch (in.page) {
    case Page::List:
      return 0;

    case Page::Repeater: {
      const char* name = in.name != nullptr && in.name[0] ? in.name : "Repeater";
      copyText(out->title, sizeof(out->title), name);
      if (!in.loggedIn) {
        copyText(out->subtitle, sizeof(out->subtitle), "repeater, not logged in");
        const AnonFacts* a = in.anon;
        char regions[64] = "not read";
        char clock[40] = "not read";
        char signal[32] = "not heard in the last scan";
        if (a != nullptr) {
          if (a->valid) {
            copyText(regions, sizeof(regions), a->regionCount > 0 ? a->regions : "none");
            clockStampText(clock, sizeof(clock), a->clock);
          } else if (a->asked) {
            copyText(regions, sizeof(regions), "reading...");
            copyText(clock, sizeof(clock), "reading...");
          }
          char s[24];
          if (signalText(a->signalKnown, a->snr4, s, sizeof(s))) {
            copyText(signal, sizeof(signal), s);
          }
        }
        addRow(out, "Regions", regions, Act::None);
        addRow(out, "Clock", clock, Act::None);
        addRow(out, "Signal", signal, Act::None);
        if (in.remembered) addRow(out, "Forget password", "", Act::Forget);
        out->bigButton = true;
        copyText(out->bigLabel, sizeof(out->bigLabel), in.busy ? "Logging in..." : "Log in");
        if (in.busy) copyText(out->banner, sizeof(out->banner), "Logging in to the repeater...");
        else if (in.remembered) copyText(out->banner, sizeof(out->banner),
                                         "Password remembered: Log in is one tap.");
        return out->count;
      }

      copyText(out->subtitle, sizeof(out->subtitle), "repeater, logged in");
      const Status* st = in.status;
      const Facts* f = in.facts != nullptr ? in.facts : nullptr;
      const Facts empty;
      if (f == nullptr) f = &empty;

      // The status card: battery, noise floor, uptime, airtime, clock.
      auto cell = [&](const char* label, const char* value) {
        if (out->cellCount >= 6) return;
        StatusCell& c = out->cells[out->cellCount++];
        copyText(c.label, sizeof(c.label), label);
        copyText(c.value, sizeof(c.value), value);
      };
      char v[40];
      if (st != nullptr && st->valid) {
        char volts[16];
        batteryVoltsText(volts, sizeof(volts), st->battMv);
        const int pct = batteryPercent(st->battMv);
        if (pct >= 0) snprintf(v, sizeof(v), "%s %d %%", volts, pct);
        else snprintf(v, sizeof(v), "%s", volts);
        cell("Battery", v);
        snprintf(v, sizeof(v), "%d dBm", (int)st->noiseFloor);
        cell("Noise", v);
        uptimeText(v, sizeof(v), st->uptimeSec);
        cell("Uptime", v);
        uptimeText(v, sizeof(v), st->airtimeSec);
        cell("Airtime", v);
      } else {
        // EST-364: a status request that has not been answered yet says exactly that, in dim ink
        // beside the refresh icon. It is not a refusal, so nothing here is red and nothing says
        // "Refused"; only an answered request fills the numbers in.
        cell("Status", in.statusWait ? "no answer yet" : "tap refresh");
        if (out->cellCount > 0) out->cells[out->cellCount - 1].dim = true;
        cell("Battery", "n/a");
        cell("Noise", "n/a");
        cell("Uptime", "n/a");
        cell("Airtime", "n/a");
      }
      clockText(v, sizeof(v), f->clock);
      cell("Clock", v);

      if (in.clockWrong) {
        char line[120];
        clockStampText(v, sizeof(v), f->clock);
        snprintf(line, sizeof(line), "Clock is wrong: %s", v);
        copyText(out->banner, sizeof(out->banner), line);
        out->bannerButton = true;
      }

      char pos[48];
      positionText(pos, sizeof(pos), *f);
      addRow(out, "Name", f->nameKnown ? f->name : "not read", Act::Open, false,
             (uint8_t)Page::Name);
      addRow(out, "Position", pos, Act::Open, false, (uint8_t)Page::Position);
      char radio[48];
      if (f->radioKnown) radioText(radio, sizeof(radio), f->radio);
      else copyText(radio, sizeof(radio), "not read");
      addRow(out, "Radio", radio, Act::Open, false, (uint8_t)Page::Radio);
      char tx[16];
      if (f->txKnown) snprintf(tx, sizeof(tx), "%d dBm", (int)f->tx);
      else copyText(tx, sizeof(tx), "not read");
      addRow(out, "TX power", tx, Act::Open, false, (uint8_t)Page::Tx);
      char regs[64];
      if (in.regions != nullptr) regionSummary(regs, sizeof(regs), *in.regions);
      else copyText(regs, sizeof(regs), "not read");
      addRow(out, "Regions", regs, Act::Open, false, (uint8_t)Page::Regions);
      char neigh[24];
      if (in.neighbours != nullptr && in.neighbours->valid) {
        snprintf(neigh, sizeof(neigh), "%d heard", in.neighbours->count);
      } else {
        copyText(neigh, sizeof(neigh), "not read");
      }
      addRow(out, "Neighbours", neigh, Act::Open, false, (uint8_t)Page::Neighbours);
      char adv[40];
      advertText(adv, sizeof(adv), *f);
      addRow(out, "Advertising", adv, Act::Open, false, (uint8_t)Page::Advertising);
      addRow(out, "Repeat", f->repeatKnown ? (f->repeat ? "on" : "off") : "not read", Act::Open,
             f->repeat, (uint8_t)Page::Repeat);
      char more[48];
      moreText(more, sizeof(more), *f);
      addRow(out, "More", more, Act::Open, false, (uint8_t)Page::More);
      if (out->count < kUiRowCap) {
        UiRow& restart = out->rows[out->count++];
        copyText(restart.label, sizeof(restart.label), "Restart repeater");
        restart.act = Act::Restart;
        restart.danger = true;
      }
      // Power users only: the raw CLI log and the free line live at the very bottom.
      addRow(out, "Console", "raw command log", Act::Open, false, (uint8_t)Page::Console);
      return out->count;
    }

    case Page::Name: {
      copyText(out->title, sizeof(out->title), "Name");
      const Draft* d = in.draft;
      addRow(out, "Name", d ? d->name : "", Act::Edit, false, 0);
      out->hasApply = true;
      return out->count;
    }

    case Page::Position: {
      copyText(out->title, sizeof(out->title), "Position");
      const Draft* d = in.draft;
      char lat[20] = "";
      char lon[20] = "";
      if (d != nullptr) {
        coordText(lat, sizeof(lat), d->latE6);
        coordText(lon, sizeof(lon), d->lonE6);
      }
      addRow(out, "Latitude", lat, Act::Edit, false, 0);
      addRow(out, "Longitude", lon, Act::Edit, false, 1);
      copyText(out->banner, sizeof(out->banner), "Five decimals, the repeater's own precision.");
      out->hasApply = true;
      return out->count;
    }

    case Page::Radio: {
      copyText(out->title, sizeof(out->title), "Radio");
      const Draft* d = in.draft;
      const RadioValues rv = d ? d->v.radio : RadioValues();
      char v[24];
      char f[16];
      freqText(f, sizeof(f), rv.freq_khz);
      snprintf(v, sizeof(v), "%s MHz", f);
      addRow(out, "Frequency", v, Act::Step, false, (uint8_t)RadioKey::Freq);
      out->rows[out->count - 1].stepKeys = true;
      char b[12];
      bwText(b, sizeof(b), rv.bw_hz);
      snprintf(v, sizeof(v), "%s kHz", b);
      addRow(out, "Bandwidth", v, Act::Step, false, (uint8_t)RadioKey::Bw);
      out->rows[out->count - 1].stepKeys = true;
      snprintf(v, sizeof(v), "SF%u", (unsigned)rv.sf);
      addRow(out, "Spread factor", v, Act::Step, false, (uint8_t)RadioKey::Sf);
      out->rows[out->count - 1].stepKeys = true;
      snprintf(v, sizeof(v), "CR%u", (unsigned)rv.cr);
      addRow(out, "Coding rate", v, Act::Step, false, (uint8_t)RadioKey::Cr);
      out->rows[out->count - 1].stepKeys = true;
      out->hasApply = true;
      out->applyDanger = true;
      copyText(out->banner, sizeof(out->banner),
               "The mesh radio the repeater uses. A change needs a restart to take effect.");
      return out->count;
    }

    case Page::Tx: {
      copyText(out->title, sizeof(out->title), "TX power");
      const Draft* d = in.draft;
      char v[16];
      snprintf(v, sizeof(v), "%d dBm", d ? (int)d->v.radio.tx : 14);
      addRow(out, "TX power", v, Act::Step, false, (uint8_t)RadioKey::Tx);
      out->rows[out->count - 1].stepKeys = true;
      out->hasApply = true;
      return out->count;
    }

    case Page::Repeat: {
      copyText(out->title, sizeof(out->title), "Repeat");
      const Draft* d = in.draft;
      addRow(out, "Repeat floods", d && d->v.repeat ? "on" : "off", Act::Toggle);
      out->rows[out->count - 1].on = d ? d->v.repeat : true;
      out->hasApply = true;
      copyText(out->banner, sizeof(out->banner),
               "Off means this repeater stops passing other nodes' floods on.");
      return out->count;
    }

    case Page::Regions: {
      copyText(out->title, sizeof(out->title), "Regions");
      const Draft* d = in.draft;
      const RegionList* list = in.regions;
      if (list == nullptr || !list->valid) {
        addRow(out, "Regions", "not read", Act::None);
      } else if (list->count == 0) {
        addRow(out, "Regions", "none", Act::None);
      } else {
        for (int i = 0; i < list->count && out->count < kUiRowCap - 4; ++i) {
          const Region& rg = list->rows[i];
          UiRow& r = out->rows[out->count++];
          copyText(r.label, sizeof(r.label), regionName(rg));
          copyText(r.value, sizeof(r.value), "Repeats floods here");
          r.act = Act::RegionToggle;
          r.index = (uint8_t)i;
          r.on = d != nullptr && i < kMaxRegions ? d->flood[i] : rg.allow;
          if (!regionIsWildcard(rg)) copyText(r.key, sizeof(r.key), "x");
        }
      }
      char v[32];
      if (in.home == nullptr) copyText(v, sizeof(v), "not read");
      else if (strcmp(in.home, "*") == 0) copyText(v, sizeof(v), "* (anywhere)");
      else copyText(v, sizeof(v), in.home);
      addRow(out, "Home region", v, Act::Pick, false, 0);
      out->rows[out->count - 1].arrow = true;
      if (in.defaultScope == nullptr) copyText(v, sizeof(v), "not read");
      else if (in.defaultScope[0] == 0) copyText(v, sizeof(v), "none");
      else copyText(v, sizeof(v), in.defaultScope);
      addRow(out, "Default scope", v, Act::Pick, false, 1);
      out->rows[out->count - 1].arrow = true;
      addRow(out, "Add region", "", Act::AddRegion);
      out->rows[out->count - 1].arrow = true;
      out->hasApply = true;
      return out->count;
    }

    case Page::AddRegion: {
      copyText(out->title, sizeof(out->title), "Add region");
      const Draft* d = in.draft;
      addRow(out, "Name", d ? d->addName : "", Act::Edit, false, 0);
      char parent[40] = "none";
      if (d != nullptr && d->parentSel >= 0 && in.regions != nullptr &&
          d->parentSel < in.regions->count) {
        copyText(parent, sizeof(parent), regionName(in.regions->rows[d->parentSel]));
      }
      addRow(out, "Parent", parent, Act::Toggle);
      out->rows[out->count - 1].on = d != nullptr && d->parentSel >= 0;
      out->hasApply = true;
      copyText(out->banner, sizeof(out->banner), "Tap Parent to change it. New regions repeat floods.");
      return out->count;
    }

    case Page::HomePicker:
    case Page::DefaultPicker: {
      const bool home = in.page == Page::HomePicker;
      copyText(out->title, sizeof(out->title), home ? "Home region" : "Default scope");
      const Draft* d = in.draft;
      if (home) {
        addRow(out, "* (anywhere)", "no home region", Act::Toggle, false, 0);
        out->rows[out->count - 1].on = d == nullptr || d->home[0] == 0 || strcmp(d->home, "*") == 0;
      } else {
        addRow(out, "none", "no default scope", Act::Toggle, false, 0);
        out->rows[out->count - 1].on = d != nullptr && d->defaultChanged && d->defaultNone;
        addRow(out, "* (anywhere)", "the wildcard", Act::Toggle, false, 1);
        out->rows[out->count - 1].on = d != nullptr && !d->defaultNone &&
                                      strcmp(d->defaultScope, "*") == 0;
      }
      const RegionList* list = in.regions;
      if (list != nullptr && list->valid) {
        for (int i = 0; i < list->count && out->count < kUiRowCap; ++i) {
          const Region& rg = list->rows[i];
          if (regionIsWildcard(rg)) continue;
          // 0 = anywhere / none, 1 = the wildcard choice, 2 + ordinal = one region.
          addRow(out, regionName(rg), "", Act::Toggle, false, (uint8_t)(2 + i));
          UiRow& r = out->rows[out->count - 1];
          if (home) r.on = d != nullptr && strcmp(d->home, regionName(rg)) == 0;
          else r.on = d != nullptr && !d->defaultNone &&
                      strcmp(d->defaultScope, regionName(rg)) == 0;
        }
      }
      out->hasApply = true;
      copyText(out->banner, sizeof(out->banner), "Pick one, then Apply.");
      return out->count;
    }

    case Page::Neighbours: {
      copyText(out->title, sizeof(out->title), "Neighbours");
      addRow(out, "Discover neighbours", "ask the repeater", Act::Discover);
      const NeighbourList* list = in.neighbours;
      if (list == nullptr || !list->valid) {
        addRow(out, "Neighbours", "not read", Act::None);
      } else if (list->count == 0) {
        addRow(out, "Neighbours", "none", Act::None);
      } else {
        for (int i = 0; i < list->count && out->count < kUiRowCap; ++i) {
          const Neighbour& nb = list->rows[i];
          char v[48];
          char snr[12];
          char ago[20];
          snrText(snr, sizeof(snr), nb.snr4);
          agoText(ago, sizeof(ago), nb.secsAgo < 0 ? 0 : nb.secsAgo);
          snprintf(v, sizeof(v), "SNR %s, %s", snr, ago);
          addRow(out, nb.name[0] ? nb.name : "?", v, Act::None);
        }
      }
      return out->count;
    }

    case Page::Advertising: {
      copyText(out->title, sizeof(out->title), "Advertising");
      const Draft* d = in.draft;
      char v[24];
      snprintf(v, sizeof(v), "%u min", d ? (unsigned)d->v.advertLocal : 0);
      addRow(out, "Local advert", v, Act::Step, false, 0);
      out->rows[out->count - 1].stepKeys = true;
      snprintf(v, sizeof(v), "%u h", d ? (unsigned)d->v.advertFlood : 0);
      addRow(out, "Flood advert", v, Act::Step, false, 1);
      out->rows[out->count - 1].stepKeys = true;
      out->hasApply = true;
      return out->count;
    }

    case Page::More: {
      copyText(out->title, sizeof(out->title), "More");
      const Draft* d = in.draft;
      char v[24];
      snprintf(v, sizeof(v), "%u hops", d ? (unsigned)d->v.floodMax : 0);
      addRow(out, "Flood max", v, Act::Step, false, 0);
      out->rows[out->count - 1].stepKeys = true;
      addRow(out, "Loop detect", d ? loopDetectText(d->v.loop) : "", Act::Toggle, d != nullptr, 10);
      addRow(out, "Path hash", d ? pathHashText(d->v.pathHash) : "", Act::Toggle, d != nullptr, 11);
      out->hasApply = true;
      return out->count;
    }

    case Page::Console: {
      copyText(out->title, sizeof(out->title), "Console");
      copyText(out->subtitle, sizeof(out->subtitle), "raw command and answer log");
      out->hasConsole = true;
      for (int i = 0; i < in.logCount && i < kConsoleLines; ++i) {
        if (in.log == nullptr || in.log[i] == nullptr) continue;
        copyText(out->logLines[out->logLineCount++], sizeof(out->logLines[0]), in.log[i]);
      }
      // The free command line is the page's only control (the layout's input + Send). The log
      // lines above are not rows at all: there is nothing to tap in an answer.
      copyText(out->consoleInput, sizeof(out->consoleInput),
               in.consoleInput == nullptr ? "" : in.consoleInput);
      return out->count;
    }

    default:
      return 0;
  }
}

// ---------------------------------------------------------------------------------------- the apply
namespace {

int pushLine(ApplyPlan* out, char* line, int n) {
  if (out == nullptr || n <= 0) return 0;
  if (out->count >= kApplyMax) return 0;
  copyText(out->line[out->count], sizeof(out->line[0]), line);
  return ++out->count;
}

}  // namespace

int applyPlan(Page page, const Draft& d, const RegionList& regions, ApplyPlan* out) {
  if (out == nullptr) return 0;
  *out = ApplyPlan();
  char line[96];
  switch (page) {
    case Page::Name:
      return pushLine(out, line, cliName(line, sizeof(line), d.name));
    case Page::Position:
      pushLine(out, line, cliLat(line, sizeof(line), d.latE6));
      pushLine(out, line, cliLon(line, sizeof(line), d.lonE6));
      return out->count;
    case Page::Radio:
      return pushLine(out, line, cliRadio(line, sizeof(line), d.v.radio));
    case Page::Tx:
      return pushLine(out, line, cliTx(line, sizeof(line), d.v.radio));
    case Page::Repeat:
      return pushLine(out, line, cliRepeat(line, sizeof(line), d.v.repeat));
    case Page::Advertising:
      pushLine(out, line, cliAdvertLocal(line, sizeof(line), d.v.advertLocal));
      pushLine(out, line, cliAdvertFlood(line, sizeof(line), d.v.advertFlood));
      return out->count;
    case Page::More:
      pushLine(out, line, cliFloodMax(line, sizeof(line), d.v.floodMax));
      pushLine(out, line, cliLoopDetect(line, sizeof(line), d.v.loop));
      pushLine(out, line, cliPathHash(line, sizeof(line), d.v.pathHash));
      return out->count;
    case Page::AddRegion: {
      const char* parent = nullptr;
      if (d.parentSel >= 0 && d.parentSel < regions.count) {
        parent = regionName(regions.rows[d.parentSel]);
      }
      if (cliRegionPut(line, sizeof(line), d.addName, parent) > 0) {
        pushLine(out, line, (int)strlen(line));
        copyText(line, sizeof(line), kCliRegionSave);
        pushLine(out, line, (int)strlen(line));
      }
      return out->count;
    }
    case Page::Regions: {
      for (int i = 0; i < regions.count && i < kMaxRegions; ++i) {
        if (!d.floodChanged[i]) continue;
        const char* name = regionName(regions.rows[i]);
        if (name[0] == 0) continue;
        pushLine(out, line, cliRegionFlood(line, sizeof(line), name, d.flood[i]));
      }
      if (d.homeChanged) {
        if (d.home[0] == 0 || strcmp(d.home, "*") == 0) {
          pushLine(out, line, cliRegionHomeNone(line, sizeof(line)));
        } else {
          pushLine(out, line, cliRegionHome(line, sizeof(line), d.home));
        }
      }
      if (d.defaultChanged) {
        if (d.defaultNone) pushLine(out, line, cliRegionDefaultNone(line, sizeof(line)));
        else pushLine(out, line, cliRegionDefault(line, sizeof(line), d.defaultScope));
      }
      if (d.hasAdd) {
        pushLine(out, line,
                 cliRegionPut(line, sizeof(line), d.addName,
                              d.addParent[0] ? d.addParent : nullptr));
      }
      // The ticket's rule: a region change is followed by the save in the same step. `set` lines need
      // no save (CommonCLI saves from every set handler); the region map is the one that does.
      if (out->count > 0) {
        copyText(line, sizeof(line), kCliRegionSave);
        pushLine(out, line, (int)strlen(line));
      }
      return out->count;
    }
    case Page::HomePicker:
      pushLine(out, line, d.home[0] == 0 || strcmp(d.home, "*") == 0
                               ? cliRegionHomeNone(line, sizeof(line))
                               : cliRegionHome(line, sizeof(line), d.home));
      copyText(line, sizeof(line), kCliRegionSave);
      pushLine(out, line, (int)strlen(line));
      return out->count;
    case Page::DefaultPicker:
      pushLine(out, line, d.defaultNone ? cliRegionDefaultNone(line, sizeof(line))
                                        : cliRegionDefault(line, sizeof(line), d.defaultScope));
      copyText(line, sizeof(line), kCliRegionSave);
      pushLine(out, line, (int)strlen(line));
      return out->count;
    default:
      return 0;
  }
}

int readBackLines(Page page, char out[kReadBackMax][24]) {
  const char* lines[kReadBackMax] = {};
  int n = 0;
  switch (page) {
    case Page::Repeater: lines[n++] = "clock"; break;
    case Page::Name: lines[n++] = "get name"; break;
    case Page::Position: lines[n++] = "get lat"; lines[n++] = "get lon"; break;
    case Page::Radio: lines[n++] = "get radio"; break;
    case Page::Tx: lines[n++] = "get tx"; break;
    case Page::Repeat: lines[n++] = "get repeat"; break;
    case Page::Regions:
    case Page::AddRegion:
    case Page::HomePicker:
    case Page::DefaultPicker:
      lines[n++] = "region";
      lines[n++] = "region home";
      lines[n++] = "region default";
      break;
    case Page::Neighbours: lines[n++] = "neighbors"; break;
    case Page::Advertising:
      lines[n++] = "get advert.interval";
      lines[n++] = "get flood.advert.interval";
      break;
    case Page::More:
      lines[n++] = "get flood.max";
      lines[n++] = "get loop.detect";
      lines[n++] = "get path.hash.mode";
      break;
    default: break;
  }
  for (int i = 0; i < n; ++i) copyText(out[i], sizeof(out[0]), lines[i]);
  return n;
}

// -------------------------------------------------------------------------------------- confirmation
ConfirmAsk confirmForApply(Page page, const Draft& d) {
  ConfirmAsk ask;
  if (page == Page::Radio) {
    ask.need = true;
    copyText(ask.verb, sizeof(ask.verb), "Apply");
    copyText(ask.text, sizeof(ask.text),
             "The repeater will drop off the mesh until it restarts with the new radio settings.");
  } else if (page == Page::Repeat && !d.v.repeat) {
    ask.need = true;
    copyText(ask.verb, sizeof(ask.verb), "Apply");
    copyText(ask.text, sizeof(ask.text),
             "The repeater will stop passing floods on, and nodes beyond it will lose that link.");
  }
  return ask;
}

ConfirmAsk confirmRestart() {
  ConfirmAsk ask;
  ask.need = true;
  copyText(ask.verb, sizeof(ask.verb), "Restart");
  copyText(ask.text, sizeof(ask.text),
           "The repeater will restart now and be unreachable for a few seconds.");
  return ask;
}

ConfirmAsk confirmRemoveRegion(const char* name) {
  ConfirmAsk ask;
  ask.need = true;
  copyText(ask.verb, sizeof(ask.verb), "Remove");
  snprintf(ask.text, sizeof(ask.text), "Remove region %s? Floods will no longer repeat there.",
           name != nullptr && name[0] ? name : "*");
  return ask;
}

ConfirmAsk confirmClock(uint32_t fromEpoch, uint32_t toEpoch) {
  ConfirmAsk ask;
  if (toEpoch <= fromEpoch || (toEpoch - fromEpoch) <= kTimeMaxAheadSecs) return ask;
  ask.need = true;
  copyText(ask.verb, sizeof(ask.verb), "Set clock");
  clockJumpText(ask.text, sizeof(ask.text), fromEpoch, toEpoch);
  return ask;
}

bool tappable(Act act) { return act != Act::None; }

}  // namespace rptr
}  // namespace om
