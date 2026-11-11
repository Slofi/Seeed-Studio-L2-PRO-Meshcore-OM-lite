// EST-369 host test: the shared sound library, organised by shape. Everything runs on the laptop with
// the system g++ and the SAME sound_synth.cpp the firmware compiles, so the numbers Claude listens to
// in docs/sounds/ are the numbers asserted here.
//
// Proves: 41 current sounds, ten per shape and one long Alert (the low-battery warning); every shape's
// note count and length range; the voice starts every name and each shape has several voices; unique
// names and note sequences; the kept seven duos plus TK-TK, Glass Trio and the battery warning; the
// retired EST-361 ids 0..53 still resolve to a current sound and render the same waveform; every
// render starts and ends at zero and never clips; the EST-359 migration; and the `play <event>
// [sound-id]` USB command.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

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

static bool startsWithVoice(const char* name, const char* voice) {
  if (name == nullptr || voice == nullptr) return false;
  const size_t n = strlen(voice);
  return strncasecmp(name, voice, n) == 0;
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
    const uint8_t def = defaultSound(ev);
    CHECK(soundIsCurrent(def));
    CHECK(soundShape(def) != Shape::Count);
    Event parsed = Event::Count;
    CHECK(parseEvent(eventKey(ev), &parsed));
    CHECK(parsed == ev);
  }
  CHECK(!parseEvent("Message", nullptr));
  CHECK(!parseEvent("beep", nullptr));
  CHECK(!parseEvent(nullptr, nullptr));
  // DND lets exactly the two safety sounds through (the shell reads this flag).
  for (int i = 0; i < kEventCount; ++i) {
    const Event ev = (Event)i;
    const bool want = ev == Event::Error || ev == Event::LowBattery;
    CHECK(dndExempt(ev) == want);
  }
  for (int i = 0; i < kEventCount; ++i) {
    for (int j = 0; j < i; ++j) CHECK(strcmp(eventTag((Event)i), eventTag((Event)j)) != 0);
  }
}

static void testLibrary() {
  // The reduced set: 41 current sounds, about ten per style, plus the one long alert.
  CHECK(soundCount() == 95);
  CHECK(libraryCount() == 41);
  CHECK(kShapeCount == 5);
  static const int kWant[kShapeCount] = {10, 10, 10, 10, 1};   // Solo, Duo, Trio, Multi, Alert
  int shapeCount[kShapeCount] = {};
  const char* shapeVoices[kShapeCount][8] = {};
  int shapeVoiceCount[kShapeCount] = {};
  int lastShape = -1;
  for (int i = 0; i < libraryCount(); ++i) {
    const uint8_t id = libraryId(i);
    CHECK(soundIsCurrent(id));
    CHECK(id == (uint8_t)(54 + i));   // current ids are contiguous and start at 54
    CHECK(soundName(id) != nullptr && soundName(id)[0] != 0);
    CHECK(soundVoice(id) != nullptr && soundVoice(id)[0] != 0);
    // "The voice in the name" for the four pickable shapes; the Alert's kept name stays "Battery Low".
    if (soundShape(id) != Shape::Alert) CHECK(startsWithVoice(soundName(id), soundVoice(id)));
    const Shape shape = soundShape(id);
    CHECK(shape != Shape::Count);
    CHECK(shapeName(shape)[0] != 0);
    CHECK((int)shape >= lastShape);   // picker order: shape blocks are contiguous
    lastShape = (int)shape;
    ++shapeCount[(int)shape];
    // Which voices this shape uses (the ticket asks for several per shape).
    bool seen = false;
    for (int v = 0; v < shapeVoiceCount[(int)shape]; ++v) {
      if (strcmp(shapeVoices[(int)shape][v], soundVoice(id)) == 0) seen = true;
    }
    if (!seen && shapeVoiceCount[(int)shape] < 8) {
      shapeVoices[(int)shape][shapeVoiceCount[(int)shape]++] = soundVoice(id);
    }
    CHECK(soundMs(id) > 0);
    CHECK(soundNoteCount(id) >= 1 && soundNoteCount(id) <= 8);
    // Shape first: the note count fixes the style, and the length ranges are the ticket's.
    const int notes = soundNoteCount(id);
    const int ms = soundMs(id);
    switch (shape) {
      case Shape::Solo:
        CHECK(notes == 1);
        CHECK(ms >= 70 && ms <= 260);
        break;
      case Shape::Duo:
        CHECK(notes == 2);
        CHECK(ms >= 140 && ms <= 360);
        break;
      case Shape::Trio:
        CHECK(notes == 3);
        CHECK(ms >= 200 && ms <= 450);
        break;
      case Shape::Multi:
        CHECK(notes >= 4);
        CHECK(ms >= 250 && ms <= 599);
        break;
      case Shape::Alert:
        CHECK(notes >= 4);
        CHECK(ms >= 1200 && ms <= 1500);
        break;
      default:
        CHECK(false);
        break;
    }
    for (int n = 0; n < notes; ++n) {
      NoteInfo note;
      CHECK(soundNote(id, n, &note));
      CHECK(note.hz >= 100 && note.hz <= 5000);
      CHECK(note.durMs > 0);
      CHECK((int)note.atMs + (int)note.durMs <= ms);
    }
    NoteInfo bad;
    CHECK(!soundNote(id, notes, &bad));
    // Every name is unique and no two sounds share a note sequence.
    for (int j = 0; j < i; ++j) {
      const uint8_t other = libraryId(j);
      CHECK(strcmp(soundName(id), soundName(other)) != 0);
      CHECK(!sameNoteSequence(id, other));
    }
  }
  for (int s = 0; s < kShapeCount; ++s) {
    CHECK(shapeCount[s] == kWant[s]);
    if (s != (int)Shape::Alert) CHECK(shapeVoiceCount[s] >= 4);   // several voices per style
  }
  // The current ids are the whole accepted range above the retired ones.
  CHECK(!soundIsCurrent(53));
  CHECK(soundIsCurrent(54));
  CHECK(soundIsCurrent(94));
  CHECK(validSound(0));     // retired, still accepted from NVS
  CHECK(validSound(94));
  CHECK(!validSound(95));
  CHECK(!validSound(kSoundOff));
  CHECK(soundName(95) == nullptr);
  CHECK(soundMs(95) == 0);
  CHECK(soundShape(95) == Shape::Count);
  CHECK(libraryId(-1) == kSoundOff);
  CHECK(libraryId(libraryCount()) == kSoundOff);
  CHECK(resolveSound(kSoundOff) == kSoundOff);
  CHECK(resolveSound(95) == kSoundOff);
}

