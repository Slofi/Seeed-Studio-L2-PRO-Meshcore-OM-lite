// SPDX-License-Identifier: MIT
// EST-356: Bluetooth as a MeshCore companion node, off by default.
//
// The phone's MeshCore app talks to this node through upstream's companion code (MyMesh), so every
// command it sends (channels, radio params, TX power, name, position, time, contacts, messages,
// login/status/trace) is handled by MyMesh and saved in its DataStore. Our screens read the same
// stores (see mesh_link.h), and this file owns only the part upstream leaves to the board:
//   * the switch (NVS "omble", key "on", default OFF), so the BLE stack is not even allocated at boot
//     unless the user asked for it;
//   * the 6-digit pairing PIN in NodePrefs::ble_pin (the CMD_SET_DEVICE_PIN field), made once at the
//     first switch-on, never 123456;
//   * the three states the page and the status bar show (off / advertising / connected);
//   * the heap numbers around the one-time stack bring-up, for the Wi-Fi + BLE coexistence check.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ble_model.h"

namespace om {

// After meshLinkBegin(): reads the switch and makes NodePrefs::ble_pin the active PIN. Never touches
// the BLE radio; a saved "on" is applied by the first bleLoop().
bool bleBegin();
void bleLoop();
void bleSetEnabled(bool on);
bool bleIsEnabled();
ble::Phase blePhase();
bool bleConnected();

// Changes when the page or the status bar would look different (switch, PIN, phase).
uint32_t bleSerial();

// The stored PIN (0 = not made yet). Named separately from the transport so the USB command surface
// can report pin_set=yes/no without ever carrying the number.
uint32_t blePin();
// The CMD_SET_DEVICE_PIN path, live: NodePrefs field, savePrefs(), and the transport's passkey.
bool blePinSet(uint32_t pin);
uint32_t bleNewPin();     // 0 when the store refused the write
void blePinText(char* out, size_t cap);

uint32_t bleFreeHeap();
uint32_t bleFreePsram();

// EST-357: the two guards.
// The screen notice (a toast). The shell registers it once; this file never draws.
void bleSetNotice(void (*fn)(const char*));
// The start guard's numbers, so `ble status` can print what it is comparing against: free internal
// bytes and the largest internal block Bluedroid needs before the transport is allowed to allocate.
uint32_t bleStartNeedFree();
uint32_t bleStartNeedBlock();

}  // namespace om
