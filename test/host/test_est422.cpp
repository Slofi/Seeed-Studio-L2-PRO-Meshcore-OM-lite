// EST-422 host test: the flick that became a tap, and the one confirm sheet for the settings that
// matter.
//
// Part 1, the tap rule (ui/tap_rule.h). Every `TapTrace` sequence below is what the loop feeds it: the
// press edge, one sample per pass while the finger is down, then the release point. The OLD rule is
// called beside the new one, so each flick case proves the ticket's "must fail on the current rule".
//   * a still press for any duration is a tap (the long press is `acted`, and is not);
//   * a press + release 40 px away with NO move sample in between is NOT a tap;
//   * 11 px is still a tap, 13 px is not (the long-press rule's own 12 px slop);
//   * a gesture the loop saw only once (the flick that began and ended between two samples) is not a
//     tap either, which is what the ~2 ms sampling while a finger is down is for;
//   * a sideways flick is not a tap (the old drag latch only looked at Y);
//   * a drag and a fired hold are never taps.
//
// Part 2, the confirm list and the sheet (ui/settings_actions.h):
//   * every listed setting answers a real ConfirmKind from the table, or names it at its commit point
//     (the node name, the position mode), and every small switch answers None;
//   * begin() asks "Change X to Y?" with the value quoted and does NOT change anything;
//   * Cancel and a tap outside answer None and never run the change;
//   * Apply runs it once, with the kind, the payload and the value the question showed;
//   * the keys are the ones the drawing uses: the modal layout's own rectangles answer the hit test.
#include <stdio.h>
#include <string.h>

#include "ui/settings_actions.h"
#include "ui/settings_layout.h"
#include "ui/tap_rule.h"

using namespace om::ui;

static int g_fail = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// --- 1. the tap rule -------------------------------------------------------------------------------
static void testStillPressIsATap() {
  // A real tap: press at (100, 100), seen again two passes later, released on the same pixel.
  TapTrace g;
  g.press(100, 100);
  g.sample(100, 101);
  g.sample(100, 100);
  CHECK(g.tapAllowed(100, 100, false, false));
  // Any duration is a tap as long as the finger stayed put: the rule has no clock at all.
  g.sample(101, 100);
  CHECK(g.tapAllowed(101, 100, false, false));
}

static void testFlickWithNoMoveSampleIsNotATap() {
  // The ticket's case: press, then the release edge 40 px away, no move sample in between.
  TapTrace g;
  g.press(100, 100);
  g.sample(100, 140);
  CHECK(!g.tapAllowed(100, 140, false, false));
  // Armed against the rule this replaced: it tapped the press point here, which is the bug.
  CHECK(tapAllowedLegacy(false, false));
}

static void testSlop() {
  TapTrace g;
  g.press(100, 100);
  g.sample(111, 100);
  CHECK(g.tapAllowed(111, 100, false, false));            // 11 px: still a tap
  TapTrace h;
  h.press(100, 100);
  h.sample(100, 113);
  CHECK(!h.tapAllowed(100, 113, false, false));           // 13 px: not a tap
  // Exactly 12 px is the boundary and is still a tap (the long-press rule's kMovePx is exclusive).
  TapTrace k;
  k.press(0, 0);
  k.sample(12, 0);
  CHECK(k.tapAllowed(12, 0, false, false));
}

static void testSeenOnceIsNotATap() {
  // The flick that began and ended between two samples: the loop saw the finger exactly once, so a tap
  // cannot be proven. The old rule tapped here.
  TapTrace g;
  g.press(100, 100);
  CHECK(g.samples == 1);
  CHECK(!g.tapAllowed(100, 100, false, false));
  CHECK(tapAllowedLegacy(false, false));
  // One more sample anywhere makes a still finger a tap again.
  g.sample(100, 100);
  CHECK(g.tapAllowed(100, 100, false, false));
}

static void testLongPressAndDrag() {
  TapTrace g;
  g.press(100, 100);
  g.sample(100, 100);
  // A hold that already fired (the menu opened under the finger) is not a tap.
  CHECK(!g.tapAllowed(100, 100, false, true));
  CHECK(!tapAllowedLegacy(false, true));
  // A scroll is not a tap, new rule and old rule alike.
  CHECK(!g.tapAllowed(100, 100, true, false));
  CHECK(!tapAllowedLegacy(true, false));
}

static void testSidewaysFlick() {
  // 40 px sideways with the Y unchanged: the old drag latch (dy only) never fired, so this tapped.
  TapTrace g;
  g.press(100, 100);
  g.sample(140, 100);
  CHECK(!g.tapAllowed(140, 100, false, false));
  CHECK(tapAllowedLegacy(false, false));
  // And the press point itself is enough when the release is the only far sample.
  TapTrace h;
  h.press(20, 20);
  h.sample(20, 20);
  CHECK(!h.tapAllowed(60, 20, false, false));
}

