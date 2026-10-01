# FroggyHeroes2

A working Free Heroes II port for the **GB300** handheld, built on top of
[sf2000_multicore](https://github.com/Trademarked69/sf2000_multicore).

Free Heroes II never had a native SF2000/GB300 core. This project takes the
upstream engine source, wraps it in a libretro core with a libco-based
coroutine scheduler (no OS threads), and fixes the parts that did not survive
the port: audio, input and screen layout.

Target: **GB300V2** (ST7789V, 320x240, MIPS32r2).

## Quick start

Grab the release assets and copy them onto your SD card:

```
<SD>/cores/fheroes2/core_87000000              <- fheroes2_gb300v2.sf2k
<SD>/ROMS/fheroes2/DATA/HEROES2.AGG            <- HEROES2.AGG
<SD>/ROMS/fheroes2/MAPS/BROKENA.MP2            <- BROKENA.MP2
<SD>/ROMS/fheroes2/GAMES/TUTORIAL.GM1          <- TUTORIAL.GM1
<SD>/ROMS/fheroes2/fheroes2.cfg                <- fheroes2.cfg
```

Then create the ROM stub that points the menu at the core. The format is
`core;dir;file.`:

```
<SD>/ROMS/fheroes2/fheroes2;HEROES2.AGG.gba
```

containing a single line:

```
fheroes2;fheroes2;HEROES2.AGG.
```

The included game data is the official **H2DEMO** demo, which is freely
redistributable. It contains one campaign and one map. To play the full game,
replace `DATA/HEROES2.AGG` with your own copy from a legitimate purchase — the
core works with either.

## Configuration

`fheroes2.cfg` sits next to the game data.

| Key | Notes |
| :--- | :--- |
| `videomode` | `320x240` for this device. Any QVGA value enables the wide layout. |
| `sound` | `1` enables sound effects. |
| `sound volume` | `0`–`10`. |
| `music` | `off` — the demo has no MIDI data. |
| `animation` | Lower is smoother on this hardware. |
| `pocket pc` | `1` for the full-width map. |

> **Boolean values must be numeric.** Write `sound = 1`, **not** `sound = on`.
> The config parser runs values through `IntParams()` → `String::ToInt()`, and
> `"on"` does not parse — it yields `0`, which this codebase reads as **off**.
> Writing `on` silently switches every one of those features *off*.

## Controls

| Button | Action |
| :--- | :--- |
| D-pad | Move cursor (accelerates while held) |
| **L** / A | Left click — select, confirm, move hero, attack, build |
| **R** / B | Right click — hold to inspect a unit or object |
| START | Return / confirm |
| SELECT | Escape / cancel |
| X | Space |
| Y | H (next hero) |

Holding L or R does not double cursor speed, so R can be held to keep the unit
info window open.

## What this fixes

The original port booted but was unusable. Four problems were addressed.

### 1. No sound at all

SDL's audio init walked the driver bootstrap table and found nothing, because
the only compiled-in driver (`DUMMY`) reported itself unavailable unless
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

Mouse emulation via D-pad, with L mapped to left click and R to right click, and
cursor acceleration ramped by hold duration.

### 4. Screen layout

The adventure map originally reserved a fixed ~150px sidebar for radar, icons,
buttons and status, squeezing the playfield down to about 170px of a 320px
screen.

At QVGA resolution the engine already enables `GLOBAL_POCKETPC` and
`GAME_HIDE_INTERFACE` on its own, which collapses that sidebar and widens the
map to the full screen. The only requirement is a `fheroes2.cfg` with
`videomode = 320x240`.

## Building

From the `sf2000_multicore` checkout:

```sh
make FROGGY_TYPE=GB300V2 CONSOLE=fheroes2 CORE=cores/fheroes2
```

## Credits and licensing

This core bundles code from three projects with different licences:

| Component | Source | Licence |
| :--- | :--- | :--- |
| Free Heroes II engine 0.5 (r1898) | fheroes2 project, Andrey Afletdinov | **GPL-2.0** |
| SDL 1.2.15 | SDL project | LGPL-2.1 |
| sf2000_multicore frontend | [Trademarked69/sf2000_multicore](https://github.com/Trademarked69/sf2000_multicore) (forked from [madcock](https://github.com/madcock/sf2000_multicore), from [kobily](https://gitlab.com/kobily/sf2000_multicore)) | ISC |

Because the Free Heroes II engine is GPL-2.0, this binary is distributed under
the **GNU General Public License v2.0**, which is the most restrictive term in
the combination. The full GPL-2.0 text is in [`LICENSE`](LICENSE).

Attribution required by the licences:

- The `sf2000_multicore` frontend is copyright © 2024 the authors of
  gitlab.com/kobily, github.com/bnister, github.com/tzubertowski and the
  contributors of madcock/sf2000_multicore.
- SDL 1.2.15 is copyright © Sam Lantinga and the SDL contributors.

*Heroes of Might and Magic II* was developed by New World Computing and published
by 3DO. The bundled **H2DEMO** demo is freely redistributable; the full game is
not included and must be supplied by you from a legitimate copy.