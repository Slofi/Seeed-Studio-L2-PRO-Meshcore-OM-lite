// SPDX-License-Identifier: MIT
// EST-422: the settings that ASK before they change, and the one sheet that asks.
//
// Filip chose (2026-11-13) "confirm the important settings, not everything": radio region / preset,
// TX power, path hash, the node name, every Delete or Forget, the Wi-Fi switch, the Bluetooth
// switch, the keyboard switch and the Position mode. The small switches (sounds, volume, brightness,
// auto-off, traces, names) still change at the touch.
//
// Two things live here, so the drawing, the tap and the host test cannot disagree:
//   * `confirmKindForAction`: the ONE list, keyed by the shell's own action id (SetAct, moved here
//     with the enum so the test reads the same numbers main.cpp switches on). ConfirmKind::None is
//     every setting that changes at once.
//   * `ConfirmAsk`: the sheet's state (open or not, which change, which value, the question) and the
//     answer rule: Cancel and a tap outside answer None and change NOTHING, Apply answers the kind
//     and the shell runs the change. `confirmTap` is the whole flow in one call.
//   * the question and the key label: "Change X to Y?" with Cancel on the left (the default side)
//     and Apply on the right, from the same SettingsModalLayout the "Clear messages" pop-up uses.
//
// Two changes have no row id of their own: the node name changes when the name editor's OK is
// committed, and the Position mode changes on the GPS choice or on the map picker's Save. They name
// their ConfirmKind at that point instead of through the table, which is why the host test checks
// them through the flow as well as through an id.
//
// Plain C++ (no Arduino, no LovyanGFX), so test/host/test_est422.cpp proves the list and the flow.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "settings_layout.h"   // SettingsModalHit: the sheet's two keys, from the drawing's rectangles

// The shell's settings action ids, unchanged from EST-347 (they used to be a private enum in
// main.cpp). SetAct moved here so one table can say which of them asks first, and so the host test
// asks exactly the ids the shell switches on. File scope, like before, so main.cpp's switch reads
// A_Preset and friends without a namespace change.
enum SetAct : uint16_t {
  A_None = 0,
  A_Preset, A_TxPower, A_Hash, A_Node, A_NodeName, A_Channels, A_Wifi, A_WifiToggle, A_WifiScan, A_WifiSaved, A_Bt, A_BtToggle, A_BtNewPin, A_Sound,
  A_Brightness, A_Time, A_AdvertPos,
  // EST-412: the Grove keyboard switch (default OFF).
  A_GroveKeyboard,
  // EST-405: the screen auto-off switch and the row that opens its five time choices.
  A_IdleAutoOff, A_IdleTime,
  // EST-392: ONE position source control (the General page's `Position` row); it opens the small
  // GPS / Fixed choice.
  A_PosSource,
  A_Apply, A_Cancel,
  A_MapRecentre, A_MapClear, A_RptrForget, A_RptrAuto,
  // EST-414: Settings > RPTR > "Open repeater" jumps to the open session's page on the RPTR tab.
  A_RptrOpen,
  A_ChatPick, A_ChatOpen, A_ChatEnabled, A_ChatSound, A_ChatRename, A_ChatDelete,
  A_ChatMsgSound, A_ChatDmSound, A_ChatMenSound,
  A_SoundMaster, A_SoundVolume, A_SoundBoot, A_SoundMsg, A_SoundDm, A_SoundMen, A_SoundSent,
  A_SoundError, A_SoundNew, A_SoundBatt,
  A_SoundTouch, A_SoundClick, A_SoundDnd, A_SoundWelcome, A_SoundPickUse,
  A_ChanAdd, A_ChanRename, A_ChanEnabled, A_ChanSound, A_ChanClear, A_ChanDelete,
  A_AddCreate, A_AddPrivate, A_AddPublic, A_AddHashtag,
  A_TimeManual, A_TimeApply,
  A_CustomOpen, A_CustomFreq, A_CustomBw, A_CustomSf, A_CustomCr, A_CustomApply,
  A_PresetBase = 100,      // 100..103 the four presets
  A_HashBase = 120,        // 120..122 = 1B / 2B / 3B
  A_PickBase = 140,        // 140.. the shared sound library, then Off
  A_ChanBase = 200,        // 200..207 one row per channel
  A_TimeFieldBase = 220,   // 220..224 year / month / day / hour / minute
  A_WifiNetBase = 240,     // 240..247 the scan rows on the Wi-Fi page (strongest first)
  A_WifiForgetBase = 250,  // 250..254 the saved rows on the Saved networks page
  A_IdleTimeBase = 260,    // 260..264 the five screen auto-off times, in kIdleTimes order
};

