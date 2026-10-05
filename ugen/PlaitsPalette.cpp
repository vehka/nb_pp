// Copyright 2026 vehka.
// SPDX-License-Identifier: MIT
//
// PlaitsPalette: a SuperCollider UGen playing one Plaits Palette engine.
//
// Inputs: engine (read once, when the synth starts), pitch (MIDI note),
// harm, timbre, morph, macro, gate, level, decay, lpg_colour, stereo.
// Outputs: out and aux, or left and right when stereo is on.

#include "SC_PlugIn.h"

#include "pp_voice.h"

static InterfaceTable* ft;

enum Inputs {
  kInEngine, kInPitch, kInHarm, kInTimbre, kInMorph, kInMacro,
  kInGate, kInLevel, kInDecay, kInLpgColour, kInStereo
};

struct PlaitsPalette : public Unit {
  nb_pp::Voice* voice;
  // Engines render kBlockSize (12) frames at a time and scsynth asks for 64,
  // so one rendered block is handed out across calls.
  float out_block[nb_pp::kBlockSize];
  float aux_block[nb_pp::kBlockSize];
  int block_pos;
  int block_fill;
};

static inline float Unipolar(float x) {
  return sc_clip(x, 0.0f, 1.0f);
}

static void PlaitsPalette_next(PlaitsPalette* unit, int inNumSamples) {
  float* out = OUT(0);
  float* aux = OUT(1);

  nb_pp::VoiceParams vp;
  vp.note = sc_clip(IN0(kInPitch), 0.0f, 127.0f);
  vp.harmonics = Unipolar(IN0(kInHarm));
  vp.timbre = Unipolar(IN0(kInTimbre));
  vp.morph = Unipolar(IN0(kInMorph));
  vp.macro = Unipolar(IN0(kInMacro));
  vp.gate = IN0(kInGate) > 0.0f;
  vp.level = Unipolar(IN0(kInLevel));
  vp.decay = Unipolar(IN0(kInDecay));
  vp.lpg_colour = Unipolar(IN0(kInLpgColour));
  vp.stereo = IN0(kInStereo) > 0.0f;

  int produced = 0;
  while (produced < inNumSamples) {
    if (unit->block_fill == 0) {
      unit->voice->Render(vp, unit->out_block, unit->aux_block);
      unit->block_fill = nb_pp::kBlockSize;
      unit->block_pos = 0;
    }
    int take = sc_min(inNumSamples - produced, unit->block_fill);
    for (int i = 0; i < take; ++i) {
      out[produced + i] = unit->out_block[unit->block_pos + i];
      aux[produced + i] = unit->aux_block[unit->block_pos + i];
    }
    unit->block_pos += take;
    unit->block_fill -= take;
    produced += take;
  }
}

static void PlaitsPalette_silent(PlaitsPalette* unit, int inNumSamples) {
  ClearUnitOutputs(unit, inNumSamples);
}

static void PlaitsPalette_Ctor(PlaitsPalette* unit) {
  unit->voice = nullptr;
  unit->block_pos = 0;
  unit->block_fill = 0;
  SETCALC(PlaitsPalette_silent);
  ClearUnitOutputs(unit, 1);

  // The engines are written for 48 kHz and have no resampler here.
  static bool rate_warned = false;
  if (!rate_warned && SAMPLERATE != 48000.0) {
    rate_warned = true;
    Print("PlaitsPalette: engines assume 48000 Hz, server runs at %g Hz; "
          "pitch and timbre will be off\n", SAMPLERATE);
  }

  void* memory = RTAlloc(unit->mWorld, sizeof(nb_pp::Voice));
  if (!memory) {
    Print("PlaitsPalette: out of real-time memory\n");
    return;
  }
  memset(memory, 0, sizeof(nb_pp::Voice));
  unit->voice = new (memory) nb_pp::Voice();
  int engine = static_cast<int>(IN0(kInEngine));
  if (!unit->voice->Init(engine)) {
    Print("PlaitsPalette: no engine %d (0..%d)\n", engine,
          nb_pp::kNumEngines - 1);
    unit->voice->~Voice();
    RTFree(unit->mWorld, memory);
    unit->voice = nullptr;
    return;
  }
  SETCALC(PlaitsPalette_next);
}

static void PlaitsPalette_Dtor(PlaitsPalette* unit) {
  if (unit->voice) {
    unit->voice->~Voice();
    RTFree(unit->mWorld, unit->voice);
  }
}

PluginLoad(PlaitsPalette) {
  ft = inTable;
  nb_pp::InitUserData();
  DefineDtorUnit(PlaitsPalette);
}
