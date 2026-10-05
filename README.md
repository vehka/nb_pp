# nb_pp

[Plaits Palette](https://rubato.audio/plaits-palette/) engines as
[nb](https://github.com/sixolet/nb) voices for norns: the 96 synthesis models
of Rubato Audio's Plaits firmware, four voices (`palette 1` to `palette 4`),
each polyphonic.

This is an early version. It has been tested on desktop norns only, not yet
on a norns or shield.

## Install

1. Put this repository in `dust/code/nb_pp`, with its submodules:

   ```sh
   cd ~/dust/code
   git clone --recurse-submodules https://github.com/vehka/nb_pp
   ```

2. Build and install the SuperCollider UGen. This needs `cmake`, a C++17
   compiler and the SuperCollider plugin headers (or a SuperCollider source
   tree, given as `SC_PATH=...`):

   ```sh
   ~/dust/code/nb_pp/ugen/build.sh install
   ```

   It compiles about 120 files, which takes a while on a norns.

3. Enable the mod in SYSTEM > MODS and restart.

Without the UGen the mod still loads; the voices are silent and the mod menu
says so.

## Playing

Pick `palette 1`..`4` as the voice in any script that uses nb. The voice's
params:

- **style**: `poly`, `mono` (one note that moves, with **slew**) or `perc`
  (every note is a short trigger, note length is ignored).
- **model**: the engine. The next four params are renamed after what the
  model does with them: harmonics, timbre, morph and the Palette's fourth
  macro (0.5 is the model's stock sound).
- **decay**, **lpg color**: the Plaits low-pass gate. A held note stays open;
  decay sets how it closes after release (or after the trigger, in `perc`).
  Drums and resonators have their own envelope and ignore it.
- **stereo**: models render a left/right pair instead of out/aux.
  **aux mix** blends the aux output in when stereo is off.
- **amp**, **gain** (into a soft clipper), **pan**, **send a/b** (for
  sixolet's fx mod).

Velocity is the Plaits level/accent: it opens the gate further and brighter.
Turning a param changes sounding notes too.

## Choosing engines

SYSTEM > MODS > NB_PP lists the models by family. Models turned off there are
skipped when turning the **model** param.

- E2 scroll, E3 jump to the next family
- K3 toggle a model; on a family line, the whole family
- K2 back

The choice is saved in `dust/data/nb_pp/prefs.data` and applies to all four
voices. Saved param sets are not affected: a pset that uses a model that is
off still plays it.

## Not here (yet)

The custom wavetables, wave terrains, chord tables and speech banks the
Palette web builder makes; the Drift/Step randomizer; Sync In and
through-zero FM; an attack time. Chiptune plays as a plain gated oscillator,
without its own arpeggio envelope.

## Development

- `ugen/eurorack` is the Palette source (a submodule of
  [lylepmills/eurorack](https://github.com/lylepmills/eurorack)).
  `ugen/pp_voice.h` wraps one engine, `ugen/PlaitsPalette.cpp` is the UGen.
- After updating the submodule: `python3 ugen/gen_engines.py` (rewrites
  `lib/engines.lua`, keeping the order so saved params stay valid), then
  rebuild and reinstall the UGen. The Lua list and the UGen must come from
  the same submodule revision.
- `ugen/build/pp_host_test` plays every engine without SuperCollider and
  checks that it sounds, stays finite and falls silent after release.

## License

MIT, see `LICENSE`. The Plaits Palette engines and Mutable Instruments'
Plaits and stmlib code in `ugen/eurorack` are MIT licensed by their authors
(Emilie Gillet, Lyle Mills / Rubato Audio and the Palette contributors);
`ugen/pp_voice.h` is adapted from that repository's Seed3 voice.
