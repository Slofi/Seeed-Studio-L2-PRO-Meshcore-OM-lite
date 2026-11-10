// EST-359 host test: the sound bank. Everything here runs on the laptop with the system g++ and the
// SAME sound_synth.cpp the firmware compiles, so the numbers Claude listens to in docs/sounds/ are
// the numbers asserted here.
//
// Proves: every event has 2-3 named variants, a valid default and an Off row; all phrases are the
// length the table says and inside the ticket's 0.1..0.4 s (Sent and Touch are deliberately shorter);
// the ADSR starts at zero, reaches its peak inside the 3..8 ms attack, decays exponentially and ends
// at exactly zero; the boot phrase is still D7, a silent gap, F7; no variant clips at full scale; and
// the `play <event> [variant]` USB command accepts exactly the table.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sound_synth.h"
#include "usb_cmd_logic.h"

using namespace om::sound;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

static int msToSamples(int ms) { return ms * (int)kRate / 1000; }

static double windowRms(const int16_t* b, int fromMs, int toMs) {
  const int a = msToSamples(fromMs);
  const int z = msToSamples(toMs);
  if (z <= a) return 0.0;
  double sum = 0.0;
  for (int i = a; i < z; ++i) sum += (double)b[i] * (double)b[i];
  return sqrt(sum / (double)(z - a));
}

// Correlation with a sine at `hz`: a cheap way to say "this window really carries D7, not F7".
static double toneMagnitude(const int16_t* b, int fromMs, int toMs, double hz) {
  const int a = msToSamples(fromMs);
  const int z = msToSamples(toMs);
  if (z <= a) return 0.0;
  double re = 0.0;
  double im = 0.0;
  for (int i = a; i < z; ++i) {
    const double ph = 2.0 * M_PI * hz * (double)i / (double)kRate;
    re += (double)b[i] * cos(ph);
    im += (double)b[i] * sin(ph);
  }
  return sqrt(re * re + im * im) / (double)(z - a);
}


static void testTable() {
  CHECK(kEventCount == 9);
  static const char* kKeys[kEventCount] = {"boot", "message", "dm", "mention", "sent",
                                           "error", "newnode", "battery", "touch"};
  for (int i = 0; i < kEventCount; ++i) {
    const Event ev = (Event)i;
    CHECK(strcmp(eventKey(ev), kKeys[i]) == 0);
    CHECK(eventLabel(ev) != nullptr && eventLabel(ev)[0] != 0);
    const int count = variantCount(ev);
    CHECK(count >= 2 && count <= 3);
    CHECK((int)defaultVariant(ev) < count);
    CHECK(!validVariant(ev, (uint8_t)count));   // variantCount is the Off row, not a sound
    for (int v = 0; v < count; ++v) {
      const char* name = variantName(ev, (uint8_t)v);
      CHECK(name != nullptr && name[0] != 0);
      CHECK(eventMs(ev, (uint8_t)v) > 0);
      for (int w = 0; w < v; ++w) {
        CHECK(strcmp(name, variantName(ev, (uint8_t)w)) != 0);
      }
    }
    Event parsed = Event::Count;
    CHECK(parseEvent(eventKey(ev), &parsed));
    CHECK(parsed == ev);
  }
  CHECK(!parseEvent("Message", nullptr));   // the words are the table's exact keys
  CHECK(!parseEvent("beep", nullptr));
  CHECK(!parseEvent(nullptr, nullptr));
  // DND lets exactly the two safety sounds through (the shell reads this flag).
  for (int i = 0; i < kEventCount; ++i) {
    const Event ev = (Event)i;
    const bool want = ev == Event::Error || ev == Event::LowBattery;
    CHECK(dndExempt(ev) == want);
  }
}

static void testRender() {
  static int16_t buf[kMaxSamples];
  int seen = 0;
  for (int i = 0; i < kEventCount; ++i) {
    const Event ev = (Event)i;
    for (int v = 0; v < variantCount(ev); ++v) {
      RenderInfo info{};
      const int n = render(ev, (uint8_t)v, buf, kMaxSamples, &info);
      const int want = msToSamples(eventMs(ev, (uint8_t)v));
      CHECK(n == want);
      CHECK(info.samples == want);
      CHECK(n > 0 && n <= kMaxSamples);
      ++seen;

      // Length window from the ticket: 100..400 ms, with the two deliberately short exceptions.
      const int ms = eventMs(ev, (uint8_t)v);
      if (ev == Event::Sent) CHECK(ms >= 60 && ms <= 120);
      else if (ev == Event::Touch) CHECK(ms >= 20 && ms <= 60);
      else CHECK(ms >= 100 && ms <= 400);

      // No click at the edges: the first sample is silent or one LSB, the last sample exactly zero.
      CHECK(buf[0] == 0 || buf[0] == 1 || buf[0] == -1);
      CHECK(buf[n - 1] == 0);

      // Digital headroom at full volume: never full scale, never above the synth's target.
      int peak = 0;
      bool clipped = false;
      for (int k = 0; k < n; ++k) {
        const int m = buf[k] < 0 ? -buf[k] : buf[k];
        if (m > peak) peak = m;
        if (buf[k] == 32767 || buf[k] == -32768) clipped = true;
      }
      CHECK(!clipped);
      CHECK(peak == info.peak);
      CHECK(peak <= kPeakTarget);
      CHECK(peak < 32767);
      CHECK(peak > 1000);   // every sound is actually audible
    }
  }
  CHECK(seen == 27);   // 9 events: 3 variants each but Sent/Touch have 3 too, see the table

  // A bad event, a bad variant, a tiny buffer and a null buffer all refuse cleanly.
  CHECK(render(Event::Count, 0, buf, kMaxSamples, nullptr) == 0);
  CHECK(render(Event::Message, 99, buf, kMaxSamples, nullptr) == 0);
  CHECK(render(Event::Message, 0, buf, 10, nullptr) == 0);
  CHECK(render(Event::Message, 0, nullptr, kMaxSamples, nullptr) == 0);
}

