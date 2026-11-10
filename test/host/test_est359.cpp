// EST-361 host test: the shared sound library. Everything here runs on the laptop with the system
// g++ and the SAME sound_synth.cpp the firmware compiles, so the numbers Claude listens to in
// docs/sounds/ are the numbers asserted here.
//
// Proves: 54 library sounds, at least six per event (the event's own "home" variants), every name
// unique, no two sounds share a note sequence, the message defaults are the new-node family a fourth
// UP and new node the same family a fourth DOWN, one original TK-TK boot sound, the battery warning
// is 1.0..1.5 s and near the peak cap, every render starts and ends at zero, no variant clips, the
// EST-359 per-event choices migrate to ids, and `play <event> [sound-id]` accepts exactly the library.
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

// The ticket's "same note sequence": same pitches in the same order, including their timing.
static bool sameNoteSequence(uint8_t a, uint8_t b) {
  if (soundNoteCount(a) != soundNoteCount(b)) return false;
  for (int i = 0; i < soundNoteCount(a); ++i) {
    NoteInfo na;
    NoteInfo nb;
    if (!soundNote(a, i, &na) || !soundNote(b, i, &nb)) return false;
    if (na.hz != nb.hz || na.atMs != nb.atMs || na.durMs != nb.durMs) return false;
  }
  return true;
}