// --- 2. the confirm list ---------------------------------------------------------------------------
// The important changes and the action id that asks for them (0 = the change names its kind at its
// commit point: the name editor's OK, the position choice, the map picker's Save).
struct AskCase {
  ConfirmKind kind;
  const char* what;
  uint16_t id;
};
static const AskCase kImportant[] = {
    {ConfirmKind::Preset, "radio region / preset", A_PresetBase + 1},
    {ConfirmKind::Preset, "the custom preset", A_CustomApply},
    {ConfirmKind::TxPower, "TX power", A_TxPower},
    {ConfirmKind::PathHash, "path hash", A_HashBase + 1},
    {ConfirmKind::NodeName, "node name", 0},
    {ConfirmKind::ChannelDelete, "delete a channel (Chat page)", A_ChatDelete},
    {ConfirmKind::ChannelDelete, "delete a channel (Channel page)", A_ChanDelete},
    {ConfirmKind::WifiForget, "forget a Wi-Fi network", A_WifiForgetBase + 2},
    {ConfirmKind::RepeaterForget, "forget a repeater", A_RptrForget},
    {ConfirmKind::WifiSwitch, "the Wi-Fi switch", A_WifiToggle},
    {ConfirmKind::BtSwitch, "the Bluetooth switch", A_BtToggle},
    {ConfirmKind::KeyboardSwitch, "the keyboard switch", A_GroveKeyboard},
    {ConfirmKind::Position, "the Position mode", 0},
    {ConfirmKind::ClearMessages, "clear a channel's messages", A_ChanClear},
};

// The small switches: they change at the touch, exactly as before this ticket.
static const uint16_t kAtOnce[] = {
    A_SoundMaster, A_SoundVolume, A_Brightness, A_IdleAutoOff, A_SoundTouch, A_SoundDnd,
    A_SoundWelcome, A_AdvertPos, A_RptrAuto, A_ChatEnabled, A_ChatSound, A_ChanEnabled,
    A_ChanSound, A_IdleTimeBase + 1, A_Preset /* a page */, A_Hash /* a page */,
};

static void testTheList() {
  for (const AskCase& c : kImportant) {
    if (c.id == 0) {
      // Named at its commit point (documented in ui/settings_actions.h); it must still be a real kind.
      CHECK(c.kind != ConfirmKind::None);
      continue;
    }
    CHECK(confirmKindForAction(c.id) == c.kind);
    CHECK(!settingChangesAtOnce(c.id));
  }
  for (uint16_t id : kAtOnce) {
    CHECK(settingChangesAtOnce(id));
    CHECK(confirmKindForAction(id) == ConfirmKind::None);
  }
}

// A fake store: the test's own value, changed only by the apply the sheet's answer calls.
struct Fake {
  int applies = 0;
  ConfirmKind kind = ConfirmKind::None;
  int arg = -1;
  char value[ConfirmAsk::kValueCap] = "";
  int valueNow = 0;
};

static void fakeApply(void* ctx, ConfirmKind kind, int arg, const char* value) {
  Fake* f = (Fake*)ctx;
  ++f->applies;
  f->kind = kind;
  f->arg = arg;
  snprintf(f->value, sizeof(f->value), "%s", value != nullptr ? value : "");
  f->valueNow = arg;   // the "setting" the change would write
}

static bool contains(const char* hay, const char* needle) {
  return hay != nullptr && needle != nullptr && strstr(hay, needle) != nullptr;
}