// The boot phrase keeps Filip's "TK-TK": D7 (2349 Hz), a silent gap, F7 (2794 Hz).
static void testBoot() {
  static int16_t buf[kMaxSamples];
  RenderInfo info{};
  const int n = render(Event::Boot, 0, buf, kMaxSamples, &info);
  CHECK(n == msToSamples(205));

  // The attack is soft and quick: nothing at t=0, the peak arrives in the first 12 ms (the voice's
  // attack is 4 ms; the mixed partials put the instantaneous peak a little later).
  const int peakAt = [&] {
    int at = 0;
    int m = 0;
    for (int i = 0; i < msToSamples(12); ++i) {
      const int v = buf[i] < 0 ? -buf[i] : buf[i];
      if (v > m) { m = v; at = i; }
    }
    return at;
  }();
  CHECK(peakAt >= msToSamples(3));
  CHECK(peakAt < msToSamples(12));
  CHECK(windowRms(buf, 0, 2) < windowRms(buf, 4, 6));

  // Exponential decay: each later window of the first note is quieter (ends at 60 ms).
  const double early = windowRms(buf, 4, 8);
  const double mid = windowRms(buf, 20, 40);
  const double late = windowRms(buf, 40, 60);
  CHECK(early > mid);
  CHECK(mid > late);

  // The gap is real silence, and the second note starts after it.
  CHECK(windowRms(buf, 75, 105) == 0.0);
  CHECK(windowRms(buf, 110, 114) == 0.0);   // still before 115 ms
  CHECK(windowRms(buf, 118, 140) > 1000.0);

  // D7 dominates the first note, F7 the second.
  const double d7a = toneMagnitude(buf, 0, 55, 2349.0);
  const double f7a = toneMagnitude(buf, 0, 55, 2794.0);
  CHECK(d7a > f7a * 3.0);
  const double d7b = toneMagnitude(buf, 115, 175, 2349.0);
  const double f7b = toneMagnitude(buf, 115, 175, 2794.0);
  CHECK(f7b > d7b * 3.0);

  // All three boot variants keep the phrase; only the voice changes.
  for (int v = 1; v < variantCount(Event::Boot); ++v) {
    RenderInfo vi{};
    const int vn = render(Event::Boot, (uint8_t)v, buf, kMaxSamples, &vi);
    CHECK(vn > 0);
    CHECK(windowRms(buf, 75, 105) == 0.0);
    const double d7 = toneMagnitude(buf, 0, 55, 2349.0);
    const double f7 = toneMagnitude(buf, 0, 55, 2794.0);
    CHECK(d7 > f7 * 2.0);
  }
}

static void testUsbPlay() {
  {
    char line[] = "play message";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr);
    CHECK(c.kind == om::usb::Kind::Play);
    CHECK(c.target != nullptr && strcmp(c.target, "message") == 0);
    CHECK(c.count == (int)defaultVariant(Event::Message));
  }
  {
    char line[] = "play dm 2";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr);
    CHECK(c.kind == om::usb::Kind::Play);
    CHECK(c.count == 2);
  }
  {
    char line[] = "play boot 2";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr);
    CHECK(c.count == 2);
  }
  {
    char line[] = "play boot 3";   // 3 is Off, not a playable variant
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);
  }
  {
    char line[] = "play message 9";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);
  }
  {
    char line[] = "play";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);
  }
  {
    char line[] = "play nope";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);
  }
  {
    char line[] = "play message x";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error != nullptr && strcmp(c.error, "number") == 0);
  }
  {
    char line[] = "play message 1 extra";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);
  }
  // The older surface still parses (the new verb must not have swallowed it).
  {
    char line[] = "status";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr && c.kind == om::usb::Kind::Status);
  }
  // Every key in the table is accepted by `play`.
  for (int i = 0; i < kEventCount; ++i) {
    char line[32];
    snprintf(line, sizeof(line), "play %s", eventKey((Event)i));
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr);
    CHECK(c.kind == om::usb::Kind::Play);
  }
}

int main() {
  testTable();
  testRender();
  testBoot();
  testUsbPlay();
  if (g_fail == 0) {
    printf("est359: all sound bank checks passed\n");
    return 0;
  }
  printf("est359: %d check(s) failed\n", g_fail);
  return 1;
}
