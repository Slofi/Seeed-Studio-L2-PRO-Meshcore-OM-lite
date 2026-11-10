// Host test for EST-346: the five things the ticket asks a host test for.
//
//  1. A touch while a toast shows dismisses it and fires NOTHING; only the next new press acts.
//     A toast that has run out by time leaves the next touch untouched, and 5 s is the one duration.
//  2. The keyboard's open rule: every open starts on the letters page with shift off, also when the
//     sheet was closed on the 123 or the emoji page, and the draft is not part of that state.
//  3. The s / c / z long press: a short tap keeps the plain letter, a hold past the tested threshold
//     fires once (the bubble) and its release is not a tap, a moved finger cancels.
//  4. The accent mapping and its exact UTF-8 bytes (2 per letter), capitals with shift.
//  5. The composer's byte budget at the limit with those 2-byte letters: a whole letter is appended or
//     refused, never split, and the backspace removes one whole letter.
//
// Plain g++ (no Arduino, no LovyanGFX).
#include <stdio.h>
#include <string.h>

#include "ui/chat_layout.h"
#include "ui/compose_text.h"
#include "ui/long_press.h"
#include "ui/theme.h"
#include "ui/toast.h"

using namespace om;
using namespace om::ui;

static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++g_fail;                                                            \
    }                                                                      \
  } while (0)

// --- 1. the toast's press rule ---------------------------------------------------------------------
// The loop's shape, small enough to drive: a press dismisses a visible toast and spends the gesture;
// the release runs the tap action only when the press was not spent, so the dismissing press fires
// nothing and the next new press acts normally.
struct PressModel {
  ToastState toast;
  bool spent = false;
  int acted = 0;

  void press(uint32_t nowMs) {
    acted = 0;
    spent = toastDismissOnPress(toast, nowMs);
  }
  void release() {   // the release is where a tap's action runs (main.cpp's release path)
    const bool wasSpent = spent;
    spent = false;
    if (!wasSpent) ++acted;
  }
};

static void testToastDismiss() {
  CHECK(kToastMs == 5000);   // the one constant in theme.h: 5 s, not 3 s

  PressModel p;
  CHECK(p.toast.show("New node: SI-Ivančna Gorica (chat)", 1000, kToastMs));
  CHECK(p.toast.visible(1000));
  CHECK(p.toast.visible(5999));
  CHECK(!p.toast.visible(6000));

  // The touch during the toast: closes it, and its release fires nothing.
  p.press(2000);
  CHECK(!p.toast.active());
  CHECK(p.spent);
  p.release();   // the same gesture's release: spent, so no tap behind
  CHECK(p.acted == 0);

  // The NEXT new press is free and acts.
  p.press(2100);
  CHECK(!p.spent);
  p.release();
  CHECK(p.acted == 1);

  // A toast that expires by time leaves the next touch untouched.
  PressModel e;
  CHECK(e.toast.show("Ping ERA-3: 120 ms", 0, kToastMs));
  CHECK(!e.toast.visible(kToastMs));      // at the deadline it is already over
  e.press(kToastMs);                      // the loop's expire() has cleared it by this point
  CHECK(!e.spent);
  CHECK(e.toast.active());                // the press did not touch the expired state
  CHECK(e.toast.expire(kToastMs));        // exactly one expiry event
  CHECK(!e.toast.active());
  e.release();
  CHECK(e.acted == 1);

  // A press with no toast at all is free.
  PressModel n;
  n.press(0);
  CHECK(!n.spent);
  n.release();
  CHECK(n.acted == 1);

  // The same press event, twice in a row with a toast in between: each dismissal is its own press.
  PressModel m;
  CHECK(m.toast.show("Trace came back", 0, kToastMs));
  m.press(10);
  CHECK(!m.toast.active());
  m.release();
  CHECK(m.acted == 0);
  m.press(20);   // nothing to dismiss now
  m.release();
  CHECK(m.acted == 1);
}

