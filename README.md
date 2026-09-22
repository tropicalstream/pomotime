# X3 Timer for MemoMind glasses

A port of [x3timer](../x3timer) (RayNeo X3 Pro) to the MemoMind
[Plugin Open Platform](https://github.com/memomind-open/plugin-open-platform):
a glanceable dual-mode timer — **Pomodoro** focus mode plus three athletic
modalities (**HIIT/Tabata · EMOM · AMRAP**) — as a native glasses `.gmp` plugin.

The widget occupies < 20% of the 600×350 panel; everything else stays black
(transparent on the waveguide). A thin edge pulse echoes the phase so it reads
from the corner of the eye. A running Pomodoro shrinks to a corner chip after
5 s of no interaction so the view stays clear for reading or coding; any
interaction or a phase change pops it back.

Runs entirely on the glasses: no phone plugin, no Bluetooth, 7.6 KiB flash,
680 B static RAM.

## Controls

Glasses alone. The single button does all in-session work; **head motion is
ignored while a session is live**, so looking down at a water bottle or nodding
along to music can never touch the clock.

| Input | Idle / finished | Running / paused |
|---|---|---|
| **Click** | Start | Pause / resume |
| **Double-click** | Reset | Running: Pomodoro **extend +10 min** (flow protection) · HIIT/EMOM: skip interval · AMRAP: +1 lap. Paused: reset |
| **Hold** | Next program (Pomodoro → HIIT → EMOM → AMRAP) | Next program, guarded (see below) |
| **Head left / right** | Previous / next program | *ignored* |
| **Very long hold**, BACK, HOME | Exit | Exit |

To reset a running session: pause, then double-click.

With a paired accessory ring: **LEFT / RIGHT** program, **UP** extend/skip/lap,
**DOWN** pin/unpin the compact corner chip.

**Accidental-switch guard.** While a session is running or paused, a program
switch first shows *… ? REPEAT TO CONFIRM* with a 5-second draining
bar. Repeat the gesture within the window to confirm; otherwise it cancels and
the session keeps running. Idle or finished, switches are immediate.

## What changed from the RayNeo version

The display is 16-level monochrome, so colour coding became **cadence**: work
phases get a brisk edge pulse, rest/break phases a slow breathing pulse, the
final five seconds of an athletic interval a hard flash, and a paused clock
blinks its colon. Phase names in the overline carry the rest.

The glasses expose no audio, no persistent storage and only two Host fonts, so:

- Countdown beeps, GO tones and fanfares are gone (visual cues only).
- Task binding and the 7-day sparkline are gone; today's pomodoro count is
  kept for the session.
- The time readout is seven-segment digits built from LVGL rectangles, so it
  scales with the panel independent of Host font sizes.
- Particle bursts became a brief white flash of the panel border.

## Layout

```
glass/x3timer/manifest.json   plugin identity
glass/x3timer/x3timer.c       the whole plugin (engine + LVGL rendering)
glass/tests/host_test.c       host-compiled engine test against a stub Host
glass/tests/run.sh            builds and runs it
```

## Build

Requires the Plugin Open Platform checkout at `~/Projects/plugin-open-platform`
with its `.venv` (Homebrew Python is externally managed, so the SDK's own pip
bootstrap fails without one):

```sh
cd ~/Projects/plugin-open-platform/GlassSDK
../.venv/bin/python build.py build --project ~/Projects/x3timer-memomind/glass/x3timer
```

Output: `glass/x3timer/.build/x3timer/x3timer.gmp` plus the `.review.json` /
`.review-source.enc` sidecars that Studio packages for submission.

## Test

```sh
glass/tests/run.sh
```

Drives every program through the plugin callbacks with a fake clock: pomodoro
break / long-break sequencing, flow-extend, auto-minimize, the switch guard,
HIIT round/rest ordering with skip, EMOM rounds, AMRAP count-up with laps,
suspend/resume wall-time credit, and exit.

## Run in Studio

Desktop Studio only discovers plugins that live as real directories under
`GlassSDK/examples/` (symlinks are skipped), so:

```sh
glass/sync-to-sdk.sh
```

copies the sources there and builds them. Then in **MemoMind Plugin Studio**:
**Import workspace** → the `plugin-open-platform` folder, and choose
**X3 Timer** in the glass-plugin dropdown (it sorts near the top). After a
rebuild, **Refresh repository** reloads it. The virtual 600×350 display takes
the primary button (click / double / hold 1 s) and head motion from the
joystick — left/right browse programs while idle; anything else is ignored.

Verified in Studio 0.1.0: 28–29 FPS, 7.8 KiB ROM, 692 B RAM.
