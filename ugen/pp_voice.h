// Copyright 2026 vehka.
// SPDX-License-Identifier: MIT
//
// One Plaits Palette engine plus the module's output stage (registered gain
// or limiter) and its low-pass gate, in float.
//
// Adapted from alt_firmwares/palette_seed3/src/fw_voice.h in
// https://github.com/lylepmills/eurorack (MIT, Lyle Mills / Rubato Audio),
// which carries the same decay/colour law as plaits::Voice. Differences: the
// engine is chosen once at Init, pitch and level follow their inputs while a
// note is held, and nothing here is static, so many voices can live in memory
// handed out by the host.

#ifndef NB_PP_VOICE_H_
#define NB_PP_VOICE_H_

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/limiter.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/envelope.h"
#include "plaits/dsp/fx/low_pass_gate.h"
#include "plaits/resources.h"

// PALETTE_ENGINES(X) and PALETTE_ENGINE_COUNT, generated in the eurorack
// repository from the Palette catalog.
#include "engine_table.h"

namespace nb_pp {

// The names engine_table.h uses for an engine's built-in data.
enum UserData { kNone = 0, kFmBankA, kFmBankB, kFmBankC, kWaveTerrain };

struct EngineDesc {
  const char* id;
  const char* name;
  std::size_t size;
  plaits::Engine* (*construct)(void* storage);
  void (*destroy)(plaits::Engine* engine);
  float out_gain;
  float aux_gain;
  bool already_enveloped;
  UserData user_data;
};

template <class T>
plaits::Engine* ConstructEngine(void* storage) { return new (storage) T(); }
template <class T>
void DestroyEngine(plaits::Engine* engine) { static_cast<T*>(engine)->~T(); }

#define NB_PP_X_DESC(i, id, name, Class, og, ag, env, ud) \
  { id, name, sizeof(Class), &ConstructEngine<Class>, &DestroyEngine<Class>, \
    og, ag, env, ud },
static const EngineDesc kEngines[] = { PALETTE_ENGINES(NB_PP_X_DESC) };
#undef NB_PP_X_DESC

constexpr std::size_t MaxOf(std::size_t a, std::size_t b) {
  return a > b ? a : b;
}
#define NB_PP_X_SIZE(i, id, name, Class, og, ag, env, ud) MaxOf(sizeof(Class),
#define NB_PP_X_CLOSE(i, id, name, Class, og, ag, env, ud) )
constexpr std::size_t kMaxEngineSize =
    PALETTE_ENGINES(NB_PP_X_SIZE) 0 PALETTE_ENGINES(NB_PP_X_CLOSE);
#undef NB_PP_X_SIZE
#undef NB_PP_X_CLOSE

constexpr int kNumEngines = PALETTE_ENGINE_COUNT;
constexpr std::size_t kArenaBytes = 16 * 1024;  // what the module gives one
constexpr std::size_t kBlockSize = plaits::kBlockSize;  // 12 frames

// The 64x64 int8 surface Wave Terrain scans when it has no user bank: the one
// in plaits_test.cc. Built by InitUserData(), which must run once before any
// voice is made (and off the audio thread).
inline int8_t* WaveTerrainStorage() {
  static int8_t terrain[4096];
  return terrain;
}

inline void InitUserData() {
  int8_t* terrain = WaveTerrainStorage();
  for (int x = 0; x < 64; ++x) {
    for (int y = 0; y < 64; ++y) {
      terrain[x + 64 * y] =
          static_cast<int8_t>(127.0f * sinf((x * y) / 300.0f));
    }
  }
}

inline const uint8_t* BuiltInUserData(UserData kind) {
  switch (kind) {
    case kFmBankA: return plaits::fm_patches_table[0];
    case kFmBankB: return plaits::fm_patches_table[1];
    case kFmBankC: return plaits::fm_patches_table[2];
    case kWaveTerrain:
      return reinterpret_cast<const uint8_t*>(WaveTerrainStorage());
    case kNone: break;
  }
  return nullptr;
}

struct VoiceParams {
  float note = 48.0f;  // MIDI note number, fractional
  float harmonics = 0.5f;
  float timbre = 0.5f;
  float morph = 0.5f;
  float macro = 0.5f;  // the fourth control; 0.5 is an engine's stock sound
  float level = 0.8f;  // accent, and how far the gate opens
  float decay = 0.5f;
  float lpg_colour = 0.5f;
  bool gate = false;
  bool stereo = false;  // out/aux become left/right
};

class Voice {
 public:
  Voice() {}
  ~Voice() { Destroy(); }

