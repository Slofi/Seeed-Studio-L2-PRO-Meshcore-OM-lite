# OM Lite sound library (EST-361)

Every file is the exact waveform the device streams (16 kHz, 16-bit mono, the same sound_synth.cpp). `id` is the shared-library id stored in NVS; `n` is the sound's index in its home event's own set. Any sound may be chosen for any notification.

| file | event | n | id | family | name | notes (Hz@at+dur ms) | ms | peak |
|---|---|---:|---:|---|---|---|---:|---:|
| boot-0-bell-tk-tk.wav | boot | 0 | 0 | Bells | Bell TK-TK | 2349 Hz@0+60, 2794 Hz@115+70 | 205 | 16659 |
| boot-1-bell-pair.wav | boot | 1 | 1 | Bells | Bell Pair | 784 Hz@0+120, 1047 Hz@80+150 | 260 | 17062 |
| boot-2-chime-up.wav | boot | 2 | 2 | Chimes | Chime Up | 1047 Hz@0+110, 1319 Hz@90+140 | 240 | 13483 |
| boot-3-soft-rise.wav | boot | 3 | 3 | Soft | Soft Rise | 440 Hz@0+90, 554 Hz@70+120 | 220 | 14604 |
| boot-4-marimba-up.wav | boot | 4 | 4 | Marimba | Marimba Up | 880 Hz@0+100, 1319 Hz@70+120 | 210 | 15184 |
| boot-5-chime-arp.wav | boot | 5 | 5 | Chimes | Chime Arp | 523 Hz@0+70, 784 Hz@70+70, 1047 Hz@140+100 | 260 | 16888 |
| message-0-soft-hi.wav | message | 0 | 6 | Soft | Soft Hi | 880 Hz@0+100, 698 Hz@90+130 | 250 | 12395 |
| message-1-marimba-hi.wav | message | 1 | 7 | Marimba | Marimba Hi | 1047 Hz@0+90, 784 Hz@80+120 | 230 | 13335 |
| message-2-bell-hi.wav | message | 2 | 8 | Bells | Bell Hi | 1397 Hz@0+90, 1047 Hz@80+120 | 230 | 13128 |
| message-3-soft-high-fifth.wav | message | 3 | 9 | Soft | Soft High Fifth | 988 Hz@0+100, 784 Hz@90+130 | 250 | 12323 |
| message-4-marimba-high-fifth.wav | message | 4 | 10 | Marimba | Marimba High Fifth | 1176 Hz@0+90, 880 Hz@80+120 | 230 | 14579 |
| message-5-bell-high-fifth.wav | message | 5 | 11 | Bells | Bell High Fifth | 1568 Hz@0+90, 1176 Hz@80+130 | 240 | 13065 |
| dm-0-soft-warm.wav | dm | 0 | 12 | Soft | Soft Warm | 349 Hz@0+140, 440 Hz@100+160 | 300 | 13125 |
| dm-1-soft-deep.wav | dm | 1 | 13 | Soft | Soft Deep | 294 Hz@0+150, 349 Hz@110+170 | 300 | 16711 |
| dm-2-soft-basso.wav | dm | 2 | 14 | Soft | Soft Basso | 196 Hz@0+160, 294 Hz@120+180 | 340 | 13490 |
| dm-3-soft-low-fifth.wav | dm | 3 | 15 | Soft | Soft Low Fifth | 262 Hz@0+150, 392 Hz@100+170 | 330 | 17136 |
| dm-4-soft-low-third.wav | dm | 4 | 16 | Soft | Soft Low Third | 220 Hz@0+140, 262 Hz@100+160 | 300 | 13328 |
| dm-5-soft-dark.wav | dm | 5 | 17 | Soft | Soft Dark | 165 Hz@0+170, 220 Hz@120+180 | 360 | 15559 |
| mention-0-chime-bright.wav | mention | 0 | 18 | Chimes | Chime Bright | 1319 Hz@0+80, 1760 Hz@70+110 | 220 | 11490 |
| mention-1-bell-high.wav | mention | 1 | 19 | Bells | Bell High | 1568 Hz@0+90, 2093 Hz@80+120 | 240 | 12751 |
| mention-2-chime-glass.wav | mention | 2 | 20 | Chimes | Chime Glass | 1760 Hz@0+70, 2637 Hz@65+100 | 200 | 11312 |
| mention-3-chime-top.wav | mention | 3 | 21 | Chimes | Chime Top | 2093 Hz@0+80, 2637 Hz@70+110 | 220 | 11888 |
| mention-4-bell-very-high.wav | mention | 4 | 22 | Bells | Bell Very High | 2093 Hz@0+90, 2794 Hz@80+120 | 240 | 11613 |
| mention-5-glass-trio.wav | mention | 5 | 23 | Chimes | Glass Trio | 1568 Hz@0+65, 2093 Hz@65+65, 2637 Hz@130+110 | 280 | 16274 |
| sent-0-tick.wav | sent | 0 | 24 | Ticks | Tick | 1319 Hz@0+45 | 80 | 8736 |
| sent-1-blip.wav | sent | 1 | 25 | Ticks | Blip | 1568 Hz@0+50 | 90 | 7713 |
| sent-2-tock.wav | sent | 2 | 26 | Ticks | Tock | 1047 Hz@0+55 | 95 | 7831 |
| sent-3-tick-tock.wav | sent | 3 | 27 | Ticks | Tick Tock | 1319 Hz@0+45, 1047 Hz@50+55 | 120 | 8736 |
| sent-4-tick-tick.wav | sent | 4 | 28 | Ticks | Tick Tick | 1319 Hz@0+35, 1319 Hz@45+35 | 90 | 7079 |
| sent-5-blip-tock.wav | sent | 5 | 29 | Ticks | Blip Tock | 1568 Hz@0+40, 1047 Hz@50+50 | 110 | 7831 |
| error-0-error-low.wav | error | 0 | 30 | Alerts | Error Low | 440 Hz@0+120, 349 Hz@105+160 | 300 | 12332 |
| error-1-error-warn.wav | error | 1 | 31 | Alerts | Error Warn | 330 Hz@0+100, 262 Hz@110+170 | 310 | 20957 |
| error-2-alert-triple.wav | error | 2 | 32 | Alerts | Alert Triple | 587 Hz@0+70, 523 Hz@70+70, 440 Hz@140+130 | 300 | 20048 |
| error-3-error-deep.wav | error | 3 | 33 | Alerts | Error Deep | 262 Hz@0+110, 196 Hz@100+180 | 320 | 12268 |
| error-4-alert-double.wav | error | 4 | 34 | Alerts | Alert Double | 880 Hz@0+60, 880 Hz@90+60 | 220 | 19481 |
| error-5-bell-descend.wav | error | 5 | 35 | Bells | Bell Descend | 392 Hz@0+110, 294 Hz@100+160 | 320 | 12173 |
| newnode-0-soft-lo.wav | newnode | 0 | 36 | Soft | Soft Lo | 494 Hz@0+100, 392 Hz@90+130 | 250 | 12304 |
| newnode-1-marimba-lo.wav | newnode | 1 | 37 | Marimba | Marimba Lo | 587 Hz@0+90, 440 Hz@80+120 | 230 | 13179 |
| newnode-2-bell-lo.wav | newnode | 2 | 38 | Bells | Bell Lo | 784 Hz@0+90, 587 Hz@80+120 | 230 | 12789 |
| newnode-3-soft-node-low-fifth.wav | newnode | 3 | 39 | Soft | Soft Node Low Fifth | 415 Hz@0+100, 330 Hz@90+130 | 250 | 12470 |
| newnode-4-marimba-low-fifth.wav | newnode | 4 | 40 | Marimba | Marimba Low Fifth | 523 Hz@0+90, 392 Hz@80+120 | 230 | 13172 |
| newnode-5-bell-low-fifth.wav | newnode | 5 | 41 | Bells | Bell Low Fifth | 698 Hz@0+90, 523 Hz@80+130 | 240 | 13256 |
| battery-0-battery-low.wav | battery | 0 | 42 | Alerts | Battery Low | 440 Hz@0+300, 349 Hz@250+300, 262 Hz@500+350, 440 Hz@700+300, 349 Hz@950+300, 262 Hz@1200+150 | 1350 | 27000 |
| battery-1-battery-sigh.wav | battery | 1 | 43 | Alerts | Battery Sigh | 349 Hz@0+300, 294 Hz@250+300, 220 Hz@500+300, 349 Hz@700+300, 294 Hz@950+300, 220 Hz@1200+100 | 1300 | 27000 |
| battery-2-battery-bell.wav | battery | 2 | 44 | Alerts | Battery Bell | 392 Hz@0+320, 330 Hz@270+320, 262 Hz@540+380, 392 Hz@750+320, 330 Hz@1020+320, 262 Hz@1290+110 | 1400 | 27000 |
| battery-3-battery-deep.wav | battery | 3 | 45 | Alerts | Battery Deep | 294 Hz@0+280, 262 Hz@230+280, 196 Hz@460+330, 294 Hz@650+280, 262 Hz@880+280, 196 Hz@1110+140 | 1250 | 27000 |
| battery-4-battery-alarm.wav | battery | 4 | 46 | Alerts | Battery Alarm | 523 Hz@0+280, 440 Hz@230+280, 349 Hz@460+330, 523 Hz@650+280, 440 Hz@880+280, 349 Hz@1110+190 | 1300 | 27000 |
| battery-5-battery-pulse.wav | battery | 5 | 47 | Alerts | Battery Pulse | 440 Hz@0+300, 392 Hz@260+300, 330 Hz@520+330, 440 Hz@720+300, 392 Hz@980+300, 330 Hz@1240+110 | 1350 | 27000 |
| touch-0-tap-tick.wav | touch | 0 | 48 | Ticks | Tap Tick | 1500 Hz@0+30 | 55 | 5425 |
| touch-1-tap-tock.wav | touch | 1 | 49 | Ticks | Tap Tock | 900 Hz@0+35 | 60 | 7121 |
| touch-2-tap-click.wav | touch | 2 | 50 | Ticks | Tap Click | 2200 Hz@0+18 | 35 | 2399 |
| touch-3-tap-tik.wav | touch | 3 | 51 | Ticks | Tap Tik | 1000 Hz@0+30 | 50 | 5469 |
| touch-4-tap-snap.wav | touch | 4 | 52 | Ticks | Tap Snap | 1800 Hz@0+20 | 45 | 2849 |
| touch-5-tap-double.wav | touch | 5 | 53 | Ticks | Tap Double | 900 Hz@0+22, 900 Hz@38+22 | 80 | 3747 |
