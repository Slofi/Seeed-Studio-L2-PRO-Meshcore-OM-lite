// SPDX-License-Identifier: MIT
// Settings, drawn on the new UI layer in the mock's style.
//
// The screens own no geometry: ui/settings_layout.h hands out the tab row, the title/back key, the
// body and every row rectangle, and the same rectangles answer the taps. Every page draws into the
// off-screen frame (omFrameBegin/omFrameEnd), so a USB screenshot sees it and a redraw never
// flickers. A list longer than the body scrolls with the finger (settingsDragScroll).
#pragma once

#include <stdint.h>

#include "ui/chat_selector.h"   // EST-350: the selector inside Settings is the Chat tab's own
#include "ui/settings_layout.h"
#include "ui/widgets.h"

namespace om {
namespace ui {

// The two text pages use the SAME keyboard sheet as the chat composer (EST-352): letters, 123, Del,
// the s/c/z long press, no emoji. The old 10x4 letter grid and 13x5 base64 alphabet are gone.
enum class SettingsEditor : uint8_t { None = 0, Letters, Base64 };

// The sheet field one editor means: a name (MeshCore's allowed characters and 31-byte limit) or a
// channel key (base64 alphabet, 24 bytes = a 16-byte secret with its "==" padding).
KeyboardField settingsEditorField(SettingsEditor editor);

struct SettingsView {
  StatusModel status;
  SettingsTab tab = SettingsTab::General;
  const char* title = "Settings";
  bool subPage = false;          // no settings tab row: back + title under the status bar
  const SettingRow* rows = nullptr;
  int rowCount = 0;
  int scroll = 0;
  // The title-row back key is always drawn: on the root it leaves Settings for the screen the gear
  // was pressed on, on a sub-page it goes up one level (the mock's L2 back key).
  SettingsEditor editor = SettingsEditor::None;
  const char* editText = "";
  const char* okLabel = "OK";
  bool editNums = false;         // the sheet's page: letters or 123
  bool editShift = false;
  int accentRow = -1;            // the held s/c/z key while its preview is up
  int accentCol = -1;
  uint32_t accentCp = 0;
  // EST-350: the chat selector dropped down over the body. `sel.selected` is the chat whose settings
  // the page is showing — in Settings that is NOT the chat open on the Chat tab, on purpose.
  bool selector = false;
  SelectorModel sel;
};

void settingsScreenDraw(const SettingsView& v);

// One key of the editor's sheet, resolved to what the shell does with it. `ch` is the character for
// KeyKind::Char with shift already applied; `row`/`col` locate the drawn key for the accent bubble.
struct SettingsKeyHit {
  bool pressed = false;
  KeyKind kind = KeyKind::Char;
  char ch = 0;
  int row = -1;
  int col = -1;
};

// All hit tests are pure reads of the same layout the drawing used.
int settingsTabHit(const SettingsView& v, int x, int y);          // 0..4 or -1
bool settingsBackHit(const SettingsView& v, int x, int y);
int settingsRowHit(const SettingsView& v, int x, int y);          // row index or -1
int settingsStepperHit(const SettingsView& v, int x, int y);      // -1 / +1 on a stepper, else 0
// The sheet key under (x, y); pressed=false when the point is not on a key (above the sheet, a gap).
SettingsKeyHit settingsEditorKeyAt(const SettingsView& v, int x, int y);
// EST-350: the dropdown's press rule (a row picks, the pinned row opens the channel list, anything
// else closes it and is spent), from the same rectangles it was drawn in. It is the Chat tab's own
// rule (chat_selector.h selectorPress): while the dropdown is open nothing behind it can be hit.
SelectorPress settingsSelectorPress(const SettingsView& v, int x, int y);

}  // namespace ui
}  // namespace om