// The kept sounds, by name and note recipe: Filip's TK-TK boot, the soft voices he likes, the best
// of the old duos, Glass Trio and the long battery warning.
static void testKeptSounds() {
  CHECK(strcmp(soundName(defaultSound(Event::Boot)), "Bell TK-TK") == 0);
  const uint8_t tk = defaultSound(Event::Boot);
  CHECK(soundShape(tk) == Shape::Duo);
  CHECK(soundMs(tk) == 205);
  NoteInfo n0;
  NoteInfo n1;
  CHECK(soundNote(tk, 0, &n0));
  CHECK(soundNote(tk, 1, &n1));
  CHECK(n0.hz == 2349 && n0.atMs == 0 && n0.durMs == 60);
  CHECK(n1.hz == 2794 && n1.atMs == 115 && n1.durMs == 70);

  CHECK(strcmp(soundName(defaultSound(Event::Message)), "Soft Hi") == 0);
  CHECK(strcmp(soundName(defaultSound(Event::NewNode)), "Soft Lo") == 0);
  CHECK(strcmp(soundName(defaultSound(Event::Dm)), "Wood Warm") == 0);
  CHECK(strcmp(soundName(defaultSound(Event::Mention)), "Chime Bright") == 0);
  CHECK(strcmp(soundName(defaultSound(Event::Sent)), "Soft Tick") == 0);
  CHECK(strcmp(soundName(defaultSound(Event::Error)), "Bell Fall") == 0);
  // EST-390: the touch default is the quietest short sound (80 ms soft tick), not the 110 ms beep.
  CHECK(strcmp(soundName(defaultSound(Event::Touch)), "Soft Tick") == 0);

  // Message still sits clearly above new node (a fourth/fifth apart, not a timbre change).
  NoteInfo m0;
  NoteInfo n0b;
  CHECK(soundNote(defaultSound(Event::Message), 0, &m0));
  CHECK(soundNote(defaultSound(Event::NewNode), 0, &n0b));
  CHECK(m0.hz > n0b.hz * 1.4);

  // Seven kept duos plus TK-TK are in the Duo shape, Glass Trio in Trio, the warning in Alert.
  int kept = 0;
  static const char* kKept[] = {"Bell TK-TK", "Soft Hi", "Marimba Hi", "Bell Hi",
                                "Wood Warm",  "Chime Bright", "Soft Lo"};
  for (int i = 0; i < libraryCount(); ++i) {
    const uint8_t id = libraryId(i);
    for (const char* name : kKept) {
      if (strcmp(soundName(id), name) == 0) {
        ++kept;
        CHECK(soundShape(id) == Shape::Duo);
      }
    }
  }
  CHECK(kept == 7);

  const uint8_t batt = defaultSound(Event::LowBattery);
  CHECK(soundShape(batt) == Shape::Alert);
  CHECK(strcmp(soundName(batt), "Battery Low") == 0);
  static int16_t buf[kMaxSamples];
  RenderInfo info{};
  CHECK(renderSound(batt, buf, kMaxSamples, &info) > 0);
  CHECK(soundMs(batt) >= 1200 && soundMs(batt) <= 1500);
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
      if (v > m) {
        m = v;
        at = i;
      }
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

static void testRender() {
  static int16_t buf[kMaxSamples];
  for (int i = 0; i < libraryCount(); ++i) {
    const uint8_t id = libraryId(i);
    RenderInfo info{};
    const int n = renderSound(id, buf, kMaxSamples, &info);
    const int want = msToSamples(soundMs(id));
    CHECK(n == want);
    CHECK(info.samples == want);
    CHECK(n > 0 && n <= kMaxSamples);

    // Edges: the first sample is silent or one LSB, the last sample exactly zero.
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
    CHECK(peak > 1000);   // every sound is actually audible
  }

  // A bad id, a tiny buffer and a null buffer all refuse cleanly.
  CHECK(renderSound((uint8_t)soundCount(), buf, kMaxSamples, nullptr) == 0);
  CHECK(renderSound(94, buf, 10, nullptr) == 0);
  CHECK(renderSound(94, nullptr, kMaxSamples, nullptr) == 0);
  CHECK(renderSound(94, buf, 0, nullptr) == 0);
}

// Every retired EST-361 id still names and renders the kept sound it now means.
static void testRetiredIds() {
  static int16_t a[kMaxSamples];
  static int16_t b[kMaxSamples];
  for (int old = 0; old < 54; ++old) {
    const uint8_t id = (uint8_t)old;
    CHECK(validSound(id));
    const uint8_t kept = resolveSound(id);
    CHECK(soundIsCurrent(kept));
    CHECK(strcmp(soundName(id), soundName(kept)) == 0);
    CHECK(soundShape(id) == soundShape(kept));
    CHECK(soundMs(id) == soundMs(kept));
    RenderInfo ia{};
    RenderInfo ib{};
    const int na = renderSound(id, a, kMaxSamples, &ia);
    const int nb = renderSound(kept, b, kMaxSamples, &ib);
    CHECK(na > 0 && na == nb);
    CHECK(ia.peak == ib.peak);
    CHECK(memcmp(a, b, (size_t)na * sizeof(int16_t)) == 0);
  }
  // A few of the ticketed mappings, by name.
  CHECK(strcmp(soundName(0), "Bell TK-TK") == 0);
  CHECK(strcmp(soundName(2), "Chime Bright") == 0);
  CHECK(strcmp(soundName(12), "Wood Warm") == 0);
  CHECK(strcmp(soundName(23), "Glass Trio") == 0);
  CHECK(strcmp(soundName(32), "Wood Alert") == 0);
  CHECK(strcmp(soundName(42), "Battery Low") == 0);
  CHECK(strcmp(soundName(53), "Beep Clean") == 0);
}

static void testMigration() {
  for (int e = 0; e < kEventCount; ++e) {
    const Event ev = (Event)e;
    for (int old = 0; old < 3; ++old) {
      const uint8_t id = legacyVariantSound(ev, (uint8_t)old);
      CHECK(soundIsCurrent(id));   // the old choice lands on a current sound
    }
    CHECK(legacyVariantSound(ev, 3) == kSoundOff);
    CHECK(legacyVariantSound(ev, 17) == kSoundOff);
  }
  // The three old boot variants only differed in timbre: all land on the original TK-TK.
  CHECK(legacyVariantSound(Event::Boot, 0) == defaultSound(Event::Boot));
  CHECK(legacyVariantSound(Event::Boot, 1) == defaultSound(Event::Boot));
  CHECK(legacyVariantSound(Event::Boot, 2) == defaultSound(Event::Boot));
  CHECK(strcmp(soundName(legacyVariantSound(Event::Message, 0)), "Chime Bright") == 0);
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
    char line[] = "play dm 94";   // the battery warning as a DM, any sound for any event
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr);
    CHECK(c.kind == om::usb::Kind::Play);
    CHECK(c.count == 94);
  }
  {
    char line[] = "play boot 0";   // a retired EST-361 id is still accepted
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error == nullptr);
    CHECK(c.count == 0);
  }
  {
    char line[] = "play boot 95";   // past the library (ids are 0..94)
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);
  }
  {
    char line[] = "play boot 300";   // an id that would wrap to a valid byte is refused
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);
  }
  {
    char line[] = "play boot -1";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error != nullptr);   // refused (the token is not a sound id)
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
    char line[] = "play message 54 extra";
    const om::usb::Command c = om::usb::parseCommand(line);
    CHECK(c.error != nullptr && strcmp(c.error, "args") == 0);
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
  testKeptSounds();
  testBootTone();
  testRender();
  testRetiredIds();
  testMigration();
  testUsbPlay();
  if (g_fail == 0) {
    printf("est369: all shape library checks passed\n");
    return 0;
  }
  printf("est369: %d check(s) failed\n", g_fail);
  return 1;
}
