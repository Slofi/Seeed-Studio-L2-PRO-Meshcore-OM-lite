# OM Lite sound library (EST-369)

Every file is the exact waveform the device streams (16 kHz, 16-bit mono, the same sound_synth.cpp). `id` is the shared-library id stored in NVS; `n` is the sound's index inside its shape. Any sound may be chosen for any notification.

Shapes, in the picker's order: **Solo** (one note), **Duo** (two notes), **Trio** (three notes), **Multi** (four or more notes, a short phrase or chord) and **Alert** (the long low-battery warning). The voice starts the name.

| file | shape | n | voice | id | name | notes (Hz@at+dur ms) | ms | peak |
|---|---|---:|---|---:|---|---|---:|---:|
| solo-0-beep-clean.wav | Solo | 0 | beep | 54 | Beep Clean | 880 Hz@0+110 | 110 | 14251 |
| solo-1-beep-high.wav | Solo | 1 | beep | 55 | Beep High | 1568 Hz@0+90 | 90 | 13332 |
| solo-2-soft-tick.wav | Solo | 2 | soft | 56 | Soft Tick | 1175 Hz@0+80 | 80 | 6021 |
| solo-3-bell-c6.wav | Solo | 3 | bell | 57 | Bell C6 | 1047 Hz@0+220 | 220 | 18826 |
| solo-4-bell-low.wav | Solo | 4 | bell | 58 | Bell Low | 659 Hz@0+250 | 250 | 18754 |
| solo-5-glass-ping.wav | Solo | 5 | glass | 59 | Glass Ping | 2093 Hz@0+180 | 180 | 15008 |
| solo-6-marimba-tock.wav | Solo | 6 | marimba | 60 | Marimba Tock | 784 Hz@0+150 | 150 | 20543 |
| solo-7-wood-pluck.wav | Solo | 7 | wood | 61 | Wood Pluck | 392 Hz@0+200 | 200 | 18030 |
| solo-8-organ-mellow.wav | Solo | 8 | organ | 62 | Organ Mellow | 330 Hz@0+240 | 240 | 15419 |
| solo-9-kalimba-tine.wav | Solo | 9 | kalimba | 63 | Kalimba Tine | 880 Hz@0+180 | 180 | 20006 |
| duo-0-bell-tk-tk.wav | Duo | 0 | bell | 64 | Bell TK-TK | 2349 Hz@0+60, 2794 Hz@115+70 | 205 | 16659 |
| duo-1-soft-hi.wav | Duo | 1 | soft | 65 | Soft Hi | 880 Hz@0+100, 698 Hz@90+130 | 250 | 12395 |
| duo-2-marimba-hi.wav | Duo | 2 | marimba | 66 | Marimba Hi | 1047 Hz@0+90, 784 Hz@80+120 | 230 | 13335 |
| duo-3-bell-hi.wav | Duo | 3 | bell | 67 | Bell Hi | 1397 Hz@0+90, 1047 Hz@80+120 | 230 | 13128 |
| duo-4-wood-warm.wav | Duo | 4 | wood | 68 | Wood Warm | 349 Hz@0+140, 440 Hz@100+160 | 300 | 13125 |
| duo-5-chime-bright.wav | Duo | 5 | chime | 69 | Chime Bright | 1319 Hz@0+80, 1760 Hz@70+110 | 220 | 11490 |
| duo-6-soft-lo.wav | Duo | 6 | soft | 70 | Soft Lo | 494 Hz@0+100, 392 Hz@90+130 | 250 | 12304 |
| duo-7-organ-low.wav | Duo | 7 | organ | 71 | Organ Low | 262 Hz@0+180, 330 Hz@140+200 | 340 | 15801 |
| duo-8-kalimba-rise.wav | Duo | 8 | kalimba | 72 | Kalimba Rise | 1047 Hz@0+100, 1319 Hz@90+130 | 220 | 12678 |
| duo-9-wood-double.wav | Duo | 9 | wood | 73 | Wood Double | 587 Hz@0+70, 587 Hz@90+70 | 160 | 19124 |
| trio-0-glass-trio.wav | Trio | 0 | glass | 74 | Glass Trio | 1568 Hz@0+65, 2093 Hz@65+65, 2637 Hz@130+110 | 280 | 16274 |
| trio-1-chime-arp.wav | Trio | 1 | chime | 75 | Chime Arp | 1047 Hz@0+70, 1319 Hz@65+70, 1568 Hz@130+120 | 250 | 11325 |
| trio-2-bell-rise.wav | Trio | 2 | bell | 76 | Bell Rise | 523 Hz@0+90, 659 Hz@80+90, 784 Hz@160+140 | 300 | 12645 |
| trio-3-bell-fall.wav | Trio | 3 | bell | 77 | Bell Fall | 784 Hz@0+90, 659 Hz@80+90, 523 Hz@160+140 | 300 | 12221 |
| trio-4-marimba-roll.wav | Trio | 4 | marimba | 78 | Marimba Roll | 392 Hz@0+80, 523 Hz@70+80, 659 Hz@140+120 | 260 | 14164 |
| trio-5-wood-motif.wav | Trio | 5 | wood | 79 | Wood Motif | 220 Hz@0+100, 330 Hz@90+100, 262 Hz@180+120 | 300 | 12045 |
| trio-6-soft-motif.wav | Trio | 6 | soft | 80 | Soft Motif | 440 Hz@0+90, 415 Hz@80+90, 349 Hz@160+150 | 310 | 12527 |
| trio-7-organ-rise.wav | Trio | 7 | organ | 81 | Organ Rise | 196 Hz@0+130, 262 Hz@110+130, 330 Hz@220+180 | 400 | 12636 |
| trio-8-kalimba-turn.wav | Trio | 8 | kalimba | 82 | Kalimba Turn | 880 Hz@0+90, 1047 Hz@80+90, 880 Hz@160+130 | 290 | 13884 |
| trio-9-glass-top.wav | Trio | 9 | glass | 83 | Glass Top | 2093 Hz@0+70, 2349 Hz@65+70, 2794 Hz@130+120 | 250 | 11116 |
| multi-0-bell-chime.wav | Multi | 0 | bell | 84 | Bell Chime | 659 Hz@0+80, 784 Hz@70+80, 988 Hz@140+80, 1319 Hz@210+180 | 390 | 12930 |
| multi-1-chime-run.wav | Multi | 1 | chime | 85 | Chime Run | 784 Hz@0+60, 1047 Hz@55+60, 1319 Hz@110+60, 1568 Hz@165+150 | 315 | 11309 |
| multi-2-marimba-run.wav | Multi | 2 | marimba | 86 | Marimba Run | 523 Hz@0+60, 659 Hz@50+60, 784 Hz@100+60, 1047 Hz@150+150 | 300 | 14029 |
| multi-3-wood-groove.wav | Multi | 3 | wood | 87 | Wood Groove | 392 Hz@0+70, 392 Hz@70+70, 523 Hz@140+70, 659 Hz@210+150 | 360 | 18990 |
| multi-4-soft-chord.wav | Multi | 4 | soft | 88 | Soft Chord | 262 Hz@0+180, 330 Hz@0+180, 392 Hz@0+180, 523 Hz@120+180 | 300 | 16163 |
| multi-5-organ-chord.wav | Multi | 5 | organ | 89 | Organ Chord | 196 Hz@0+160, 262 Hz@20+160, 330 Hz@40+160, 392 Hz@60+200 | 260 | 12656 |
| multi-6-kalimba-lick.wav | Multi | 6 | kalimba | 90 | Kalimba Lick | 880 Hz@0+70, 1047 Hz@70+70, 1319 Hz@140+70, 1047 Hz@210+100 | 310 | 19544 |
| multi-7-glass-sparkle.wav | Multi | 7 | glass | 91 | Glass Sparkle | 1568 Hz@0+55, 2093 Hz@55+55, 2637 Hz@110+55, 3136 Hz@165+110 | 275 | 15238 |
| multi-8-bell-road.wav | Multi | 8 | bell | 92 | Bell Road | 392 Hz@0+80, 330 Hz@70+80, 262 Hz@140+80, 196 Hz@210+170 | 380 | 12235 |
| multi-9-wood-alert.wav | Multi | 9 | wood | 93 | Wood Alert | 587 Hz@0+70, 523 Hz@70+70, 440 Hz@140+70, 349 Hz@210+160 | 370 | 19917 |
| alert-0-battery-low.wav | Alert | 0 | soft | 94 | Battery Low | 440 Hz@0+300, 349 Hz@250+300, 262 Hz@500+350, 440 Hz@700+300, 349 Hz@950+300, 262 Hz@1200+150 | 1350 | 27000 |
