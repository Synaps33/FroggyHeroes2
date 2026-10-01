# FroggyHeroes2

A working Free Heroes II port for the **GB300** handheld and the **SF2000**
handheld console, built on top of
[sf2000_multicore](https://github.com/Trademarked69/sf2000_multicore).

Free Heroes II never had a native core for either device. This project takes the
upstream engine source, wraps it in a libretro core with a libco-based
coroutine scheduler (no OS threads), and fixes the parts that did not survive
the port: audio, input and screen layout.

Targets: **GB300V2** and **SF2000** (both 320x240, MIPS32r2).

## Quick start

Grab the release archive and copy the files onto your SD card:

```
<SD>/cores/fheroes2/core_87000000              <- gb300/ or sf2000/
<SD>/ROMS/fheroes2/DATA/HEROES2.AGG            <- game/HEROES2.AGG
<SD>/ROMS/fheroes2/MAPS/BROKENA.MP2            <- game/BROKENA.MP2
<SD>/ROMS/fheroes2/GAMES/TUTORIAL.GM1          <- game/TUTORIAL.GM1
<SD>/ROMS/fheroes2/fheroes2.cfg                <- game/fheroes2.cfg
```

Then create the ROM stub that points the menu at the core. The format is
`core;dir;file.`:

```
<SD>/ROMS/fheroes2/fheroes2;HEROES2.AGG.gba
```

containing a single line, no trailing newline:

```
fheroes2;fheroes2;HEROES2.AGG.
```

The two cores are **not interchangeable** — each is linked against its own
firmware layout. GB300 uses `gb300/fheroes2.sf2k`, SF2000 uses
`sf2000/fheroes2.sf2k`.

The included game data is the official **H2DEMO** demo, which is freely
redistributable. It contains one campaign and one map. To play the full game,
replace `DATA/HEROES2.AGG` with your own copy from a legitimate purchase — the
core works with either.

## Controls

The console has no pointer, so the core emulates a mouse: the D-pad moves a
virtual cursor and the buttons act as clicks and keys.

| Button | Action |
| :--- | :--- |
| D-pad | Move cursor (accelerates the longer you hold) |
| **L** / A | Left click — select, confirm, move hero, attack, build |
| **R** / B | Right click — hold to inspect a unit or object |
| START | Return / confirm |
| SELECT | Escape / cancel |
| X | Space — next hero |
| Y | Tab — show/hide minimap |
| **SELECT + START** | Pause menu — save state, load state, reset, exit to FrogUI |

Cursor speed is 2 px/frame, ramping to 4 px after 8 frames held and 7 px after
20 frames held. L and R are pure clicks and do not affect it, so you can hold R
to keep the unit information window open without the cursor drifting away.

**Left click (L)** selects the item under the cursor; on the adventure map it
moves the selected hero, opens objects, and buys troops or buildings in a town.
In battle it attacks the stack under the cursor. End the turn with the hourglass
on the button bar.

**Right click (R)** held over anything shows information about it — full stack
statistics in battle, hero and town details on the map — and closes on release.

### Engine key bindings

The engine already binds these keys; the gamepad exposes them:

| Key | Button | Action |
| :--- | :--- | :--- |
| Return | START | confirm, close messages, press OK |
| Escape | SELECT | cancel, close dialogs; on the map also toggles the button bar |
| Space | X | next hero |
| Tab | Y | show/hide minimap |

Other engine bindings — `1` (control panel), `7` (status bar), `Backspace`
(hero icons), `Alt` (spell book), `m` (movement mode) — are not mapped to any
button. The control panel at the top of the screen is the mouse equivalent of
the first three.

### The minimap

The engine binds the minimap to Tab, but nothing ever generated that key, so the
radar was only reachable by clicking the first button of the top control panel.
Y now sends Tab.

The toggle only works with the sidebar hidden (`pocket pc = 1` in the config).
On a 320x240 screen there is room for either the minimap or the hero icon bar,
status bar and button bar — not both, so enabling the radar hides the others.

### Pause menu

SELECT + START opens the firmware pause menu: save state, load state, reset and
exit. Exit returns to the FrogUI menu.

Two things worth knowing:

- Opening the menu is what snapshots the framebuffer that save states are
  written from. Open it once before relying on save states, or the state file
  will be empty.
- The frontend logs which path it took. If the menu does not appear, look for
  `opening firmware pause menu` versus `exiting to FrogUI menu` in
  `Multicore.log` — the latter means the core was not recognised as
  `fheroes2`, which also happens if you rename the library.

## Configuration

`fheroes2.cfg` sits next to the game data.

| Key | Notes |
| :--- | :--- |
| `videomode` | `320x240` for this screen. Any QVGA value enables the wide layout. |
| `sound` | `1` enables sound effects. |
| `sound volume` | `0`–`10`. |
| `music` | `off` — the demo carries no MIDI data. |
| `animation` | Lower is smoother on this hardware. |
| `pocket pc` | `1` for the full-width map and the working minimap toggle. |

> **Boolean values must be numeric.** Write `sound = 1`, **not** `sound = on`.
> The config parser runs values through `IntParams()` → `String::ToInt()`, and
> `"on"` does not parse — it returns `0`, which this codebase reads as **off**.
> Using `on` silently switches every such feature off. The bundled `fheroes2.cfg`
> is correct; the file is commented to make the reason clear.

The config file also selects the menu you get: with it present the game uses its
QVGA layout and hidden sidebar, without it a different, more cluttered path is
taken. Keep it.

## What this fixes

The original port booted but was unusable. Five problems were addressed.

### 1. No sound at all

SDL's audio init walked its driver table and found nothing, because the only
compiled-in driver (`DUMMY`) reported itself unavailable unless
`SDL_AUDIODRIVER=dummy` was set in the environment — and this SDL has no real
environment (`SDL_getenv` only sees variables set through `SDL_putenv`, and
nothing set an audio one). `Mixer::Init()` therefore left the mixer invalid and
the game silently dropped every sound effect.

`DUMMYAUD_Available()` now always reports available. It is the only driver
compiled in, so it was always the right answer.

Separately, SDL never starts a mixing thread on this platform
(`SDL_SYS_CreateThread` is stubbed out), so nothing would ever invoke the mixer
callback. The core drives it manually instead, emitting exactly one buffer per
video frame through `audio_batch_cb`.

`SDL_LockAudio_Default()` also got a `mixer_lock` null guard, since the mutex is
never created when `SDL_THREADS_DISABLED` is set.

### 2. A config file that silently disabled everything

`Settings::Read()` parses boolean-looking values through `IntParams()` →
`String::ToInt()`. The string `"on"` does not parse and yields `0`, and in this
codebase `0` means **off**. A config written with `sound = on` / `pocket pc = on`
therefore switched every one of those features *off*.

### 3. Input

L and R were doubling cursor speed, which made it impossible to hold R for unit
inspection. They now act purely as mouse clicks, and cursor speed ramps by hold
duration instead.

### 4. Screen layout

The adventure map reserved a fixed ~150px sidebar for radar, icons, buttons and
status, squeezing the playfield down to about 170px of a 320px screen. At QVGA
the engine switches to its hidden-interface layout, so the map now fills the
screen.

### 5. Minimap

Bound to Tab in the engine, reachable only by clicking the top control panel.
Now on Y — see [Controls](#controls).

### 6. Pause menu and save states

SELECT + START exited straight to the FrogUI menu on every core: the call to the
firmware pause menu had been dropped, leaving the hook to jump to the menu
launcher. Restored for this core.

That call is also where the framebuffer copy is taken, which is what save states
are written from. Without it `state_framebuffer` stayed `NULL`, and both
`wrap_state_save` and the autosave path silently skipped saving because they
guard on `if (state_framebuffer)` — so autosave and the L+R+X save-state hotkey
wrote nothing.

Other cores keep the previous behaviour, since the J2ME core unwinds its Java
task on SELECT + START before firmware reaches the hook.

## Building

From the `sf2000_multicore` checkout, one binary per device:

```sh
make FROGGY_TYPE=GB300V2 CONSOLE=fheroes2 CORE=cores/fheroes2
make FROGGY_TYPE=SF2000  CONSOLE=fheroes2 CORE=cores/fheroes2
```

## Credits and licensing

This core bundles code from three projects with different licences:

| Component | Source | Licence |
| :--- | :--- | :--- |
| Free Heroes II engine 0.5 (r1898) | fheroes2 project, Andrey Afletdinov | **GPL-2.0** |
| SDL 1.2.15 | SDL project | LGPL-2.1 |
| sf2000_multicore frontend | [Trademarked69/sf2000_multicore](https://github.com/Trademarked69/sf2000_multicore) (forked from [madcock](https://github.com/madcock/sf2000_multicore), from [kobily](https://gitlab.com/kobily/sf2000_multicore)) | ISC |

Because the Free Heroes II engine is GPL-2.0, these binaries are distributed
under the **GNU General Public License v2.0**, which is the most restrictive
term in the combination. The full GPL-2.0 text is in [`LICENSE`](LICENSE).

Attribution required by the licences:

- The `sf2000_multicore` frontend is copyright © 2024 the authors of
  gitlab.com/kobily, github.com/bnister, github.com/tzubertowski and the
  contributors of madcock/sf2000_multicore.
- SDL 1.2.15 is copyright © Sam Lantinga and the SDL contributors.

*Heroes of Might and Magic II* was developed by New World Computing and published
by 3DO. The bundled **H2DEMO** is freely redistributable; the full game is not
included and must be supplied by you from a legitimate copy.