// SPDX-License-Identifier: MIT
// EST-356: the Bluetooth parts that need no radio, with the system g++.
//
// It proves, over the same headers the firmware compiles:
//   * the switch starts OFF and the phase follows it (off -> advertising -> connected ->
//     advertising -> off), with one serial bump per visible change and no bump on a poll;
//   * the page/root/USB words for each phase;
//   * the PIN generator: always six digits in 100000..999999, never 123456, and 123456 is refused
//     by pinValid() even if a future caller hands it one;
//   * the change watch: the first poll is a baseline, an outside change is the phone's, our own
//     write is not, and the channels/radio/name bits are independent;
//   * the OMCMD1 `ble` command: status / on / off are accepted, everything else is refused (and
//     there is no word that reads the PIN).
#include <stdio.h>
#include <string.h>

#include "ble_model.h"
#include "ble_pin.h"
#include "usb_cmd_logic.h"

static int g_fail = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL %s\n", what);
    ++g_fail;
  }
}

static void testModel() {
  om::ble::Model m;
  m.reset();
  check(!m.enabled(), "a fresh switch is OFF (the ticket's default)");
  check(m.phase() == om::ble::Phase::Off, "a fresh switch is in the off phase");
  const uint32_t s0 = m.serial();

  m.enable();
  check(m.enabled() && m.phase() == om::ble::Phase::Advertising, "on -> advertising");
  check(m.serial() != s0, "switching on is a visible change");
  const uint32_t s1 = m.serial();

  m.enable();
  check(m.serial() == s1, "enabling twice does not bump the serial");

  m.noteConnected();
  check(m.connected() && m.phase() == om::ble::Phase::Connected, "a peer -> connected");
  const uint32_t s2 = m.serial();
  check(s2 != s1, "connecting is a visible change");

  m.noteConnected();
  check(m.serial() == s2, "connected twice does not bump the serial");

  m.noteDisconnected();
  check(m.enabled() && m.phase() == om::ble::Phase::Advertising, "peer left -> advertising");
  check(m.serial() == s2 + 1, "disconnecting is one visible change");

  m.disable();
  check(!m.enabled() && m.phase() == om::ble::Phase::Off, "off -> off phase");
  check(!m.connected(), "an off switch is never connected");
  const uint32_t s3 = m.serial();
  m.disable();
  check(m.serial() == s3, "disabling twice does not bump the serial");
  check(s3 > s2, "off is a visible change");

  // A transport reporting a peer while the switch is off must not invent a connection.
  m.noteConnected();
  check(m.phase() == om::ble::Phase::Off, "no connection while the switch is off");

  check(strcmp(om::ble::phaseToken(om::ble::Phase::Off), "off") == 0, "off token");
  check(strcmp(om::ble::phaseToken(om::ble::Phase::Advertising), "advertising") == 0,
        "advertising token");
  check(strcmp(om::ble::phaseToken(om::ble::Phase::Connected), "connected") == 0, "connected token");
  check(strcmp(om::ble::phaseLabel(om::ble::Phase::Off), "off") == 0, "off row value");
  check(strcmp(om::ble::phaseLabel(om::ble::Phase::Advertising), "on") == 0, "on row value");
  check(strcmp(om::ble::phaseLabel(om::ble::Phase::Connected), "connected") == 0,
        "connected row value");
}

static void testPin() {
  check(!om::ble::pinValid(0), "zero is not a PIN");
  check(!om::ble::pinValid(99999), "five digits are not a PIN");
  check(om::ble::pinValid(100000), "100000 is the lowest PIN");
  check(om::ble::pinValid(999999), "999999 is the highest PIN");
  check(!om::ble::pinValid(om::ble::kPinForbidden), "123456 is never a valid PIN");

  // 123456 maps one step up; the top of the range wraps only when it must.
  check(om::ble::pinFromRandom(23456) != om::ble::kPinForbidden, "the forbidden word is pushed off");
  check(om::ble::pinFromRandom(23456) == 123457, "123456 becomes 123457");
  check(om::ble::pinFromRandom(899999) == 999999, "the top of the range is reachable");
  check(om::ble::pinFromRandom(0) == 100000, "the bottom of the range is reachable");

  for (uint32_t r = 0; r < 1000000; ++r) {
    const uint32_t p = om::ble::pinFromRandom(r);
    if (p < 100000 || p > 999999 || p == 123456) {
      printf("FAIL generator made %lu from %lu\n", (unsigned long)p, (unsigned long)r);
      ++g_fail;
      break;
    }
  }

  char text[8];
  om::ble::pinText(text, sizeof(text), 100000);
  check(strcmp(text, "100000") == 0, "six digits, zero padded");
  check(strlen(text) == 6, "always six characters");
  om::ble::pinText(text, sizeof(text), 999999);
  check(strcmp(text, "999999") == 0, "the top prints as six digits");
  om::ble::pinText(text, 4, 123457);
  check(strcmp(text, "123") == 0, "a short buffer is respected, not overrun");
}

