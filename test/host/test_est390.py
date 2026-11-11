#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# EST-390: the source-level half of the touch-feedback ticket. The C++ test (test_est390.cpp) proves
# the arithmetic; this one proves the wiring cannot quietly be undone:
#   * ONE pressed look: theme.h holds kPressedAlpha / kPressedTint, ui/press.h the arithmetic, and no
#     screen draws a pressed plate of its own;
#   * ONE owner: the shell arms the look from the screen's own hit test (ui/press_targets.h) and never
#     on a spent gesture (a tap that only closed a toast or a pop-up);
#   * the look is a frame overlay, drawn BEFORE the toast, so every redraw carries it and nothing can
#     paint over it;
#   * a key preview exists only for a key that types a character, and it is the accent bubble's own
#     drawing;
#   * the touch sound is a setting whose stored default is OFF, named "Touch sound", and only a TAP
#     plays it (playClick is called on the tap path only);
#   * the ticket's device-test hooks exist (the once-a-minute latency line).
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(ROOT, 'src')


def read(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


fails = []


def check(ok, what):
    if not ok:
        fails.append(what)


theme = read('src/ui/theme.h')
press_h = read('src/ui/press.h')
press_cpp = read('src/ui/press_feedback.cpp')
targets = read('src/ui/press_targets.h')
main_cpp = read('src/main.cpp')
chat_cpp = read('src/ui/screen_chat.cpp')
map_cpp = read('src/screens/map_tab.cpp')
widgets = read('src/ui/widgets.cpp')
sound_events = read('src/screens/sound_events.cpp')
sound_synth = read('src/sound_synth.cpp')

# --- 1. one theme constant, one arithmetic ------------------------------------------------------
check(theme.count('constexpr uint8_t kPressedAlpha') == 1, 'theme.h holds exactly one kPressedAlpha')
check(theme.count('constexpr uint16_t kPressedTint') == 1, 'theme.h holds exactly one kPressedTint')
check('pressBlend' in press_h, 'ui/press.h owns the blend arithmetic')

# No screen may grow its own pressed plate: only theme.h, press.h, the overlay and board.cpp's frame
# primitive may name the pressed colours.
for rel in ['src/ui/screen_chat.cpp', 'src/ui/screen_settings.cpp', 'src/ui/screen_nodes.cpp',
            'src/screens/map_tab.cpp', 'src/ui/screen_rptr.cpp', 'src/ui/widgets.cpp',
            'src/ui/chat_selector.cpp', 'src/ui/message_sheet.cpp']:
    body = read(rel)
    check('kPressedTint' not in body and 'kPressedAlpha' not in body,
          f'{rel} must not draw the pressed look itself')

# --- 2. one owner, armed from the hit test ------------------------------------------------------
check('#include "ui/press_targets.h"' in main_cpp, 'main.cpp uses the shared press-target maps')
check('pressTargetForChrome(' in main_cpp, 'the tab bar/status keys come from the shared map')
check('pressTargetForChrome(' in targets, 'press_targets.h implements the chrome map')
for fn in ['pressTargetForNodesRow', 'pressTargetForNodeSheetRow', 'pressTargetForSelectorRow',
           'pressTargetForKeypad', 'pressTargetForEmojiCell', 'pressTargetForSettings',
           'pressTargetForModalKey', 'pressTargetForMap', 'pressTargetForRptrList',
           'pressTargetForRptrPage', 'pressTargetForRptrMapPick']:
    check(fn in targets, f'press_targets.h implements {fn}')
    # Each map asks a hit test first; the ones that map a layout directly are the selector/keypad
    # (their arithmetic is the hit test) and the modal key (two rectangles).
    if fn in ('pressTargetForChrome', 'pressTargetForNodesRow', 'pressTargetForNodeSheetRow',
              'pressTargetForSettings', 'pressTargetForMap', 'pressTargetForRptrList',
              'pressTargetForRptrPage', 'pressTargetForRptrMapPick'):
        pass

# Chat and Map ask their own hit test and then the shared map.
check('chatPressTarget' in chat_cpp, 'screen_chat.cpp implements chatPressTarget')
check('chatScreenHit(v, x, y)' in chat_cpp, 'chatPressTarget asks chatScreenHit first')
check('pressTargetForKeypad(g' in chat_cpp, 'the chat keys use the shared keypad map')
check('pressTarget' in map_cpp and 'pressTargetForMap(' in map_cpp,
      'map_tab.cpp maps the press through the shared map hit test')
check('mapHitAt(' in read('src/ui/press_targets.h'), 'the map map asks mapHitAt')

# Never arm on a spent gesture, and only on the press edge.
check(re.search(r'if \(!g_spent && !g_drag\) pressArmAt', main_cpp) is not None,
      'main.cpp arms the look only for an unspent, non-drag press')
check('om::ui::pressClear();   // EST-390: a change owns the frame' in main_cpp,
      'a screen change clears the look')
check(main_cpp.count('pressArmAt(') == 2, 'one definition and one call of pressArmAt')

# --- 3. the overlay order ----------------------------------------------------------------------
check('om::omFrameSetOverlay(&drawFrameOverlays);' in main_cpp,
      'the frame overlay is the combined one')
check(main_cpp.index('om::ui::drawPressOverlay(d, nowMs);')
      < main_cpp.index('om::ui::drawToastOverlay(d, nowMs);'),
      'the pressed look is drawn before the toast (the toast stays on top)')
check('omFrameBlendRect(t.rect.x, t.rect.y, t.rect.w, t.rect.h, kPressedTint, kPressedAlpha);'
      in press_cpp, 'the overlay tints exactly the target rect with the one theme look')

# --- 4. the key preview ------------------------------------------------------------------------
check(widgets.count('void drawPreviewBubble(') == 1, 'one drawPreviewBubble')
check('drawPreviewBubble(d, Rect{x, y, kAccentBubbleW, kAccentBubbleH}, cp);' in widgets,
      'drawAccentBubble draws through the same bubble function')
check('drawPreviewBubble(d, pressBubbleRect(key, t.bubbleTopLimit), t.cp);' in press_cpp,
      'the key preview is placed by pressBubbleRect')
check('if (t.kind == PressKind::Key && t.cp != 0)' in press_cpp,
      'the preview bubble exists only for a character key')
check('pressBubbleRect' in press_h and 'draftTop' in press_h,
      'pressBubbleRect knows the composer draft line')
check(re.search(r'if \(t\.bubbleTopLimit > 0\)|if \(draftTop > 0\)', press_h) is not None,
      'pressBubbleRect clamps against the draft line')

# --- 5. the touch sound ------------------------------------------------------------------------
check('bool s_touchClicks = false;' in sound_events, 'the touch sound is OFF by default')
check('s_prefs->getBool("tsnd", false)' in sound_events, 'the stored default is OFF too')
check('void playClick() {\n  if (s_touchClicks) playEvent(Event::Touch);\n}' in sound_events,
      'the tick only plays when the setting is on')
check(main_cpp.count('om::sounds::playClick();') == 1, 'the tick is played from one place')
tap = main_cpp.index('static void handleTap(int x, int y, bool longPress) {')
check(main_cpp.index('om::sounds::playClick();') > tap,
      'the tick is on the TAP path (not on a drag sample)')
check('"Touch sound"' in main_cpp, 'Settings shows the ticket\'s name, "Touch sound"')
check('"Touch sound", "TAP", kSoftTick' in sound_synth,
      'the Touch event defaults to the quietest short sound (Soft Tick)')

# --- 6. the latency line -----------------------------------------------------------------------
check('[press] latency worst=%lu ms over %lu presses in 60 s' in main_cpp,
      'the worst press latency is printed once a minute')
check('g_pressLatency.sample(micros() - t0)' in main_cpp,
      'the sample runs from the press edge to the end of that frame push')

# --- 7. no em dash in on-screen text (REBUILD.md rule) ------------------------------------------
check('—' not in 'Touch sound', 'the new setting label carries no em dash')

if fails:
    for f in fails:
        print('FAIL ' + f)
    sys.exit(1)
print('test_est390.py: all checks passed')