  // The memory this object lives in must be zeroed first: engines expect the
  // arena and their own storage to start out as .bss does on the module.
  // Returns false for a bad index.
  bool Init(int index) {
    engine_ = nullptr;
    engine_index_ = -1;
    if (index < 0 || index >= kNumEngines) return false;
    const EngineDesc& d = kEngines[index];
    allocator_.Init(arena_, kArenaBytes);
    engine_ = d.construct(engine_storage_);
    engine_index_ = index;
    engine_->Init(&allocator_);
    engine_->LoadUserData(BuiltInUserData(d.user_data));
    engine_->Reset();
    out_limiter_.Init();
    aux_limiter_.Init();
    lpg_envelope_.Init();
    out_lpg_.Init();
    aux_lpg_.Init();
    gate_ = false;
    // Chiptune, given a trigger, plays through an envelope of its own that
    // plaits::Voice sets up from the TIMBRE attenuverter; left alone it
    // never closes. Without a trigger it is an ordinary gated oscillator.
    untriggered_ = !strcmp(d.id, "chiptune");
    return true;
  }

  void Destroy() {
    if (engine_) {
      kEngines[engine_index_].destroy(engine_);
      engine_ = nullptr;
      engine_index_ = -1;
    }
  }

  int engine_index() const { return engine_index_; }
  bool stereo_capable() const { return engine_ && engine_->stereo_capable(); }

  // Renders one block of kBlockSize frames.
  void Render(const VoiceParams& vp, float* out, float* aux) {
    const std::size_t size = kBlockSize;
    if (!engine_) {
      for (std::size_t i = 0; i < size; ++i) { out[i] = 0.0f; aux[i] = 0.0f; }
      return;
    }
    const EngineDesc& d = kEngines[engine_index_];
    plaits::EngineParameters p;
    p.note = vp.note;
    p.harmonics = vp.harmonics;
    p.timbre = vp.timbre;
    p.morph = vp.morph;
    p.macro = vp.macro;
    p.chord_set_option = 0;
    p.stereo = vp.stereo && engine_->stereo_capable();
    p.accent = vp.level;
    if (untriggered_) {
      p.trigger = plaits::TRIGGER_UNPATCHED;
    } else {
      p.trigger = vp.gate ? plaits::TRIGGER_HIGH : plaits::TRIGGER_LOW;
      if (vp.gate && !gate_) p.trigger |= plaits::TRIGGER_RISING_EDGE;
    }
    gate_ = vp.gate;

    bool enveloped = d.already_enveloped;
    engine_->Render(p, out, aux, size, &enveloped);

    ApplyGain(d.out_gain, &out_limiter_, out, size);
    ApplyGain(d.aux_gain, &aux_limiter_, aux, size);

    // plaits::Voice's decay/colour law. Engines that shape their own
    // amplitude (the drums, the resonators) bypass the gate.
    if (!enveloped) {
      const float block = static_cast<float>(size);
      const float hf = vp.lpg_colour;
      const float short_decay = (200.0f * block) / plaits::kSampleRate *
          stmlib::SemitonesToRatio(-96.0f * vp.decay);
      const float decay_tail = (20.0f * block) / plaits::kSampleRate *
          stmlib::SemitonesToRatio(-72.0f * vp.decay + 12.0f * hf) -
          short_decay;
      lpg_envelope_.ProcessLP(
          vp.gate ? vp.level : 0.0f, short_decay, decay_tail, hf);
      const float gain = lpg_envelope_.gain();
      const float frequency = lpg_envelope_.frequency();
      const float hf_bleed = lpg_envelope_.hf_bleed();
      out_lpg_.Process(gain, frequency, hf_bleed, out, size);
      aux_lpg_.Process(gain, frequency, hf_bleed, aux, size);
    } else {
      lpg_envelope_.Init();
    }
  }

 private:
  // A negative registered gain means "limit with that pre-gain", a positive
  // one is a plain multiply.
  static void ApplyGain(
      float gain, stmlib::Limiter* limiter, float* buf, std::size_t size) {
    if (gain < 0.0f) {
      limiter->Process(-gain, buf, size);
    } else {
      for (std::size_t i = 0; i < size; ++i) buf[i] *= gain;
    }
  }

  alignas(8) uint8_t arena_[kArenaBytes];
  alignas(8) uint8_t engine_storage_[kMaxEngineSize];
  stmlib::BufferAllocator allocator_;
  plaits::Engine* engine_ = nullptr;
  int engine_index_ = -1;
  stmlib::Limiter out_limiter_;
  stmlib::Limiter aux_limiter_;
  plaits::LPGEnvelope lpg_envelope_;
  plaits::LowPassGate out_lpg_;
  plaits::LowPassGate aux_lpg_;
  bool gate_ = false;
  bool untriggered_ = false;

  Voice(const Voice&) = delete;
  Voice& operator=(const Voice&) = delete;
};

}  // namespace nb_pp

#endif  // NB_PP_VOICE_H_