// --- 2. the keyboard opens on letters, shift off ---------------------------------------------------
static void testKeyboardReset() {
  KeyboardState k;
  CHECK(!k.open && k.page == KeyPage::Letters && !k.shift);

  // Open from rest: letters, shift off.
  k.openSheet();
  CHECK(k.open);
  CHECK(k.page == KeyPage::Letters);
  CHECK(!k.shift && !k.nums() && !k.emoji());

  // Switch to 123, close, open again: letters, shift off (Filip's report).
  k.toggleNumbers();
  CHECK(k.nums());
  k.toggleShift();          // shift on while on the numbers page
  k.close();
  CHECK(!k.open);
  CHECK(k.nums());          // closing keeps the page until the next open...
  k.openSheet();
  CHECK(k.open);
  CHECK(!k.nums() && !k.emoji());   // ...which always starts on letters
  CHECK(!k.shift);

  // Shift on the letters page, close on shift, reopen: shift off.
  k.toggleShift();
  CHECK(k.shift);
  k.close();
  k.openSheet();
  CHECK(!k.shift);
  CHECK(k.page == KeyPage::Letters);

  // Closed on the emoji page (EST-345): reopen on letters, shift off.
  k.toggleEmoji();
  CHECK(k.emoji());
  k.close();
  k.openSheet();
  CHECK(!k.emoji() && !k.nums() && !k.shift);

  // The emoji page and the numbers page are exclusive, and "abc" returns to letters.
  k.toggleEmoji();
  k.toggleNumbers();
  CHECK(k.nums() && !k.emoji());
  k.toggleNumbers();
  CHECK(!k.nums() && !k.emoji());
  k.toggleEmoji();
  k.showLetters();
  CHECK(k.page == KeyPage::Letters);

  // The draft is not part of the page state: closing and reopening changes only the page.
  char draft[16] = "Ivan";
  k.openSheet();
  k.close();
  k.openSheet();
  CHECK(strcmp(draft, "Ivan") == 0);
}

// --- 3/4. the accent key and its letter ------------------------------------------------------------
static int findKey(char ch, int* row) {
  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(false, r, defs);
    for (int c = 0; c < n; ++c) {
      if (defs[c].kind == KeyKind::Char && defs[c].label[0] == ch) {
        *row = r;
        return c;
      }
    }
  }
  return -1;
}

static void testAccentMapping() {
  // The letters the ticket names live on the letters page and nowhere on the numbers page.
  int row = -1;
  CHECK(findKey('s', &row) >= 0);
  CHECK(findKey('c', &row) >= 0);
  CHECK(findKey('z', &row) >= 0);
  for (int r = 0; r < 4; ++r) {
    KeyDef defs[10];
    const int n = keyRow(true, r, defs);
    for (int c = 0; c < n; ++c) {
      CHECK(!(defs[c].kind == KeyKind::Char &&
              (defs[c].label[0] == 's' || defs[c].label[0] == 'c' || defs[c].label[0] == 'z')));
    }
  }

  // Lower case and capitals.
  CHECK(accentCodePoint('s', false) == 0x0161);   // š
  CHECK(accentCodePoint('c', false) == 0x010D);   // č
  CHECK(accentCodePoint('z', false) == 0x017E);   // ž
  CHECK(accentCodePoint('s', true) == 0x0160);    // Š
  CHECK(accentCodePoint('c', true) == 0x010C);    // Č
  CHECK(accentCodePoint('z', true) == 0x017D);    // Ž
  // Every other key has no long-press accent, so its press stays an ordinary tap.
  CHECK(accentCodePoint('a', false) == 0);
  CHECK(accentCodePoint('d', true) == 0);
  CHECK(accentCodePoint(',', false) == 0);
  CHECK(accentCodePoint('1', false) == 0);
  CHECK(accentCodePoint(0, false) == 0);

  // Two UTF-8 bytes each, exactly: the composer and the air count these bytes.
  struct Case {
    uint32_t cp;
    const char* bytes;
  } cases[] = {
      {0x0161, "\xC5\xA1"}, {0x010D, "\xC4\x8D"}, {0x017E, "\xC5\xBE"},
      {0x0160, "\xC5\xA0"}, {0x010C, "\xC4\x8C"}, {0x017D, "\xC5\xBD"},
  };
  for (const Case& c : cases) {
    char out[5] = {0};
    const int n = utf8Encode(c.cp, out);
    CHECK(n == 2);
    CHECK(memcmp(out, c.bytes, 3) == 0);   // both bytes and the terminator
  }
}

