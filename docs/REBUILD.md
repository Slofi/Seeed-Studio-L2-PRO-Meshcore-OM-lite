# OM Lite rebuild — the brief (Claude leads, DS builds; started 2026-11-10, S494)

Read this first, every ticket. It is the one place for the goal, the rules and the order.

## What Filip asked (2026-11-10)

- "The app DS wrote looks a bit too retro, that's why I want to change it." "Use the mockup as a guide."
- "Tanmatsu is also on the way! Both devices should have a same style app, visually and (to the extent
  possible on L2 Pro) functionally. I like the way the mockup looks, so take it as a (visual) guide.
  On the functions side, you have more free hands. Also look at WadaMesh and OverMesh for features."
- MeshCore features like WadaMesh's: discovering nearby nodes and repeaters, and the rest of its set.
- Audio: "change all the sounds, I am not really happy with the current sounds. Better quality-wise.
  Not just those 8-bit sounds."

## The verdict this rebuild starts from (Claude, 2026-11-10)

Keep the core, rebuild the screens. `board`, `mesh_link`, `channels`, `map`, `audio`, `ota`, `radio`
work on the device and stay. `main.cpp` (1,900 lines) draws every screen with hard-coded coordinates and
decides taps with a second, separate set of the same numbers (578 numeric literals, 93 globals, 3 named
colours): restyling the drawing would break touch everywhere. That layer is rebuilt.

## The visual guide

`/home/slofi/Projects/pocket-mesh/ui-mock/index.html` (read its CSS and markup, it is the spec). It is
the approved preview of 2026-10-20 (`preview.html`, frozen) with ONE change Filip asked for on 2026-11-10:
the accent is OM gold. If the two disagree on anything else, say so. It draws BOTH devices: L2 Pro 320x240 touch (bottom tab bar, single column,
list then full-screen detail with back, keyboard as a sheet over the tab bar) and Tanmatsu 800x480 with a
keyboard and no touch (left sidebar, list + detail two panes, focus ring, key hints).

Design tokens from it (CSS custom properties), NEUTRAL GREY with a slight warm lean since 2026-11-10
(Filip: the first blue-tinted greys were "too cold, suitable for blue accent, but not for gold", the amber
try was "too warm"; each grey now keeps its brightness with almost no tint): bg `#131210`, panel `#1c1b19`,
panel2 `#242321`, line `#333230`, ink `#e8edf5`, dim `#93a0b4`, dimmer `#5f6b7d`, acc `#e8b04f` (gold),
client `#4aa3ff`, acc2 `#39d98a`, warn `#ffb020`, bad `#ff5c5c`; status bar and tab bar `#171614`, 26 px
and 25 px, 11 px status text; row line `#1e1d1b`. Read the rest from `index.html`, it is the truth;
list rows min 30 px; on-screen keys about 22 px. Text: a sans for names and messages, a mono for numbers,
ids, ages and the status line (the mock uses mono in about 40 places).
**Accent: OM gold `#e8b04f`** (Filip, 2026-11-10: "change the blue colour accent with the golden one",
done in `index.html` first so he could see it). In the mock: `--acc` gold; client nodes keep their own blue
`--client:#4aa3ff` (repeater green, room amber), so a client dot never looks selected; selected backgrounds
are dark gold `#3a2c12` / `#1f1a10`, selected text `#f6e3b8`. One named constant per colour in `theme.h`.

What makes the current app look "retro", and what replaces it: bitmap fonts → anti-aliased smooth fonts
(LovyanGFX smooth/VLW fonts in flash, two families, a few sizes); flat boxes → the mock's panels, 1 px
lines, rounded corners where the mock rounds, real icons in the tab bar; cramped layout → the mock's
spacing. Check the size cost of fonts in flash and say it.

## Architecture rules

1. A small UI layer, `src/ui/`: `theme.h` (every colour, size and font, nothing else may hold one),
   a layout that hands out rectangles, widgets (status bar, tab bar, list row, button, sheet, toast,
   keyboard), and one file per screen.
2. **One rectangle drives both the drawing and the tap.** A screen never repeats a coordinate. No magic
   numbers in screens: they come from `theme.h` or from the layout.
3. The screen size is a parameter (320x240 touch, 800x480 keyboard). Write it once; the L2 is first, the
   Tanmatsu arrangement follows the mock and must not need a rewrite.
4. **Everything draws into the off-screen frame** (`omFrameBegin`/`omFrameEnd` in board.cpp), nothing
   straight to the panel: one flicker-free path, and the USB screenshot (`tools/shot.py`) reads that frame.
   Today only 10 paths use it, so a screenshot of most screens is black (measured 2026-11-10). Keep the
   scroll that follows the finger.
5. Behaviour that works today must keep working: chat send and receive, unread, the message menu
   (REPLY / INFO / COPY), channels, map from SD, settings, sound settings, USB OTA.
6. WadaMesh is GPL-3.0: read it for what it does, write our own code, copy nothing. OverMesh is ours.
7. No em dashes in any text on the screen: commas, colons, full stops.

## Work order (one ticket each, small enough for one 30-minute run)

1. Screenshot over USB: the device sends its frame on request, a host tool writes a PNG. Claude runs it.
2. Theme, fonts, layout, widgets; status bar + tab bar + the Nodes list on the new layer.
3. Chat (list, bubbles, keyboard sheet, message menu) on the new layer.
4. Map chrome, RPTR, Settings on the new layer; `main.cpp` reduced to setup + loop + dispatch.
5. MeshCore features (WadaMesh + OverMesh): node list from adverts (last heard, SNR, hops, type,
   position, favourite), discover nearby nodes and repeaters, ping, trace, neighbours, repeater login +
   status + the admin set already mirrored from OM (`pocket-mesh.md`, RPTR section), DMs.
6. Sound: better quality than square-ish beeps. Soft attack/decay envelopes, harmonics, bell or chord
   voices, or short 16-bit samples in flash; one sound per event (boot, message, DM, mention, sent,
   error, low battery, touch). Keep the new boot "TK-TK" character (2026-11-10).

## How to work (every ticket)

- You may write only inside this `firmware/` folder. `.git` is read-only for you on purpose: do not
  commit. Claude reviews the diff and commits as Slofi.
- Build inside your sandbox, real exit code:
  `cd /home/slofi/Projects/pocket-mesh/firmware && PLATFORMIO_CORE_DIR=/home/slofi/.cache/pio-om-lite/core /home/slofi/.local/share/pipx/venvs/platformio/bin/pio run -e om_lite_l2`
- Host tests for logic (layout, hit testing, notification decisions) with the system `g++`, in `test/host/`,
  one script `test/host/run.sh` that exits non-zero on any failure.
- You cannot see the device: no flashing, no serial. Claude flashes over USB OTA, takes screenshots and
  reports what the screen shows.
- At the end of every run, append to `docs/PROGRESS.md`: the ticket, what changed, build size (RAM and
  flash), test results, what you could not check, and the next step. Plain words.