namespace om {
namespace ui {

// One entry per important change. `None` means "apply at the touch".
enum class ConfirmKind : uint8_t {
  None = 0,
  Preset,            // radio region / preset, including the custom values
  TxPower,
  PathHash,
  NodeName,          // committed by the name editor's OK
  ChannelDelete,     // Chat page and Channel page
  WifiForget,        // one saved network
  RepeaterForget,    // the remembered repeater's password
  WifiSwitch,
  BtSwitch,
  KeyboardSwitch,
  Position,          // GPS, or a point picked on the map
  ClearMessages,     // EST-382's question, the same sheet since EST-422
};

// The one list. A row whose id answers None changes the moment it is touched.
constexpr ConfirmKind confirmKindForAction(uint16_t id) {
  switch (id) {
    case A_TxPower:       return ConfirmKind::TxPower;
    case A_WifiToggle:    return ConfirmKind::WifiSwitch;
    case A_BtToggle:      return ConfirmKind::BtSwitch;
    case A_GroveKeyboard: return ConfirmKind::KeyboardSwitch;
    case A_ChatDelete:    return ConfirmKind::ChannelDelete;
    case A_ChanDelete:    return ConfirmKind::ChannelDelete;
    case A_RptrForget:    return ConfirmKind::RepeaterForget;
    case A_ChanClear:     return ConfirmKind::ClearMessages;
    // The presets: the four rows and the custom page's Apply (both are a region change).
    case A_CustomApply:   return ConfirmKind::Preset;
    default: break;
  }
  if (id >= A_PresetBase && id < A_PresetBase + 4) return ConfirmKind::Preset;
  if (id >= A_HashBase && id < A_HashBase + 3) return ConfirmKind::PathHash;
  if (id >= A_WifiForgetBase && id < A_WifiForgetBase + 5) return ConfirmKind::WifiForget;
  return ConfirmKind::None;
}

// The small switches and the pages: asked only so the test can name them, and every one is None.
constexpr bool settingChangesAtOnce(uint16_t id) { return confirmKindForAction(id) == ConfirmKind::None; }

// The key on the right of the sheet. A delete says what it does.
constexpr const char* confirmApplyLabel(ConfirmKind k) {
  switch (k) {
    case ConfirmKind::ChannelDelete:  return "Delete";
    case ConfirmKind::WifiForget:     return "Forget";
    case ConfirmKind::RepeaterForget: return "Forget";
    default:                          return "Apply";
  }
}

// Destructive answers keep the red key (the "Clear messages" look); the rest use the gold one.
constexpr bool confirmIsDangerous(ConfirmKind k) {
  return k == ConfirmKind::ChannelDelete || k == ConfirmKind::WifiForget ||
         k == ConfirmKind::RepeaterForget || k == ConfirmKind::ClearMessages;
}

// "Change X to Y?" (the ticket's sentence) for a value change; the deletes ask their own sentence
// because "Change the channel to 3?" would not say what happens. `value` is the new value or the
// name the question is about, and it is always quoted in full.
inline void confirmQuestionFor(ConfirmKind k, const char* value, char* out, int cap) {
  if (out == nullptr || cap <= 0) return;
  const char* v = (value != nullptr && value[0]) ? value : "this";
  switch (k) {
    case ConfirmKind::Preset:
      snprintf(out, (size_t)cap, "Change region to %s? The radio restarts on the new preset.", v);
      break;
    case ConfirmKind::TxPower:     snprintf(out, (size_t)cap, "Change TX power to %s?", v); break;
    case ConfirmKind::PathHash:    snprintf(out, (size_t)cap, "Change path hash to %s?", v); break;
    case ConfirmKind::NodeName:    snprintf(out, (size_t)cap, "Change the node name to %s?", v); break;
    case ConfirmKind::ChannelDelete:
      snprintf(out, (size_t)cap, "Delete the channel %s and its messages?", v);
      break;
    case ConfirmKind::WifiForget:  snprintf(out, (size_t)cap, "Forget the Wi-Fi network %s?", v); break;
    case ConfirmKind::RepeaterForget:
      snprintf(out, (size_t)cap, "Forget the saved repeater %s?", v);
      break;
    case ConfirmKind::WifiSwitch:     snprintf(out, (size_t)cap, "Turn Wi-Fi %s?", v); break;
    case ConfirmKind::BtSwitch:       snprintf(out, (size_t)cap, "Turn Bluetooth %s?", v); break;
    case ConfirmKind::KeyboardSwitch: snprintf(out, (size_t)cap, "Turn the keyboard %s?", v); break;
    case ConfirmKind::Position:       snprintf(out, (size_t)cap, "Change the position to %s?", v); break;
    case ConfirmKind::ClearMessages:
      snprintf(out, (size_t)cap, "Clear all messages in %s? This cannot be undone.", v);
      break;
    default:
      out[0] = 0;
      break;
  }
}

// What a confirmed change must do. The shell implements it against the real stores; the host test
// implements it against plain variables, and both are driven by `confirmTap` below.
using ConfirmApplyFn = void (*)(void* ctx, ConfirmKind kind, int arg, const char* value);

// The sheet's whole state. One instance app-wide (the shell's g_ask).
struct ConfirmAsk {
  static constexpr int kValueCap = 40;
  static constexpr int kTextCap = 160;

