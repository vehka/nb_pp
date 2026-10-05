// Copyright 2026 vehka.
// SPDX-License-Identifier: MIT
//
// Host check of every engine through nb_pp::Voice, without SuperCollider:
// a held note must sound and stay finite, and the voice must fall silent
// after the gate closes (the SynthDef frees a synth on silence).
// Exit status is the number of engines that failed.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pp_voice.h"

using nb_pp::kBlockSize;

static const int kBlocksPerSecond = 48000 / kBlockSize;

// Frequency of a block-rendered signal from its rising zero crossings.
static float MeasureFrequency(int engine, float note) {
  void* memory = calloc(1, sizeof(nb_pp::Voice));
  nb_pp::Voice* voice = new (memory) nb_pp::Voice();
  voice->Init(engine);
  nb_pp::VoiceParams vp;
  vp.note = note;
  vp.gate = true;
  vp.level = 1.0f;
  // a plain saw from Virtual Analog: no detune, no second oscillator
  vp.harmonics = 0.5f;
  vp.timbre = 0.0f;
  vp.morph = 1.0f;
  float out[kBlockSize], aux[kBlockSize];
  float previous = 0.0f;
  double first = -1.0, last = -1.0;
  int crossings = 0;
  for (int b = 0; b < 2 * kBlocksPerSecond; ++b) {
    voice->Render(vp, out, aux);
    if (b < kBlocksPerSecond / 2) { previous = out[kBlockSize - 1]; continue; }
    for (std::size_t i = 0; i < kBlockSize; ++i) {
      if (previous < 0.0f && out[i] >= 0.0f) {
        double t = b * kBlockSize + i - out[i] / (out[i] - previous);
        if (first < 0.0) first = t;
        last = t;
        ++crossings;
      }
      previous = out[i];
    }
  }
  voice->~Voice();
  free(memory);
  return crossings > 1 ? 48000.0 * (crossings - 1) / (last - first) : 0.0f;
}

int main() {
  nb_pp::InitUserData();
  printf("sizeof(Voice)=%zu engines=%d\n", sizeof(nb_pp::Voice),
         nb_pp::kNumEngines);

  int failures = 0;
  double worst_init = 0.0;
  for (int e = 0; e < nb_pp::kNumEngines; ++e) {
    void* memory = calloc(1, sizeof(nb_pp::Voice));
    auto t0 = std::chrono::steady_clock::now();
    nb_pp::Voice* voice = new (memory) nb_pp::Voice();
    voice->Init(e);
    double init_us = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - t0).count();
    if (init_us > worst_init) worst_init = init_us;

    nb_pp::VoiceParams vp;
    vp.note = 60.0f;
    float out[kBlockSize], aux[kBlockSize];
    float held_peak = 0.0f, tail_peak = 0.0f;
    bool finite = true;
    // 1 s held, then 9 s released; the tail is the last half second
    const int total = 10 * kBlocksPerSecond;
    t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < total; ++b) {
      vp.gate = b < kBlocksPerSecond;
      voice->Render(vp, out, aux);
      for (std::size_t i = 0; i < kBlockSize; ++i) {
        if (!std::isfinite(out[i]) || !std::isfinite(aux[i])) finite = false;
        float level = std::fmax(std::fabs(out[i]), std::fabs(aux[i]));
        if (b < kBlocksPerSecond) held_peak = std::fmax(held_peak, level);
        if (b >= total - kBlocksPerSecond / 2) {
          tail_peak = std::fmax(tail_peak, level);
        }
      }
    }
    double cpu = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count() / 10.0;
    voice->~Voice();
    free(memory);

    const char* verdict = "ok";
    if (!finite) verdict = "NOT FINITE";
    else if (held_peak < 0.01f) verdict = "SILENT";
    else if (tail_peak > 0.0005f) verdict = "DOES NOT DECAY";
    if (strcmp(verdict, "ok")) ++failures;
    printf("%2d %-26s init=%6.0fus cpu=%5.2f%% held=%.2f tail=%.5f %s\n",
           e, nb_pp::kEngines[e].id, init_us, 100.0 * cpu, held_peak,
           tail_peak, verdict);
  }
  printf("slowest init: %.0f us\n", worst_init);

  float a4 = MeasureFrequency(0, 69.0f);
  bool tuned = std::fabs(a4 - 440.0f) < 0.5f;
  printf("virtual-analog at note 69: %.2f Hz %s\n", a4,
         tuned ? "ok" : "OUT OF TUNE");
  if (!tuned) ++failures;

  printf("%d failure(s)\n", failures);
  return failures;
}