static void testChangeWatch() {
  om::ble::ChangeWatch w;
  // The first poll is the baseline: the stores already have content, which is not a phone change.
  check(w.poll(11, 22, 33, 0, 0) == om::ble::kChangeNone, "first poll is a baseline");
  check(w.poll(11, 22, 33, 0, 0) == om::ble::kChangeNone, "nothing changed, nothing reported");

  check(w.poll(12, 22, 33, 0, 0) == om::ble::kChangeChannels, "an outside channel change is the phone's");
  check(w.poll(12, 22, 33, 0, 0) == om::ble::kChangeNone, "a change is reported once");

  check(w.poll(13, 22, 33, 7, 0) == om::ble::kChangeNone, "our own channel write is not the phone's");
  check(w.poll(14, 22, 33, 7, 0) == om::ble::kChangeChannels, "the next outside change is reported again");
  check(w.poll(14, 22, 33, 7, 0) == om::ble::kChangeNone, "the consumed generation does not report twice");

  check(w.poll(14, 24, 33, 7, 0) == om::ble::kChangeRadio, "an outside radio change is the phone's");
  check(w.poll(14, 25, 33, 7, 3) == om::ble::kChangeNone, "our own radio write is not the phone's");

  check(w.poll(14, 25, 44, 7, 3) == om::ble::kChangeName, "an outside name change is the phone's");
  check(w.poll(15, 26, 45, 7, 3) ==
            (om::ble::kChangeChannels | om::ble::kChangeRadio | om::ble::kChangeName),
        "three changes in one pass are all reported");

  char text[48];
  om::ble::phoneChangeText(om::ble::kChangeChannels, text, sizeof(text));
  check(strcmp(text, "Channels changed from the phone") == 0, "channel toast text");
  om::ble::phoneChangeText(om::ble::kChangeRadio, text, sizeof(text));
  check(strcmp(text, "Radio changed from the phone") == 0, "radio toast text");
  om::ble::phoneChangeText(om::ble::kChangeName, text, sizeof(text));
  check(strcmp(text, "Name changed from the phone") == 0, "name toast text");
  om::ble::phoneChangeText(om::ble::kChangeNone, text, sizeof(text));
  check(text[0] == 0, "no change, no text");
  om::ble::phoneChangeText(om::ble::kChangeRadio, text, 8);
  check(strcmp(text, "Radio c") == 0, "a short toast buffer is respected");
}

static void testBleCommand() {
  char a[] = "ble status";
  om::usb::Command c = om::usb::parseCommand(a);
  check(c.kind == om::usb::Kind::Ble && c.error == nullptr && c.target != nullptr &&
            strcmp(c.target, "status") == 0,
        "ble status is accepted");

  char b[] = "ble on";
  c = om::usb::parseCommand(b);
  check(c.kind == om::usb::Kind::Ble && c.error == nullptr && strcmp(c.target, "on") == 0,
        "ble on is accepted");
  char d[] = "ble off";
  c = om::usb::parseCommand(d);
  check(c.kind == om::usb::Kind::Ble && c.error == nullptr && strcmp(c.target, "off") == 0,
        "ble off is accepted");

  char e[] = "ble";
  c = om::usb::parseCommand(e);
  check(c.kind == om::usb::Kind::Ble && c.error != nullptr, "ble alone is refused");

  char f[] = "ble pin";
  c = om::usb::parseCommand(f);
  check(c.error != nullptr, "there is no command that reads the PIN");

  char g[] = "ble on extra";
  c = om::usb::parseCommand(g);
  check(c.error != nullptr, "extra arguments are refused");

  char h[] = "ble status extra";
  c = om::usb::parseCommand(h);
  check(c.error != nullptr, "status takes no arguments");

  // The other commands still parse exactly as before.
  char i[] = "wifi status";
  check(om::usb::parseCommand(i).kind == om::usb::Kind::Wifi, "wifi is untouched");
  char j[] = "status";
  check(om::usb::parseCommand(j).kind == om::usb::Kind::Status, "status is untouched");
}

int main() {
  testModel();
  testPin();
  testChangeWatch();
  testBleCommand();
  if (g_fail) {
    printf("est356: %d failure(s)\n", g_fail);
    return 1;
  }
  printf("est356: Bluetooth switch, PIN generator, change watch and the ble command ok\n");
  return 0;
}