  bool open = false;
  ConfirmKind kind = ConfirmKind::None;
  int arg = 0;                    // the shell's own payload (a preset index, a channel, ...)
  char value[kValueCap] = "";     // the new value or the name the question quotes
  char text[kTextCap] = "";       // the question, wrapped by the shared wrapper when drawn

  // The sheet goes up: nothing is changed yet. A None kind is a caller bug and is refused.
  bool begin(ConfirmKind k, int a, const char* v) {
    kind = k;
    arg = a;
    open = (k != ConfirmKind::None);
    value[0] = 0;
    text[0] = 0;
    if (!open) return false;
    if (v != nullptr) snprintf(value, sizeof(value), "%s", v);
    confirmQuestionFor(k, value, text, (int)sizeof(text));
    return true;
  }

  // A tap on the sheet. Cancel and Outside (the scrim, the default side) answer None and change
  // nothing; Confirm answers the kind the shell must apply now. Either way the sheet closes.
  ConfirmKind answer(SettingsModalHit hit) {
    if (!open) return ConfirmKind::None;
    const ConfirmKind k = (hit == SettingsModalHit::Confirm) ? kind : ConfirmKind::None;
    open = false;
    kind = ConfirmKind::None;
    return k;
  }

  void close() {
    open = false;
    kind = ConfirmKind::None;
  }
};

// The whole flow: one tap on the sheet. Returns true when a change was applied, so the caller knows
// whether to run its own "saved" toast (it does that inside `apply`).
inline bool confirmTap(ConfirmAsk& a, SettingsModalHit hit, ConfirmApplyFn apply, void* ctx) {
  const ConfirmKind k = a.answer(hit);
  if (k == ConfirmKind::None) return false;
  if (apply != nullptr) apply(ctx, k, a.arg, a.value);
  return true;
}

}  // namespace ui
}  // namespace om