static void testTheSheet() {
  Fake f;
  ConfirmAsk ask;
  CHECK(!ask.open);

  // Every listed change asks its own question, and asks it with the value it would write.
  for (const AskCase& c : kImportant) {
    ConfirmAsk a;
    CHECK(a.begin(c.kind, 7, "Example"));
    CHECK(a.open);
    CHECK(a.kind == c.kind);
    CHECK(a.arg == 7);
    CHECK(a.text[0] != 0);
    CHECK(contains(a.text, "Example"));   // the value (or the name) is quoted in full
    // It asks (a question mark somewhere), ends on a real character, and carries no em dash (rule 7).
    const size_t n = strlen(a.text);
    CHECK(strchr(a.text, '?') != nullptr);
    CHECK(n > 0 && a.text[n - 1] != ' ');
    CHECK(strstr(a.text, "\xe2\x80\x94") == nullptr);
  }

  // The ticket's sentence: "Change X to Y?".
  CHECK(ask.begin(ConfirmKind::TxPower, 21, "21 dBm"));
  CHECK(strcmp(ask.text, "Change TX power to 21 dBm?") == 0);
  CHECK(strcmp(confirmApplyLabel(ask.kind), "Apply") == 0);
  CHECK(!confirmIsDangerous(ask.kind));
  CHECK(strcmp(confirmApplyLabel(ConfirmKind::ChannelDelete), "Delete") == 0);
  CHECK(confirmIsDangerous(ConfirmKind::ChannelDelete));
  CHECK(strcmp(confirmApplyLabel(ConfirmKind::WifiForget), "Forget") == 0);
  CHECK(confirmIsDangerous(ConfirmKind::WifiForget));

  // Cancel: no change, and the sheet closes.
  Fake cancelCtx;
  cancelCtx.valueNow = 5;
  CHECK(ask.begin(ConfirmKind::Preset, 2, "New Zealand"));
  CHECK(!confirmTap(ask, SettingsModalHit::Cancel, fakeApply, &cancelCtx));
  CHECK(cancelCtx.applies == 0);
  CHECK(cancelCtx.valueNow == 5);
  CHECK(!ask.open);

  // A tap outside the box is the default side (Cancel) and is consumed the same way.
  CHECK(ask.begin(ConfirmKind::Preset, 2, "New Zealand"));
  CHECK(!confirmTap(ask, SettingsModalHit::Outside, fakeApply, &cancelCtx));
  CHECK(cancelCtx.applies == 0);
  CHECK(!ask.open);

  // Apply: the change runs exactly once, with the kind, the payload and the quoted value.
  f.valueNow = 5;
  CHECK(ask.begin(ConfirmKind::Preset, 2, "New Zealand"));
  CHECK(confirmTap(ask, SettingsModalHit::Confirm, fakeApply, &f));
  CHECK(f.applies == 1);
  CHECK(f.kind == ConfirmKind::Preset);
  CHECK(f.arg == 2);
  CHECK(strcmp(f.value, "New Zealand") == 0);
  CHECK(f.valueNow == 2);          // the value really moved, on Apply only
  CHECK(!ask.open);

  // A second tap after it closed changes nothing more (one sheet, one apply).
  CHECK(!confirmTap(ask, SettingsModalHit::Confirm, fakeApply, &f));
  CHECK(f.applies == 1);
}

static void testTheKeysAreTheDrawnOnes() {
  // The buttons the drawing uses are the buttons the hit test answers: the same SettingsModalLayout.
  const SettingsModalLayout m = settingsModalLayoutFor(320, 240);
  CHECK(settingsModalHitAt(m, m.cancel.x + 2, m.cancel.y + 2) == SettingsModalHit::Cancel);
  CHECK(settingsModalHitAt(m, m.confirm.x + 2, m.confirm.y + 2) == SettingsModalHit::Confirm);
  CHECK(settingsModalHitAt(m, m.confirm.right() - 2, m.confirm.bottom() - 2) ==
        SettingsModalHit::Confirm);
  // The midpoint of each key is the key, and the two keys never overlap.
  CHECK(settingsModalHitAt(m, m.cancel.x + m.cancel.w / 2, m.cancel.y + m.cancel.h / 2) ==
        SettingsModalHit::Cancel);
  CHECK(m.cancel.right() <= m.confirm.x);   // the two keys are disjoint
  // Above the box is outside, which is Cancel: a tap there never applies.
  CHECK(settingsModalHitAt(m, 160, m.box.y - 4) == SettingsModalHit::Outside);
  // The box itself stays inside the 320x240 screen (the panel this ticket is about).
  CHECK(m.box.x >= 0 && m.box.y >= 0 && m.box.right() <= 320 && m.box.bottom() <= 240);
}

// S498 keyboard fix: a light tap on a key is seen once and must still type; the same one-sample
// gesture in a list stays a flick (EST-422 stays fixed). Armed: fails with fixedTarget ignored.
static void testOneSampleOnAKeyTypes() {
  TapTrace key;
  key.press(150, 200);
  key.markFixedTarget();
  CHECK(key.tapAllowed(150, 200, false, false));      // seen once, on a key: a key press
  CHECK(!key.tapAllowed(150, 200, true, false));      // a drag is still never a tap
  CHECK(!key.tapAllowed(150, 200, false, true));      // nor a hold that already acted
  key.sample(150, 230);
  CHECK(!key.tapAllowed(150, 230, false, false));     // slid 30 px off the key: not a tap
  TapTrace list;
  list.press(150, 120);                               // the same gesture in a scrolling list
  CHECK(!list.tapAllowed(150, 120, false, false));    // stays a flick, not a tap
  TapTrace next;                                      // press() forgets the previous key
  next.press(10, 10);
  key.press(10, 10);
  CHECK(!key.tapAllowed(10, 10, false, false));
  CHECK(!next.tapAllowed(10, 10, false, false));
}

int main() {
  testOneSampleOnAKeyTypes();
  testStillPressIsATap();
  testFlickWithNoMoveSampleIsNotATap();
  testSlop();
  testSeenOnceIsNotATap();
  testLongPressAndDrag();
  testSidewaysFlick();
  testTheList();
  testTheSheet();
  testTheKeysAreTheDrawnOnes();
  if (g_fail != 0) {
    printf("test_est422: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("test_est422: all checks passed\n");
  return 0;
}