static void testTable() {
  CHECK(kEventCount == 9);
  static const char* kKeys[kEventCount] = {"boot", "message", "dm", "mention", "sent",
                                           "error", "newnode", "battery", "touch"};
  for (int i = 0; i < kEventCount; ++i) {
    const Event ev = (Event)i;
    CHECK(strcmp(eventKey(ev), kKeys[i]) == 0);
    CHECK(eventLabel(ev) != nullptr && eventLabel(ev)[0] != 0);
    CHECK(eventTag(ev) != nullptr && eventTag(ev)[0] != 0);
    const int count = variantCount(ev);
    CHECK(count >= 6);   // "more options for all"
    CHECK(validSound(defaultSound(ev)));
    CHECK(soundHomeEvent(defaultSound(ev)) == ev);   // every default is one of its own sounds
    for (int v = 0; v < count; ++v) {
      const uint8_t id = variantSoundId(ev, (uint8_t)v);
      CHECK(validSound(id));
      CHECK(soundHomeEvent(id) == ev);
      CHECK(variantName(ev, (uint8_t)v) != nullptr && variantName(ev, (uint8_t)v)[0] != 0);
      for (int w = 0; w < v; ++w) {
        CHECK(id != variantSoundId(ev, (uint8_t)w));
        CHECK(!sameNoteSequence(id, variantSoundId(ev, (uint8_t)w)));
      }
    }
    CHECK(variantSoundId(ev, (uint8_t)count) == kSoundOff);
    CHECK(variantName(ev, (uint8_t)count) == nullptr);
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
  // The picker's short tags are all different (one tag per event).
  for (int i = 0; i < kEventCount; ++i) {
    for (int j = 0; j < i; ++j) CHECK(strcmp(eventTag((Event)i), eventTag((Event)j)) != 0);
  }
}

static void testLibrary() {
  CHECK(soundCount() >= kEventCount * 6);
  CHECK(kFamilyCount == 6);
  int familyCounts[kFamilyCount] = {};
  for (int id = 0; id < soundCount(); ++id) {
    const uint8_t s = (uint8_t)id;
    CHECK(validSound(s));
    CHECK(soundName(s) != nullptr && soundName(s)[0] != 0);
    CHECK(soundFamily(s) != Family::Count);
    CHECK(familyName(soundFamily(s))[0] != 0);
    CHECK(soundHomeEvent(s) != Event::Count);
    CHECK(soundMs(s) > 0);
    CHECK(soundNoteCount(s) >= 1 && soundNoteCount(s) <= 8);
    ++familyCounts[(int)soundFamily(s)];
    // Every name is unique in the shared picker.
    for (int other = 0; other < id; ++other) {
      CHECK(strcmp(soundName(s), soundName((uint8_t)other)) != 0);
      CHECK(!sameNoteSequence(s, (uint8_t)other));
    }
    for (int i = 0; i < soundNoteCount(s); ++i) {
      NoteInfo n;
      CHECK(soundNote(s, i, &n));
      CHECK(n.hz >= 100 && n.hz <= 5000);
      CHECK(n.durMs > 0);
      CHECK((int)n.atMs + (int)n.durMs <= soundMs(s));
    }
    NoteInfo bad;
    CHECK(!soundNote(s, soundNoteCount(s), &bad));
  }
  for (int f = 0; f < kFamilyCount; ++f) CHECK(familyCounts[f] > 0);
  CHECK(!validSound((uint8_t)soundCount()));
  CHECK(soundName((uint8_t)soundCount()) == nullptr);
  CHECK(soundMs((uint8_t)soundCount()) == 0);
  CHECK(!soundNote(0, -1, nullptr));
}

static void testRender() {
  static int16_t buf[kMaxSamples];
  int seen = 0;
  for (int id = 0; id < soundCount(); ++id) {
    RenderInfo info{};
    const int n = renderSound((uint8_t)id, buf, kMaxSamples, &info);
    const int want = msToSamples(soundMs((uint8_t)id));
    CHECK(n == want);
    CHECK(info.samples == want);
    CHECK(n > 0 && n <= kMaxSamples);
    ++seen;

    // Length ranges: ticks are deliberately short; every battery-home sound is the long warning;
    // everything else is a normal notification phrase.
    const int ms = soundMs((uint8_t)id);
    if (soundFamily((uint8_t)id) == Family::Ticks) CHECK(ms >= 20 && ms <= 130);
    else if (soundHomeEvent((uint8_t)id) == Event::LowBattery) CHECK(ms >= 1000 && ms <= 1500);
    else CHECK(ms >= 100 && ms <= 400);

    // No click at the edges: the first sample is silent or one LSB, the last sample exactly zero.
    CHECK(buf[0] == 0 || buf[0] == 1 || buf[0] == -1);
    CHECK(buf[n - 1] == 0);

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
  CHECK(seen == soundCount());

  // A bad id, a tiny buffer and a null buffer all refuse cleanly.
  CHECK(renderSound((uint8_t)soundCount(), buf, kMaxSamples, nullptr) == 0);
  CHECK(renderSound(0, buf, 10, nullptr) == 0);
  CHECK(renderSound(0, nullptr, kMaxSamples, nullptr) == 0);
  CHECK(renderSound(0, buf, 0, nullptr) == 0);
}

static void testDefaults() {
  // Message = the soft new-node voice a fourth UP; new node = the same voice a fourth DOWN.
  const uint8_t msg = defaultSound(Event::Message);
  const uint8_t node = defaultSound(Event::NewNode);
  CHECK(strcmp(soundName(msg), "Soft Hi") == 0);
  CHECK(strcmp(soundName(node), "Soft Lo") == 0);
  NoteInfo m0;
  NoteInfo n0;
  CHECK(soundNote(msg, 0, &m0));
  CHECK(soundNote(node, 0, &n0));
  CHECK(m0.hz > n0.hz * 1.4);   // clearly apart, not a timbre change
  // Every message-home sound is above every new-node-home sound.
  int msgMin = 99999;
  int nodeMax = 0;
  for (int i = 0; i < variantCount(Event::Message); ++i) {
    NoteInfo n;
    CHECK(soundNote(variantSoundId(Event::Message, (uint8_t)i), 0, &n));
    if (n.hz < msgMin) msgMin = n.hz;
  }
  for (int i = 0; i < variantCount(Event::NewNode); ++i) {
    NoteInfo n;
    CHECK(soundNote(variantSoundId(Event::NewNode, (uint8_t)i), 0, &n));
    if (n.hz > nodeMax) nodeMax = n.hz;
  }
  CHECK(msgMin > nodeMax);

  // Boot still starts on the original TK-TK (D7, gap, F7).
  CHECK(defaultSound(Event::Boot) == 0);
  CHECK(strcmp(soundName(defaultSound(Event::Boot)), "Bell TK-TK") == 0);

  // Low battery: long, loud, close to the cap, still no clipping, and DND-exempt.
  const uint8_t batt = defaultSound(Event::LowBattery);
  CHECK(soundHomeEvent(batt) == Event::LowBattery);
  static int16_t buf[kMaxSamples];
  RenderInfo info{};
  CHECK(renderSound(batt, buf, kMaxSamples, &info) > 0);
  CHECK(soundMs(batt) >= 1000 && soundMs(batt) <= 1500);
  CHECK(info.peak >= 25000 && info.peak <= kPeakTarget);
  bool clipped = false;
  for (int i = 0; i < info.samples; ++i) {
    if (buf[i] == 32767 || buf[i] == -32768) clipped = true;
  }
  CHECK(!clipped);
  CHECK(dndExempt(Event::LowBattery));
}

// The boot phrase keeps Filip's "TK-TK": D7 (2349 Hz), a silent gap, F7 (2794 Hz).
static void testBootTone() {
  static int16_t buf[kMaxSamples];
  RenderInfo info{};
  const int n = renderSound(defaultSound(Event::Boot), buf, kMaxSamples, &info);
  CHECK(n == msToSamples(205));

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
  CHECK(windowRms(buf, 4, 8) > windowRms(buf, 20, 40));
  CHECK(windowRms(buf, 20, 40) > windowRms(buf, 40, 60));

  CHECK(windowRms(buf, 75, 105) == 0.0);
  CHECK(windowRms(buf, 110, 114) == 0.0);
  CHECK(windowRms(buf, 118, 140) > 1000.0);

  CHECK(toneMagnitude(buf, 0, 55, 2349.0) > toneMagnitude(buf, 0, 55, 2794.0) * 3.0);
  CHECK(toneMagnitude(buf, 115, 175, 2794.0) > toneMagnitude(buf, 115, 175, 2349.0) * 3.0);
}

static void testMigration() {
  for (int e = 0; e < kEventCount; ++e) {
    const Event ev = (Event)e;
    for (int old = 0; old < 3; ++old) {
      const uint8_t id = legacyVariantSound(ev, (uint8_t)old);
      CHECK(validSound(id));   // the old choice lands on a real shared-library sound
    }
    CHECK(legacyVariantSound(ev, 3) == kSoundOff);
    CHECK(legacyVariantSound(ev, 17) == kSoundOff);
  }
  // The three old boot variants only differed in timbre: all land on the original TK-TK.
  CHECK(legacyVariantSound(Event::Boot, 0) == legacyVariantSound(Event::Boot, 1));
  CHECK(legacyVariantSound(Event::Boot, 1) == legacyVariantSound(Event::Boot, 2));
  CHECK(strcmp(soundName(legacyVariantSound(Event::Message, 0)), "Chime Up") == 0);
  CHECK(strcmp(soundName(legacyVariantSound(Event::NewNode, 0)), "Soft Lo") == 0);
}

static void testUsbPlay() {
  {
    char line[] = "play message";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr);
    CHECK(c.kind == om::usb::Kind::Play);
    CHECK(c.target != nullptr && strcmp(c.target, "message") == 0);
    CHECK(c.count == (int)defaultSound(Event::Message));
  }
  {
    char line[] = "play dm 9";   // Soft High Fifth, a message-home sound for a DM
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr);
    CHECK(c.kind == om::usb::Kind::Play);
    CHECK(c.count == 9);
  }
  {
    char line[] = "play boot 0";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr);
    CHECK(c.count == 0);
  }
  {
    char line[] = "play boot 54";   // past the library
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);
  }
  {
    char line[] = "play message 9";   // any sound may play as any event
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr && c.count == 9);
  }
  {
    char line[] = "play message 53";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr && c.count == 53);
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
  {
    char line[] = "status";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr && c.kind == om::usb::Kind::Status);
  }
  for (int i = 0; i < kEventCount; ++i) {
    char line[32];
    snprintf(line, sizeof(line), "play %s", eventKey((Event)i));
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr);
    CHECK(c.kind == om::usb::Kind::Play);
    CHECK(c.count == (int)defaultSound((Event)i));
  }
}

int main() {
  testTable();
  testLibrary();
  testRender();
  testDefaults();
  testBootTone();
  testMigration();
  testUsbPlay();
  if (g_fail == 0) {
    printf("est361: all shared sound library checks passed\n");
    return 0;
  }
  printf("est361: %d check(s) failed\n", g_fail);
  return 1;
}
