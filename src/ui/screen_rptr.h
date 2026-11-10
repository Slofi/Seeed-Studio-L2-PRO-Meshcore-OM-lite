// SPDX-License-Identifier: MIT
// RPTR screen (EST-351, rebuilt EST-358): the mock's three stages, all of them real work now.
//   list   Scan sends the companion's own discovery request and every answer is a row
//   login  the password on the shared keyboard sheet, upstream's login, the result and its round trip
//   admin  the mock's six management views, each control sending its CLI line through MeshCore's CLI
//          path, with the repeater's answer under it
// The shell owns the state and the actions; this file owns the pixels and the rectangles.
#pragma once

#include <LovyanGFX.hpp>

#include "rptr_model.h"
#include "ui/chat_layout.h"
#include "ui/layout.h"
#include "ui/rptr_layout.h"
#include "ui/widgets.h"

namespace om {
namespace ui {

constexpr int kRptrMaxItems = 16;    // the discovery list's own cap
constexpr int kRptrInfoLines = 4;    // the login page's facts card
constexpr int kRptrAdminRows = rptr::kAdminRowCap;

enum class RptrStage : uint8_t { List = 0, Login, Admin };

struct RptrItem {
  char name[32] = "";
  char meta[64] = "";      // "SNR 6.2/7.5 dB · 0 hops · 14:02"
  bool inContacts = false;
};

struct RptrView {
  StatusModel status;
  RptrStage stage = RptrStage::List;

  // --- the scan page
  int count = 0;
  RptrItem items[kRptrMaxItems];
  int scroll = 0;
  bool scanning = false;
  bool regionsBusy = false;  // the anonymous nearby-regions read is running
  char state[40] = "";     // "listening, 2 found" / "3 found" / "none answered"

  // --- the login page
  char name[32] = "";
  char sub[80] = "";       // "repeater · 0 hops · heard 14:02"
  char info[kRptrInfoLines][72] = {};
  int infoCount = 0;
  char pwText[64] = "";      // never drawn as text on the password field, only as dots
  int pwLen = 0;
  char result[64] = "";
  bool resultOk = false;
  bool canForget = false;    // a remembered repeater: Forget is offered
  char note[96] = "";
  bool loggingIn = false;

  // --- the keyboard sheet (the login password and the identity editors)
  bool osk = false;
  bool shift = false;
  bool nums = false;
  uint8_t field = 0;         // KeyboardField as a byte
  char editTitle[32] = "";   // "(lat, lon)" placeholder, and the sheet's hint
  const char* okLabel = "Login";
  uint8_t accentRow = 255;   // 255 = no accent bubble (this sheet wants none)
  uint8_t accentCol = 255;
  uint32_t accentCp = 0;

  // --- the admin page
  int view = 0;
  rptr::AdminRow rows[kRptrAdminRows];
  int rowCount = 0;
  int aScroll = 0;
  char lastCmd[80] = "";
  char lastAnswer[120] = "";
  bool answerBad = false;
  bool answerPending = false;
  char backLabel[12] = "< list";   // "< regions" on one region's page
};

void rptrScreenDraw(const RptrView& v);

// The floor the screen must not put a tap on: true while the sheet covers the tab bar.
bool rptrSheetOpen(const RptrView& v);

// One key of the open sheet, resolved for the shell (same pattern as the Settings editor).
struct RptrKeyHit {
  bool pressed = false;
  KeyKind kind = KeyKind::Char;
  int row = -1;
  int col = -1;
  char ch = 0;
};
RptrKeyHit rptrEditorKeyAt(const RptrView& v, int x, int y);
KeyboardField rptrEditorField(const RptrView& v);

}  // namespace ui
}  // namespace om