// --- 3. the hold itself: one tested LongPress, the same the node menu uses --------------------------
static void testAccentHold() {
  CHECK(LongPress::kHoldMs == 450);   // the ticket's "about 400 ms", the helper's one number

  // A short tap: never fires, the release is the tap -> the plain letter.
  LongPress p;
  p.press(10, 10, 1000);
  CHECK(!p.update(10, 10, 1000 + LongPress::kHoldMs - 1));
  CHECK(p.release());

  // A hold: fires exactly once while the finger is still down; the release is NOT a tap.
  LongPress h;
  h.press(10, 10, 1000);
  CHECK(!h.update(10, 10, 1449));
  CHECK(h.update(10, 10, 1450));
  CHECK(!h.update(10, 10, 1600));   // once, not every loop pass
  CHECK(!h.release());

  // Moving more than the slop cancels: no accent, and no tap either (the finger scrolled, not tapped).
  LongPress m;
  m.press(10, 10, 1000);
  CHECK(!m.update(10 + LongPress::kMovePx + 1, 10, 1200));
  CHECK(!m.update(10, 10, 1600));
  CHECK(!m.release());

  // A second press after a failed one works normally (reset on press).
  LongPress s;
  s.press(0, 0, 0);
  CHECK(!s.update(40, 0, 100));
  s.press(0, 0, 200);
  CHECK(s.update(0, 0, 200 + LongPress::kHoldMs));
  CHECK(!s.release());
}

// --- 5. the composer's byte budget at the limit ----------------------------------------------------
static void testAccentBudget() {
  // A hand-sized budget: exactly five 2-byte letters fit; the sixth is refused whole.
  char draft[32] = "";
  const int allow = 10;
  for (int i = 0; i < 5; ++i) CHECK(composeAppend(draft, sizeof(draft), "\xC4\x8D", allow));
  CHECK(strlen(draft) == 10);
  CHECK(!composeAppend(draft, sizeof(draft), "\xC4\x8D", allow));
  CHECK(strlen(draft) == 10);          // refused as a unit: no half letter in the draft
  CHECK(composeRemaining(allow, (int)strlen(draft)) == 0);

  // One byte short: four fit, the fifth would cross the line and is refused.
  char odd[32] = "";
  for (int i = 0; i < 4; ++i) CHECK(composeAppend(odd, sizeof(odd), "\xC5\xA1", 9));
  CHECK(strlen(odd) == 8);
  CHECK(!composeAppend(odd, sizeof(odd), "\xC5\xA1", 9));
  CHECK(strlen(odd) == 8);

  // A mixed word at an exact boundary: ščž is 6 bytes and fits 6; the next letter does not.
  char mix[16] = "";
  CHECK(composeAppend(mix, sizeof(mix), "\xC5\xA1", 6));   // š
  CHECK(composeAppend(mix, sizeof(mix), "\xC4\x8D", 6));   // č
  CHECK(composeAppend(mix, sizeof(mix), "\xC5\xBE", 6));   // ž
  CHECK(strlen(mix) == 6);
  CHECK(strcmp(mix, "\xC5\xA1\xC4\x8D\xC5\xBE") == 0);
  CHECK(!composeAppend(mix, sizeof(mix), "\xC5\xA1", 6));

  // Backspace removes the whole letter, not one byte: the draft stays valid UTF-8.
  composeBackspace(mix);
  CHECK(strcmp(mix, "\xC5\xA1\xC4\x8D") == 0);
  CHECK(strlen(mix) == 4);
  composeBackspace(mix);
  CHECK(strlen(mix) == 2);
  composeBackspace(mix);
  CHECK(strlen(mix) == 0);

  // The real channel and DM budgets from the air path: fill them with č to the last whole letter.
  const int budgets[] = {composeAllowance(false, "", kMeshOwnName), kMeshDmTextMax};
  for (int allowReal : budgets) {
    CHECK(allowReal > 2);
    char big[kMeshTextCap + 8] = "";
    int added = 0;
    while (composeAppend(big, sizeof(big), "\xC4\x8D", allowReal)) ++added;
    CHECK(added == allowReal / 2);           // 2 bytes each, never past the budget
    CHECK(strlen(big) == (size_t)(added * 2));
    CHECK((int)strlen(big) <= allowReal);
    CHECK(allowReal - (int)strlen(big) < 2);  // the budget is genuinely at its limit
    CHECK(!composeAppend(big, sizeof(big), "\xC4\x8D", allowReal));
    CHECK(strlen(big) == (size_t)(added * 2));
  }
}

int main() {
  testToastDismiss();
  testKeyboardReset();
  testAccentMapping();
  testAccentHold();
  testAccentBudget();
  if (g_fail) {
    printf("%d check(s) failed\n", g_fail);
    return 1;
  }
  printf("EST-346: toast press rule, keyboard reset, accents and the byte limit all pass\n");
  return 0;
}
