#include "sound.hpp"

#include "ofs/atmosphere.hpp"

#include <algorithm>
#include <cmath>

namespace ofs::client {
namespace {

constexpr float kRate = float(kSoundRate);
constexpr float kTau = 6.28318531f;
// Filters and levels are recomputed this often; in between they are held or ramped.
constexpr int kChunk = 64;
constexpr double kSoundSpeed = 340;

// A fraction to move toward a target each chunk, for a given settling time.
float slew(float seconds) { return 1 - std::exp(-kChunk / (kRate * seconds)); }
float mix(float a, float b, float t) { return a + (b - a) * t; }
float unit(float value) { return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f; }
float bounded(float value, float low, float high, float fallback) {
  return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

struct Rng {
  std::uint32_t state{0x2545f491u};
  // White noise, -1..1.
  float next() {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return float(std::int32_t(state)) * (1.f / 2147483648.f);
  }
  float chance() { return next() * .5f + .5f; }
};

// Noise that falls 3 dB an octave, as most natural rushing sounds do.
struct Pink {
  float a{}, b{}, c{};
  float next(float white) {
    a = .99765f * a + white * .0990460f;
    b = .96300f * b + white * .2965164f;
    c = .57000f * c + white * 1.0526913f;
    return (a + b + c + white * .1848f) * .22f;
  }
};

// Noise that falls 6 dB an octave: rumble.
struct Brown {
  float value{};
  float next(float white) { value = (value + .02f * white) * .98039f; return value * 6.f; }
};

// State-variable filter (trapezoidal), which stays stable while it is swept.
struct Svf {
  float s1{}, s2{}, a1{}, a2{}, a3{}, k{1};
  float low{}, band{}, high{};
  void set(float hz, float q) {
    const float g = std::tan(kTau * .5f * std::clamp(hz, 12.f, kRate * .45f) / kRate);
    k = 1 / q;
    a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
  }
  void run(float in) {
    const float v3 = in - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
    s1 = 2 * v1 - s1; s2 = 2 * v2 - s2;
    low = v2; band = v1 * k; high = in - k * v1 - v2;
  }
};

// One-pole low-pass coefficient for a corner frequency.
float pole(float hz) { return 1 - std::exp(-kTau * std::clamp(hz, 10.f, kRate * .45f) / kRate); }

// Equal-power stereo placement.
void place(float pan, float& left, float& right) {
  const float angle = (std::clamp(pan, -1.f, 1.f) + 1) * .25f * kTau * .5f;
  left = std::cos(angle) * 1.41421f; right = std::sin(angle) * 1.41421f;
}

// ---------------------------------------------------------------------------
// Sounds that happen once
// ---------------------------------------------------------------------------
// Each is a few layers of two kinds: a sine that glides in pitch, and noise
// through a filter that glides in pitch. A layer swells over `attack`, holds,
// and dies away over `decay`; its frequency starts at `from` and settles on
// `to` over `glide`. Noise with a `rate` is not continuous but a scatter of
// that many grains a second: crackle, sparks, falling debris.
enum : std::uint8_t { kSine, kNoise };
enum : std::uint8_t { kLow, kBand, kHigh };

struct Layer {
  std::uint8_t type{kSine}, mode{kLow};
  float level{}, start{}, attack{}, hold{}, decay{.1f};
  float from{440}, to{440}, glide{};
  float q{.7f};   // filter sharpness; for a sine, how much second harmonic
  float rate{};
};

struct Recipe {
  SoundBus bus{SoundBus::Weapons};
  float loud{.5f};  // peak level it is brought to
  std::vector<Layer> layers;
  float length{}, scale{1};
};

constexpr std::size_t kMaxLayers = 6;

Layer sine(float level, float from, float to, float glide, float decay, float start = 0, float hold = 0,
           float attack = 0, float harmonic = 0) {
  return {kSine, kLow, level, start, attack, hold, decay, from, to, glide, harmonic, 0};
}
Layer noise(std::uint8_t mode, float level, float from, float to, float glide, float q, float decay,
            float start = 0, float hold = 0, float attack = 0, float rate = 0) {
  return {kNoise, mode, level, start, attack, hold, decay, from, to, glide, q, rate};
}

float envelope(const Layer& layer, double time) {
  const double t = time - layer.start;
  if (t < 0) return 0;
  const double rise = layer.attack > 0 ? 1 - std::exp(-t / layer.attack) : 1;
  const double fall = t <= layer.hold ? 1 : std::exp(-(t - layer.hold) / layer.decay);
  return float(layer.level * rise * fall);
}

float frequency(const Layer& layer, double time) {
  const double t = std::max(0., time - layer.start);
  return layer.glide > 0 ? float(layer.to + (layer.from - layer.to) * std::exp(-t / layer.glide)) : layer.from;
}

struct Voice {
  const Recipe* recipe{};
  double time{};  // seconds since it was heard; negative while it is still on its way
  float left{}, right{}, pitch{1}, interior{}, airPole{1};
  float air{}, cabin{};
  Rng rng;
  struct LayerState { float phase{}, burst{}; Svf filter; };
  std::array<LayerState, kMaxLayers> layers;

  // Adds up to `count` samples; returns false once it has finished.
  bool render(float* left_, float* right_, int count) {
    const double step = 1.0 / kRate, end = time + count * step;
    if (end <= 0) { time = end; return true; }
    float out[kChunk] = {};
    for (std::size_t index = 0; index < recipe->layers.size(); ++index) {
      const Layer& layer = recipe->layers[index];
      LayerState& state = layers[index];
      const float from = envelope(layer, time), to = envelope(layer, end);
      if (from < 1e-5f && to < 1e-5f) continue;
      const float hz = frequency(layer, time + count * step * .5) * pitch;
      const float slope = (to - from) / count;
      float level = from;
      if (layer.type == kSine) {
        const float advance = hz / kRate;
        for (int i = 0; i < count; ++i, level += slope) {
          const float angle = state.phase * kTau;
          out[i] += level * (std::sin(angle) + layer.q * std::sin(2 * angle));
          state.phase += advance;
          if (state.phase >= 1) state.phase -= 1;
        }
      } else {
        state.filter.set(hz, layer.q);
        const float chance = layer.rate / kRate;
        for (int i = 0; i < count; ++i, level += slope) {
          float in = rng.next();
          if (layer.rate > 0) {
            if (rng.chance() < chance) state.burst = rng.next() * 4;
            in *= state.burst;
            state.burst *= .993f;
          }
          state.filter.run(in);
          out[i] += level * (layer.mode == kLow ? state.filter.low : layer.mode == kBand ? state.filter.band
                                                                                         : state.filter.high);
        }
      }
    }
    const float cabinPole = pole(900);
    for (int i = 0; i < count; ++i) {
      air += (out[i] - air) * airPole;
      cabin += (air - cabin) * cabinPole;
      const float sample = mix(air, cabin * 1.3f, interior) * recipe->scale;
      left_[i] += sample * left; right_[i] += sample * right;
    }
    time = end;
    return time < recipe->length;
  }
};

std::array<Recipe, std::size_t(SoundKind::Count)> makeRecipes() {
  std::array<Recipe, std::size_t(SoundKind::Count)> all;
  const auto define = [&](SoundKind kind, SoundBus bus, float loud, std::vector<Layer> layers) {
    all[std::size_t(kind)] = {bus, loud, std::move(layers), 0, 1};
  };
  using enum SoundKind;
  // A cannon round: the thump of the charge, the crack of the muzzle, and a
  // short tail that runs the rounds of a burst together.
  define(Gun, SoundBus::Weapons, .7f, {
      sine(1.f, 175, 58, .028f, .05f, 0, 0, 0, .25f),
      noise(kBand, 1.1f, 2700, 950, .018f, .7f, .022f),
      noise(kHigh, .4f, 6000, 6000, 0, .7f, .004f),
      noise(kLow, .3f, 950, 320, .14f, .7f, .15f, .004f, 0, .004f)});
  // The clunk of the release, the bang of the motor lighting, and the tearing
  // roar of it going away.
  define(MissileLaunch, SoundBus::Weapons, .9f, {
      sine(.9f, 120, 60, .05f, .09f, 0, 0, 0, .3f),
      noise(kHigh, .7f, 4200, 4200, 0, .7f, .01f, .015f),
      noise(kBand, 1.2f, 420, 2900, .14f, .55f, .7f, .02f, .2f, .01f),
      noise(kLow, 1.f, 1500, 420, .6f, .7f, 1.1f, .03f, .35f, .03f),
      noise(kBand, .6f, 2100, 1500, .4f, 1.5f, .9f, .05f, .15f, .01f, 150),
      sine(.5f, 62, 38, .3f, .5f, .02f, .1f, .02f, .2f)});
  // An aircraft blowing up: crack, body, the blast rolling off into a long
  // rumble, and pieces of it coming down.
  define(Explosion, SoundBus::Weapons, .98f, {
      noise(kHigh, .9f, 3000, 3000, 0, .7f, .012f),
      sine(1.3f, 98, 30, .25f, .55f, 0, 0, 0, .3f),
      noise(kLow, 1.3f, 2900, 180, .2f, .7f, .5f, 0, 0, .002f),
      noise(kLow, 1.f, 240, 70, 1.2f, .7f, 1.5f, .02f, .4f, .08f),
      noise(kBand, .35f, 2600, 1500, .8f, 2.f, .9f, .12f, .2f, .01f, 45),
      sine(.7f, 50, 24, .5f, 1.f)});
  // An aircraft flying into the ground: the blow, the airframe folding and
  // tearing along the ground, the fuel going up, and wreckage coming down.
  define(Crash, SoundBus::Weapons, .98f, {
      sine(1.4f, 74, 26, .16f, .5f, 0, 0, 0, .35f),
      noise(kBand, 1.2f, 2100, 520, .1f, 1.1f, .22f, 0, .05f, .002f, 190),
      noise(kBand, .8f, 980, 620, .5f, 2.4f, .7f, .04f, .25f, .02f, 70),
      noise(kLow, 1.3f, 2600, 150, .22f, .7f, .65f, .07f, .05f, .01f),
      noise(kLow, 1.f, 210, 60, 1.4f, .7f, 1.9f, .1f, .5f, .1f),
      noise(kBand, .4f, 2900, 1700, .9f, 2.2f, 1.3f, .3f, .3f, .02f, 40)});
  // A wing or a fin going: the spar cracks, the skin tears, and the piece is
  // heard to leave.
  define(PartBreak, SoundBus::Weapons, .7f, {
      noise(kHigh, 1.f, 5200, 5200, 0, .7f, .007f),
      sine(1.f, 210, 66, .04f, .09f, 0, 0, 0, .3f),
      noise(kBand, .9f, 1250, 560, .2f, 3.f, .3f, .01f, .08f, .004f, 120),
      sine(.22f, 742, 706, .2f, .22f, .01f),
      sine(.16f, 1318, 1270, .2f, .15f, .01f),
      noise(kBand, .45f, 480, 1500, .25f, .8f, .32f, .05f, .05f, .06f)});
  // Leaving the aircraft: the canopy is blown off, the seat's rocket fires,
  // and the wind takes over.
  define(Eject, SoundBus::Weapons, .85f, {
      noise(kHigh, 1.f, 4600, 4600, 0, .7f, .009f),
      sine(1.f, 150, 58, .05f, .1f, 0, 0, 0, .3f),
      noise(kBand, 1.1f, 700, 2600, .1f, .6f, .3f, .09f, .22f, .01f),
      noise(kLow, .8f, 1400, 500, .3f, .7f, .45f, .1f, .2f, .02f),
      noise(kBand, .4f, 2000, 1500, .3f, 1.5f, .4f, .1f, .1f, .01f, 140)});
  // Silk filling with air: a rustle that ends in a crack.
  define(Parachute, SoundBus::Airframe, .5f, {
      noise(kBand, .5f, 900, 2200, .2f, .8f, .08f, 0, .22f, .08f),
      noise(kLow, 1.f, 1500, 380, .08f, .7f, .2f, .26f, 0, .004f),
      sine(.9f, 120, 62, .05f, .12f, .26f, 0, 0, .25f),
      noise(kBand, .25f, 2600, 2000, .3f, 1.5f, .5f, .3f, .1f, .02f, 60)});
  // A warhead: sharper and shorter, with fragments.
  define(Detonation, SoundBus::Weapons, .9f, {
      noise(kHigh, 1.1f, 4000, 4000, 0, .7f, .008f),
      sine(1.1f, 145, 42, .11f, .28f, 0, 0, 0, .3f),
      noise(kLow, 1.2f, 4300, 300, .09f, .7f, .25f),
      noise(kLow, .55f, 420, 110, .6f, .7f, .8f, .02f, .15f, .04f),
      noise(kBand, .3f, 3600, 2400, .3f, 2.5f, .35f, .05f, .05f, .005f, 70)});
  define(Impact, SoundBus::Weapons, .34f, {
      noise(kBand, .9f, 4200, 3000, .01f, 1.2f, .012f),
      sine(.3f, 2350, 2150, .03f, .05f),
      sine(.5f, 200, 110, .02f, .03f)});
  // A round going through the pilot's own airframe: a blow, and metal ringing.
  define(HitTaken, SoundBus::Weapons, .62f, {
      sine(1.f, 125, 62, .03f, .09f, 0, 0, 0, .2f),
      noise(kBand, .9f, 2300, 1500, .02f, .8f, .02f),
      sine(.28f, 530, 524, .1f, .18f),
      sine(.2f, 1183, 1175, .1f, .12f),
      sine(.14f, 1834, 1826, .1f, .08f),
      noise(kBand, .3f, 1500, 1500, 0, 2.f, .15f, .01f, 0, 0, 60)});
  // A flare: the pop of the cartridge, then the hiss and sputter of it burning.
  define(Flare, SoundBus::Weapons, .4f, {
      sine(.8f, 330, 140, .015f, .035f),
      noise(kBand, .7f, 1800, 1200, .01f, .9f, .015f),
      noise(kHigh, .22f, 5200, 4200, .3f, .7f, .42f, .005f, .05f, .01f),
      noise(kBand, .2f, 6500, 6500, 0, 2.f, .5f, .01f, 0, 0, 90)});
  define(Chaff, SoundBus::Weapons, .34f, {
      noise(kBand, .8f, 1350, 700, .05f, .7f, .09f, 0, 0, .003f),
      sine(.5f, 205, 120, .02f, .03f),
      noise(kBand, .16f, 5000, 5000, 0, 1.5f, .25f, .01f, 0, 0, 150)});
  // Fuel lighting in the jet pipe.
  define(ReheatLight, SoundBus::Engines, .5f, {
      sine(1.f, 78, 42, .12f, .28f, 0, 0, .01f, .2f),
      noise(kLow, .7f, 300, 950, .15f, .7f, .32f, 0, .04f, .02f)});
  define(GearLock, SoundBus::Airframe, .4f, {
      sine(1.f, 88, 60, .03f, .07f, 0, 0, 0, .2f),
      noise(kBand, .6f, 1500, 1500, 0, 1.5f, .012f),
      sine(.12f, 410, 404, .05f, .06f)});
  // Tyres meeting the runway: a chirp of rubber over the thump of the gear.
  define(Touchdown, SoundBus::Airframe, .6f, {
      noise(kBand, .8f, 3400, 1900, .08f, 3.f, .13f, 0, 0, .002f),
      sine(.12f, 2100, 1300, .1f, .1f),
      sine(1.f, 72, 45, .05f, .14f, 0, 0, 0, .2f),
      noise(kLow, .5f, 500, 300, .05f, .7f, .1f)});
  define(Crunch, SoundBus::Airframe, .85f, {
      sine(1.1f, 82, 35, .08f, .25f, 0, 0, 0, .3f),
      noise(kLow, 1.1f, 1800, 350, .12f, .7f, .3f),
      noise(kBand, .5f, 1900, 1400, .2f, 2.5f, .4f, .01f, 0, 0, 110),
      sine(.12f, 640, 630, .1f, .2f)});
  // Water: the slap of the hull, the rush of what it throws up and the hiss
  // of it coming back down.
  define(Splash, SoundBus::Airframe, .8f, {
      sine(1.f, 74, 36, .06f, .22f, 0, 0, 0, .3f),
      noise(kLow, 1.f, 1100, 320, .1f, .7f, .32f),
      noise(kBand, .7f, 2600, 1300, .15f, .9f, .55f),
      noise(kHigh, .35f, 5200, 3400, .3f, .7f, .8f, .005f, .05f, .01f)});
  define(DryFire, SoundBus::Cockpit, .16f, {
      noise(kBand, .8f, 3200, 3200, 0, 3.f, .008f),
      sine(.3f, 1900, 1700, .01f, .012f)});
  define(WeaponSelect, SoundBus::Cockpit, .2f, {
      noise(kBand, .8f, 2000, 2000, 0, 1.5f, .008f),
      sine(.5f, 185, 120, .02f, .03f),
      noise(kBand, .5f, 2600, 2600, 0, 2.f, .006f, .05f)});
  define(LockBeep, SoundBus::Cockpit, .17f, {sine(1.f, 1480, 1480, 0, .02f, 0, .05f, .003f, .15f)});
  define(LockLost, SoundBus::Cockpit, .17f, {sine(1.f, 640, 430, .08f, .05f, 0, .1f, .003f, .2f)});
  define(Caution, SoundBus::Cockpit, .2f, {
      sine(1.f, 940, 940, 0, .02f, 0, .08f, .003f, .3f),
      sine(1.f, 940, 940, 0, .02f, .16f, .08f, .003f, .3f),
      sine(1.f, 940, 940, 0, .02f, .32f, .08f, .003f, .3f)});
  define(MissileAlert, SoundBus::Cockpit, .24f, {
      sine(1.f, 2100, 2100, 0, .015f, 0, .05f, .002f, .2f),
      sine(1.f, 2100, 2100, 0, .015f, .09f, .05f, .002f, .2f),
      sine(1.f, 2100, 2100, 0, .015f, .18f, .05f, .002f, .2f)});
  define(HitMarker, SoundBus::Cockpit, .2f, {
      sine(1.f, 1760, 1600, .03f, .035f),
      noise(kBand, 1.f, 3800, 3800, 0, 2.f, .005f)});
  // Three rising notes.
  define(KillChime, SoundBus::Cockpit, .24f, {
      sine(1.f, 784, 784, 0, .22f, 0, 0, .002f, .2f),
      sine(1.f, 1175, 1175, 0, .25f, .085f, 0, .002f, .2f),
      sine(1.f, 1568, 1568, 0, .42f, .17f, 0, .002f, .2f)});
  define(Killed, SoundBus::Cockpit, .26f, {
      sine(1.f, 330, 70, .45f, .8f, 0, .1f, .005f, .3f),
      noise(kLow, .7f, 900, 100, .5f, .7f, .8f, 0, .1f, .01f)});
  define(Serviced, SoundBus::Cockpit, .22f, {
      sine(1.f, 660, 660, 0, .18f, 0, 0, .002f, .2f),
      sine(1.f, 990, 990, 0, .36f, .13f, 0, .002f, .2f)});
  define(ChatBlip, SoundBus::Cockpit, .14f, {
      sine(1.f, 880, 880, 0, .05f, 0, 0, .002f),
      sine(1.f, 1320, 1320, 0, .09f, .06f, 0, .002f)});
  define(UiClick, SoundBus::Cockpit, .12f, {
      noise(kBand, 1.f, 2800, 2800, 0, 2.f, .004f),
      sine(.35f, 1500, 1500, 0, .012f)});
  define(UiOpen, SoundBus::Cockpit, .13f, {
      sine(1.f, 520, 520, 0, .05f, 0, 0, .002f),
      sine(1.f, 780, 780, 0, .08f, .045f, 0, .002f)});
  define(UiClose, SoundBus::Cockpit, .13f, {
      sine(1.f, 780, 780, 0, .05f, 0, 0, .002f),
      sine(1.f, 520, 520, 0, .08f, .045f, 0, .002f)});

  // Each is played once here, so that `loud` is the level it actually peaks at
  // whatever its layers add up to.
  for (auto& recipe : all) {
    for (const auto& layer : recipe.layers)
      recipe.length = std::max(recipe.length, layer.start + layer.hold + layer.attack * 3 + layer.decay * 7);
    Voice voice;
    voice.recipe = &recipe;
    voice.left = voice.right = 1;
    float peak = 1e-6f;
    for (bool playing = true; playing;) {
      float left[kChunk] = {}, right[kChunk] = {};
      playing = voice.render(left, right, kChunk);
      for (float sample : left) peak = std::max(peak, std::abs(sample));
    }
    recipe.scale = recipe.loud / peak;
  }
  return all;
}

const std::array<Recipe, std::size_t(SoundKind::Count)>& recipes() {
  static const auto all = makeRecipes();
  return all;
}

// ---------------------------------------------------------------------------
// Sounds that go on
// ---------------------------------------------------------------------------

// A pair of jet engines. Four things are heard in one: the whine of the
// compressor, which rises with the engine's speed; the roar of the exhaust,
// which rises much faster with its power and is heard mostly from behind; a
// rumble under both; and, in reheat, the tearing crackle of the flame.
struct JetVoice {
  JetSound now;
  float run[2]{1, 1};  // how far each engine has spun up, 0 stopped .. 1 running
  Rng rng;
  Pink pink;
  Brown brown;
  Svf rumble, roar, hiss, crack, whistle, cabin;
  float air{}, flutter{}, burst{}, drift{}, driftTarget{};
  float phase[2]{};
  float left{}, right{};

  void render(const JetSound& wanted, float volume, float* outLeft, float* outRight, int count) {
    JetSound target = wanted;
    if (!wanted.id && now.id) {
      // Gone from the scene: what was sounding dies away where it was.
      target = now;
      target.gain = 0;
      if (now.gain < 1e-4f) { now = {}; left = right = 0; }
    } else if (wanted.id != now.id) {
      // Someone else's engines in this slot: they fade in from nothing.
      now = target;
      now.gain = 0;
      left = right = 0;
      for (int e = 0; e < 2; ++e) {
        run[e] = target.power[e] >= 0 ? 1.f : 0.f;
        now.power[e] = unit(target.power[e]);
        now.reheat[e] = unit(target.reheat[e]);
      }
    }
    if (!now.id) return;
    const float quick = slew(.045f), slow = slew(.9f);
    now.gain = mix(now.gain, bounded(target.gain, 0, 4, 0), quick);
    now.pan = mix(now.pan, bounded(target.pan, -1, 1, 0), quick);
    now.doppler = mix(now.doppler, bounded(target.doppler, .3f, 3, 1), quick);
    now.rear = mix(now.rear, unit(target.rear), quick);
    now.interior = mix(now.interior, unit(target.interior), quick);
    now.air = mix(now.air, bounded(target.air, 200, 20000, 20000), quick);
    now.pitch = bounded(target.pitch, .4f, 2, 1);
    now.fan = unit(target.fan);
    float power = 0, running = 0, reheat = 0, speed[2];
    for (int e = 0; e < 2; ++e) {
      run[e] = mix(run[e], target.power[e] >= 0 ? 1.f : 0.f, slow);
      now.power[e] = mix(now.power[e], unit(target.power[e]), slew(.12f));
      now.reheat[e] = mix(now.reheat[e], unit(target.reheat[e]), slew(.08f));
      power += .5f * now.power[e] * run[e];
      running += .5f * run[e];
      reheat += .5f * now.reheat[e] * run[e];
      speed[e] = std::pow(now.power[e], .85f);
    }
    if (now.gain < 1e-4f && target.gain < 1e-4f) { left = right = 0; return; }

    // The two engines never turn at quite the same speed, and beat.
    if (rng.chance() < .02f) driftTarget = rng.next();
    drift = mix(drift, driftTarget, .01f);
    const float tune = now.pitch * now.doppler;
    const float idle = mix(640.f, 470.f, now.fan), full = mix(2500.f, 1650.f, now.fan);
    float advance[2], whineHz = 0;
    for (int e = 0; e < 2; ++e) {
      const float hz = mix(idle, full, speed[e]) * tune * (e ? 1.013f : 1.f) * (.3f + .7f * run[e]) *
                       (1 + .004f * drift);
      advance[e] = hz / kRate;
      whineHz += .5f * hz;
    }
    // Levels, each as the loudness that layer should have beside the source,
    // divided by what its filter passes of the noise it is fed.
    const float behind = .45f + .55f * now.rear;
    const float whine = (.2f + .3f * power) * (1 - .55f * power * power) * (1 - .5f * reheat) *
                        (1 - .45f * now.rear) * (1 + .4f * now.fan);
    const float lWhine[2] = {whine * run[0] * .3f, whine * run[1] * .3f};
    const float lWhistle = whine * running * 7.f;
    const float lRoar = (running * (.018f + .095f * std::pow(power, 2.2f)) * (1 - .3f * now.fan) + reheat * .054f) * behind / .157f;
    const float lRumble = (running * (.032f + .064f * power * power) * (.6f + .4f * now.rear) + reheat * .076f * behind) / .23f;
    const float lHiss = (running * (.004f + .039f * power * power * power) + reheat * .021f) * behind / .25f;
    const float lCrack = (reheat * .03f + std::max(0.f, power - .85f) * .15f * running) * behind / .2f;
    const float crackChance = (25 + 110 * reheat) / kRate;
    // At speed the whine thins to a single note, and is lost in the roar.
    const float overtone = 1 - .6f * power;
    rumble.set((110 + 60 * reheat) * now.doppler, .7f);
    roar.set((190 + 330 * power) * tune * (1 - .25f * now.fan), .75f);
    hiss.set((1200 + 1500 * power) * now.doppler, .6f);
    crack.set(850 * tune, 1.4f);
    whistle.set(whineHz, 22);
    cabin.set(600, .7f);
    const float airPole = pole(now.air), flutterPole = pole(14);
    const float inside = now.interior, level = now.gain * volume * (1 - .35f * inside);
    float toLeft, toRight;
    place(now.pan, toLeft, toRight);
    toLeft *= level; toRight *= level;
    const float stepLeft = (toLeft - left) / count, stepRight = (toRight - right) / count;
    for (int i = 0; i < count; ++i) {
      const float white = rng.next(), other = rng.next();
      // Reheat does not burn evenly: the rumble surges a dozen times a second.
      flutter += (other - flutter) * flutterPole;
      const float surge = 1 + std::clamp(flutter * 18, -1.f, 1.f) * .45f * reheat;
      rumble.run(brown.next(white));
      roar.run(pink.next(white));
      hiss.run(other);
      if (rng.chance() < crackChance) burst = .4f + .6f * rng.chance();
      crack.run(other * burst * 5);
      burst *= .9965f;
      whistle.run(white);
      float sample = lRumble * rumble.low * surge + lRoar * roar.band * surge + lHiss * hiss.band +
                     lCrack * crack.band + lWhistle * whistle.band;
      for (int e = 0; e < 2; ++e) {
        const float angle = phase[e] * kTau;
        sample += lWhine[e] * (std::sin(angle) + overtone * (.45f * std::sin(2 * angle) + .22f * std::sin(3 * angle)));
        phase[e] += advance[e];
        if (phase[e] >= 1) phase[e] -= 1;
      }
      // From the flight deck the engines are behind a bulkhead: felt more than heard.
      cabin.run(sample);
      sample = mix(sample, cabin.low * 1.8f + sample * .1f, inside);
      air += (sample - air) * airPole;
      left += stepLeft; right += stepRight;
      outLeft[i] += air * left; outRight[i] += air * right;
    }
  }
};

// A rocket motor burning.
struct MissileVoice {
  MissileSound now;
  Rng rng;
  Pink pink;
  Brown brown;
  Svf roar, rumble, crack;
  float air{}, burst{}, left{}, right{};

  void render(const MissileSound& wanted, float volume, float* outLeft, float* outRight, int count) {
    MissileSound target = wanted;
    if (!wanted.id && now.id) {
      target = now;
      target.gain = 0;
      if (now.gain < 1e-4f) { now = {}; left = right = 0; }
    } else if (wanted.id != now.id) {
      now = target; now.gain = 0; left = right = 0;
    }
    if (!now.id) return;
    const float quick = slew(.04f);
    now.gain = mix(now.gain, bounded(target.gain, 0, 4, 0), quick);
    now.pan = mix(now.pan, bounded(target.pan, -1, 1, 0), quick);
    now.doppler = mix(now.doppler, bounded(target.doppler, .3f, 3, 1), quick);
    now.air = mix(now.air, bounded(target.air, 200, 20000, 20000), quick);
    if (now.gain < 1e-4f && target.gain < 1e-4f) { left = right = 0; return; }
    roar.set(1100 * now.doppler, .55f);
    rumble.set(260 * now.doppler, .7f);
    crack.set(2300 * now.doppler, 1.6f);
    const float airPole = pole(now.air);
    float toLeft, toRight;
    place(now.pan, toLeft, toRight);
    toLeft *= now.gain * volume; toRight *= now.gain * volume;
    const float stepLeft = (toLeft - left) / count, stepRight = (toRight - right) / count;
    for (int i = 0; i < count; ++i) {
      const float white = rng.next(), other = rng.next();
      roar.run(pink.next(white));
      rumble.run(brown.next(other));
      if (rng.chance() < 160 / kRate) burst = .4f + .6f * rng.chance();
      crack.run(white * burst * 5);
      burst *= .9965f;
      const float sample = roar.band * 1.7f + rumble.low * .85f + crack.band * .6f;
      air += (sample - air) * airPole;
      left += stepLeft; right += stepRight;
      outLeft[i] += air * left; outRight[i] += air * right;
    }
  }
};

// The air, and what the pilot's own airframe does in it: the rush past the
// canopy, the shudder of a hard turn, the airbrake and the gear in the stream,
// the wheels on the runway. The rush is made separately for each ear, which is
// what makes it surround the listener instead of sitting between the speakers.
struct AirframeVoice {
  struct Side {
    Rng rng;
    Pink pink;
    Svf rush, hiss;
    float gust{}, gustTarget{};
  };
  Side side[2];
  Rng rng;
  Pink pink;
  Brown brown;
  Svf shudder, shudderHigh, brake, gear, rollLow, rollHigh, thump, scrapeHigh, scrapeLow, motor, cabin;
  float airspeed{}, density{1}, own{}, buffet{}, airbrake{}, gearDrag{}, roll{}, scrape{}, gearMotor{}, flapMotor{},
      interior{};
  float flutterPhase{}, joint{}, motorPhase[2]{}, humPhase{}, rasp{};

  void render(const SoundScene& scene, float volume, float* outLeft, float* outRight, int count) {
    const float quick = slew(.08f);
    airspeed = mix(airspeed, bounded(scene.airspeed, 0, 1500, 0), quick);
    density = mix(density, bounded(scene.density, 0, 1.5f, 1), quick);
    own = mix(own, unit(scene.own), quick);
    buffet = mix(buffet, unit(scene.buffet), slew(.12f));
    airbrake = mix(airbrake, unit(scene.airbrake), quick);
    gearDrag = mix(gearDrag, unit(scene.gearDrag), quick);
    roll = mix(roll, bounded(scene.roll, 0, 200, 0), quick);
    scrape = mix(scrape, unit(scene.scrape), slew(.03f));
    gearMotor = mix(gearMotor, unit(scene.gearMotor), slew(.06f));
    flapMotor = mix(flapMotor, unit(scene.flapMotor), slew(.06f));
    interior = mix(interior, unit(scene.interior), quick);

    // Thin air carries little sound, however fast it goes by.
    const float thin = std::sqrt(density);
    const float wind = (.003f + std::min(1.3f, std::pow(airspeed / 280, 1.4f)) * thin * .08f) * (1 - .3f * interior);
    const float hiss = wind * .16f * std::min(1.f, airspeed / 200) * (1 - .6f * interior);
    for (auto& s : side) {
      if (s.rng.chance() < .012f) s.gustTarget = s.rng.next();
      s.gust = mix(s.gust, s.gustTarget, .004f);
      s.rush.set(std::min(1500.f, 160 + 1.9f * airspeed) * (1 + .12f * s.gust) * (1 - .35f * interior), .55f);
      s.hiss.set(2600, .5f);
    }
    const float lShudder = buffet * own * 1.1f;
    const float lBrake = airbrake * own * .3f, lGear = gearDrag * own * .09f;
    const float rolling = std::min(1.f, roll / 60) * own;
    const float lScrape = scrape * own * .7f;
    const float lMotor = (gearMotor * .03f + flapMotor * .018f) * own;
    const float motorHz = gearMotor >= flapMotor ? 335.f : 505.f;
    const float lCabin = interior * .012f;
    shudder.set(45, 1.2f); shudderHigh.set(130, .9f);
    brake.set(430, .8f); gear.set(240, .7f);
    rollLow.set(58 + roll * .25f, .8f); rollHigh.set(380, .6f);
    thump.set(105, 2.5f);
    scrapeHigh.set(1900, 1.2f); scrapeLow.set(480, .9f);
    motor.set(2400, 1);
    cabin.set(700, .5f);
    // Runway joints go by every few metres.
    const float jointStep = roll / (7.5f * kRate);
    for (int i = 0; i < count; ++i) {
      float channel[2];
      for (int c = 0; c < 2; ++c) {
        Side& s = side[c];
        const float white = s.rng.next();
        s.rush.run(s.pink.next(white));
        s.hiss.run(white);
        channel[c] = s.rush.band * wind * (1 + .2f * s.gust) / .172f + s.hiss.band * hiss / .25f;
      }
      float centre = 0;
      const float white = rng.next(), tinted = pink.next(white), deep = brown.next(rng.next());
      if (lShudder > 1e-4f || lBrake > 1e-4f) {
        flutterPhase += (11 + 3 * rasp) / kRate;
        if (flutterPhase >= 1) { flutterPhase -= 1; rasp = rng.next(); }
        const float flutter = .55f + .45f * std::sin(flutterPhase * kTau);
        shudder.run(deep); shudderHigh.run(deep);
        brake.run(tinted);
        centre += lShudder * flutter * (shudder.band + shudderHigh.band) + lBrake * flutter * brake.band * 2.5f;
      }
      if (lGear > 1e-4f) { gear.run(tinted); centre += lGear * gear.band * 2.5f; }
      if (rolling > 1e-4f) {
        joint += jointStep;
        float knock = 0;
        if (joint >= 1) { joint -= 1; knock = 14 + 6 * rng.next(); }
        rollLow.run(deep); rollHigh.run(tinted); thump.run(knock);
        centre += rolling * (rollLow.band * .5f + rollHigh.band * .17f + thump.low * .12f);
      }
      if (lScrape > 1e-4f) {
        scrapeHigh.run(white); scrapeLow.run(tinted);
        centre += lScrape * (scrapeHigh.band + scrapeLow.band * 2) * (.6f + .4f * deep);
      }
      if (lMotor > 1e-5f) {
        // An electric motor through a gearbox: a buzz and its octave, and the whirr of the screwjack.
        motorPhase[0] += motorHz / kRate; motorPhase[1] += motorHz * 2.02f / kRate;
        for (float& phase : motorPhase) if (phase >= 1) phase -= 1;
        const float turn = motorPhase[0] * kTau, mesh = motorPhase[1] * kTau;
        motor.run(white);
        centre += lMotor * (std::sin(turn) + .5f * std::sin(3 * turn) + .3f * std::sin(5 * turn) +
                            .5f * std::sin(mesh) + motor.band * 1.5f);
      }
      if (lCabin > 1e-5f) {
        // The flight deck is never silent: air conditioning and the avionics' hum.
        humPhase += 400 / kRate;
        if (humPhase >= 1) humPhase -= 1;
        cabin.run(tinted);
        centre += lCabin * (cabin.band * 3 + .12f * std::sin(humPhase * kTau));
      }
      outLeft[i] += (channel[0] + centre) * volume;
      outRight[i] += (channel[1] + centre) * volume;
    }
  }
};

// The flight deck's warnings. Each fades in and out over a few milliseconds
// so that none of them clicks.
struct ToneVoice {
  float stall{}, warning{}, urgency{}, growl{}, locked{}, progress{};
  float stallPhase{}, stallGate{}, warnPhase{}, warnGate{}, sawPhase{}, lockPhase{}, tremolo{};
  float strain{}, beat{}, pulsePhase{}, pulse{};
  Rng rng;
  Svf throat;

  void render(const SoundScene& scene, float volume, float* outLeft, float* outRight, int count) {
    const float quick = slew(.012f);
    const float seeker = bounded(scene.seeker, 0, 1, 0);
    stall = mix(stall, scene.stall > .5f ? 1.f : 0.f, quick);
    warning = mix(warning, scene.warning > 0 ? 1.f : 0.f, quick);
    if (scene.warning > 0) urgency = mix(urgency, unit(scene.warning), slew(.2f));
    growl = mix(growl, seeker > 0 && seeker < 1 ? 1.f : 0.f, quick);
    locked = mix(locked, seeker >= 1 ? 1.f : 0.f, quick);
    progress = mix(progress, seeker, slew(.1f));
    strain = mix(strain, unit(scene.strain), slew(.3f));
    if (stall + warning + growl + locked + strain < 1e-4f) return;
    throat.set(520 + 520 * progress, 3);
    const float warble = (4.5f + 7 * urgency) / kRate;
    for (int i = 0; i < count; ++i) {
      float sample = 0;
      if (stall > 1e-4f) {
        // A reedy horn, pulsed.
        stallGate += 6.f / kRate;
        if (stallGate >= 1) stallGate -= 1;
        stallPhase += 520 / kRate;
        if (stallPhase >= 1) stallPhase -= 1;
        const float angle = stallPhase * kTau;
        const float gate = std::clamp(std::min(stallGate, .55f - stallGate) * 60, 0.f, 1.f);
        sample += stall * gate * .1f * (std::sin(angle) + .45f * std::sin(3 * angle) + .2f * std::sin(5 * angle));
      }
      if (warning > 1e-4f) {
        // Two notes, faster as the missile closes.
        warnGate += warble;
        if (warnGate >= 1) warnGate -= 1;
        warnPhase += (warnGate < .5f ? 1250.f : 1650.f) / kRate;
        if (warnPhase >= 1) warnPhase -= 1;
        const float angle = warnPhase * kTau;
        sample += warning * .11f * (std::sin(angle) + .3f * std::sin(2 * angle));
      }
      if (growl > 1e-4f) {
        // The rasp of a seeker head that has found something warm.
        sawPhase += (72 + 62 * progress) / kRate;
        if (sawPhase >= 1) sawPhase -= 1;
        throat.run((2 * sawPhase - 1) * (1 + .35f * rng.next()));
        sample += growl * (.05f + .05f * progress) * throat.band;
      }
      if (locked > 1e-4f) {
        // The same head with its target held: a steady, chattering tone.
        lockPhase += 1050 / kRate;
        if (lockPhase >= 1) lockPhase -= 1;
        tremolo += 23 / kRate;
        if (tremolo >= 1) tremolo -= 1;
        sample += locked * .075f * (.6f + .4f * std::sin(tremolo * kTau)) *
                  (std::sin(lockPhase * kTau) + .25f * std::sin(2 * lockPhase * kTau));
      }
      if (strain > 1e-3f) {
        // The pilot's own pulse, louder and faster as sight goes: two thumps
        // to a beat.
        beat += (1.5f + 1.3f * strain) / kRate;
        if (beat >= 1) { beat -= 1; pulse = 1; }
        if (beat >= .28f && beat - (1.5f + 1.3f * strain) / kRate < .28f) pulse = .7f;
        pulsePhase += (46 + 30 * pulse) / kRate;
        if (pulsePhase >= 1) pulsePhase -= 1;
        sample += strain * strain * .34f * pulse * std::sin(pulsePhase * kTau);
        pulse *= .9993f;
      }
      outLeft[i] += sample * volume; outRight[i] += sample * volume;
    }
  }
};

}  // namespace

struct SoundMixer::State {
  SoundScene scene;
  std::array<JetVoice, SoundScene::kJets> jets;
  std::array<MissileVoice, SoundScene::kMissiles> missiles;
  AirframeVoice airframe;
  ToneVoice tones;
  std::array<Voice, 64> voices;
  std::uint32_t seed{0x1234567u};
  float master{}, world{1}, muffle{}, limiter{1};
  float muffled[2]{};
  std::array<float, std::size_t(SoundBus::Count)> volume{1, 1, 1, 1};

  void start(const SoundEvent& event) {
    const Recipe& recipe = recipes()[std::size_t(event.kind) % recipes().size()];
    if (recipe.layers.empty() || !(event.gain > 0)) return;
    // With every voice busy, the one nearest its end makes way.
    Voice* chosen = nullptr;
    double furthest = -1e9;
    for (auto& voice : voices) {
      if (!voice.recipe) { chosen = &voice; break; }
      const double done = voice.time / voice.recipe->length;
      if (done > furthest) { furthest = done; chosen = &voice; }
    }
    *chosen = {};
    chosen->recipe = &recipe;
    chosen->time = -double(bounded(event.delay, 0, 30, 0));
    chosen->pitch = bounded(event.pitch, .25f, 4, 1);
    chosen->interior = unit(event.interior);
    chosen->airPole = pole(20000 / (1 + bounded(event.distance, 0, 1e6f, 0) / 500));
    seed = seed * 1664525u + 1013904223u;
    chosen->rng.state = seed | 1u;
    const float gain = bounded(event.gain, 0, 4, 0);
    place(bounded(event.pan, -1, 1, 0), chosen->left, chosen->right);
    chosen->left *= gain; chosen->right *= gain;
  }
};

SoundMixer::SoundMixer() : state_(new State) {
  recipes();  // built here, not on the audio thread
  for (std::size_t i = 0; i < state_->jets.size(); ++i) state_->jets[i].rng.state = 0x51ed270bu + 977u * std::uint32_t(i);
  for (std::size_t i = 0; i < state_->missiles.size(); ++i) state_->missiles[i].rng.state = 0x7f4a7c15u + 31u * std::uint32_t(i);
  state_->airframe.side[1].rng.state = 0x3c6ef372u;
  state_->airframe.rng.state = 0xa54ff53au;
}

SoundMixer::~SoundMixer() { delete state_; }

void SoundMixer::setScene(const SoundScene& scene) {
  const std::lock_guard lock(mutex_);
  pending_ = scene;
  dirty_ = true;
}

void SoundMixer::play(const SoundEvent& event) {
  const std::lock_guard lock(mutex_);
  if (queued_ < queue_.size()) queue_[queued_++] = event;
}

void SoundMixer::render(float* stereo, std::size_t frames) {
  State& s = *state_;
  // The game thread holds this lock only to copy; if it has it now, the news
  // is picked up ten milliseconds later instead.
  if (std::unique_lock lock(mutex_, std::try_to_lock); lock) {
    if (dirty_) { s.scene = pending_; dirty_ = false; }
    for (std::size_t i = 0; i < queued_; ++i) s.start(queue_[i]);
    queued_ = 0;
  }
  const SoundScene& scene = s.scene;
  std::size_t active = 0;
  float loudestOut = peak_.load(std::memory_order_relaxed);
  for (std::size_t done = 0; done < frames;) {
    const int count = int(std::min<std::size_t>(kChunk, frames - done));
    const float quick = slew(.03f);
    s.master = mix(s.master, bounded(scene.master, 0, 2, 0), quick);
    s.world = mix(s.world, unit(scene.world), quick);
    s.muffle = mix(s.muffle, unit(scene.muffle), slew(.25f));
    for (std::size_t bus = 0; bus < s.volume.size(); ++bus)
      s.volume[bus] = mix(s.volume[bus], bounded(scene.volume[bus], 0, 2, 1), quick);
    const auto volume = [&](SoundBus bus) { return s.volume[std::size_t(bus)]; };

    // The world, which can be held and muffled, and the flight deck, which cannot.
    float worldLeft[kChunk] = {}, worldRight[kChunk] = {}, deckLeft[kChunk] = {}, deckRight[kChunk] = {};
    for (std::size_t i = 0; i < s.jets.size(); ++i)
      s.jets[i].render(scene.jets[i], volume(SoundBus::Engines), worldLeft, worldRight, count);
    for (std::size_t i = 0; i < s.missiles.size(); ++i)
      s.missiles[i].render(scene.missiles[i], volume(SoundBus::Engines), worldLeft, worldRight, count);
    s.airframe.render(scene, volume(SoundBus::Airframe), worldLeft, worldRight, count);
    s.tones.render(scene, volume(SoundBus::Cockpit), deckLeft, deckRight, count);
    active = 0;
    for (auto& voice : s.voices) {
      if (!voice.recipe) continue;
      ++active;
      const bool deck = voice.recipe->bus == SoundBus::Cockpit;
      const float level = volume(voice.recipe->bus);
      float left[kChunk] = {}, right[kChunk] = {};
      if (!voice.render(left, right, count)) voice.recipe = nullptr;
      float* toLeft = deck ? deckLeft : worldLeft;
      float* toRight = deck ? deckRight : worldRight;
      for (int i = 0; i < count; ++i) { toLeft[i] += left[i] * level; toRight[i] += right[i] * level; }
    }

    // Shot down, the pilot hears the world as if from under water.
    const float dull = pole(mix(20000.f, 320.f, s.muffle));
    float* out = stereo + done * 2;
    for (int i = 0; i < count; ++i) {
      float sample[2] = {worldLeft[i] * s.world, worldRight[i] * s.world};
      for (int c = 0; c < 2; ++c) {
        s.muffled[c] += (sample[c] - s.muffled[c]) * dull;
        sample[c] = s.muffled[c] * (1 - .35f * s.muffle);
      }
      sample[0] += deckLeft[i]; sample[1] += deckRight[i];
      // A limiter: anything over the ceiling pulls the whole mix down at once
      // and lets it back up slowly, so an explosion is loud without clipping.
      const float loudest = std::max(std::abs(sample[0]), std::abs(sample[1]));
      const float wanted = loudest > .95f ? .95f / loudest : 1.f;
      s.limiter = wanted < s.limiter ? wanted : mix(s.limiter, wanted, .00012f);
      for (int c = 0; c < 2; ++c) {
        float value = sample[c] * s.limiter * s.master;
        if (!std::isfinite(value)) value = 0;
        value = std::clamp(value, -1.f, 1.f);
        loudestOut = std::max(loudestOut, std::abs(value));
        out[i * 2 + c] = value;
      }
    }
    done += std::size_t(count);
  }
  peak_.store(loudestOut, std::memory_order_relaxed);
  activeVoices_.store(active, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// The director
// ---------------------------------------------------------------------------
namespace {

// The pilot's own engines, which are never confused with anyone else's.
constexpr std::uint64_t kOwnJet = ~std::uint64_t{0};

// How far away a sound is as loud as it is at its source, and how far it carries.
struct Reach { double reference, limit; };
Reach reachOf(SoundKind kind) {
  switch (kind) {
    case SoundKind::Explosion: return {160, 18000};
    case SoundKind::Detonation: return {110, 14000};
    case SoundKind::Gun: return {32, 5000};
    case SoundKind::Crash: return {170, 17000};
    case SoundKind::PartBreak: return {70, 5000};
    case SoundKind::Eject: return {45, 4000};
    case SoundKind::Parachute: return {30, 1500};
    case SoundKind::MissileLaunch: return {80, 9000};
    case SoundKind::Crunch: return {60, 5000};
    case SoundKind::Splash: return {70, 6000};
    case SoundKind::ReheatLight: return {50, 6000};
    case SoundKind::HitTaken: return {200, 2000};
    case SoundKind::Flare: case SoundKind::Chaff: return {24, 2500};
    default: return {28, 2500};
  }
}

JetSound jetOf(const State& state, AircraftType type, std::uint64_t id) {
  JetSound jet;
  jet.id = id;
  const unsigned engines = validAircraftType(type) ? aircraftDefinition(type).flight.engine_count : 2;
  for (int e = 0; e < 2; ++e) {
    // A single-engined aircraft leaves the second slot silent.
    const bool running = unsigned(e) < engines && state.engine_health[e] > 0 && state.fuel_mass != 0;
    jet.power[e] = running && std::isfinite(state.n1[e]) ? float(std::clamp(state.n1[e], 0., 1.)) : -1.f;
    jet.reheat[e] = running && std::isfinite(state.afterburner[e]) ? float(std::clamp(state.afterburner[e], 0., 1.)) : 0.f;
  }
  switch (type) {
    case AircraftType::A320: jet.pitch = .82f; jet.fan = 1; break;
    case AircraftType::Su57: jet.pitch = .9f; break;
    case AircraftType::SR71: jet.pitch = .72f; break;
    case AircraftType::JF17: jet.pitch = 1.12f; break;
    default: jet.pitch = 1.05f; break;
  }
  return jet;
}

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}  // namespace

float SoundDirector::random() {
  seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
  return float(std::int32_t(seed_)) * (1.f / 2147483648.f);
}

void SoundDirector::at(SoundKind kind, const Vec3& position, float gain, bool own) {
  if (finite(position) && pending_.size() < 256) pending_.push_back({kind, position, gain, own});
}

void SoundDirector::cue(SoundKind kind, float gain, float pitch) {
  mixer_.play({kind, gain, 0, 0, 0, pitch, 0});
}

void SoundDirector::scrape(float amount) {
  if (std::isfinite(amount)) scrape_ = std::max(scrape_, double(std::clamp(amount, 0.f, 1.f)));
}

void SoundDirector::reset() {
  const auto seed = seed_;
  auto pending = std::move(pending_);
  pending.clear();
  const SoundScene previous = scene_;
  // Everything is forgotten except the listener's settings, which the next frame restates.
  eye_ = {}; forward_ = {1, 0, 0}; right_ = {0, 1, 0}; velocity_ = {};
  listenerKnown_ = cockpit_ = primed_ = false;
  reheat_ = {}; engineRunning_ = {true, true};
  missilesSeen_.clear();
  threats_ = 0;
  onWheels_ = true; radarLocked_ = seekerReady_ = false; alive_ = true;
  airborne_ = gearMotor_ = flap_ = flapMotor_ = scrape_ = 0;
  gearCommand_ = 1; health_ = 100; lockStep_ = 0;
  scene_ = {};
  scene_.master = previous.master;
  scene_.volume = previous.volume;
  seed_ = seed;
  pending_ = std::move(pending);
}

SoundDirector::Emitter SoundDirector::hear(const Vec3& position, const Vec3& velocity, const Vec3* nose,
                                           double reference, double reach) const {
  Emitter e;
  const Vec3 to = position - eye_;
  e.distance = to.norm();
  if (!std::isfinite(e.distance)) { e.distance = reach; return e; }
  const Vec3 direction = e.distance > 1e-3 ? to / e.distance : forward_;
  // Something close enough to touch is all around, not off to one side.
  e.pan = float(direction.dot(right_) * std::clamp(e.distance / 12, 0., 1.) * .7);
  e.gain = float(reference / (reference + e.distance) * std::clamp((reach - e.distance) / (reach * .3), 0., 1.));
  e.air = float(20000 / (1 + e.distance / 500));
  // Pitch rises as the two close and falls as they part.
  const double away = velocity.dot(direction), follow = velocity_.dot(direction);
  e.doppler = float(std::clamp((kSoundSpeed + follow) / std::max(kSoundSpeed * .4, kSoundSpeed + away), .45, 2.2));
  if (nose) e.rear = float(std::clamp(.5 + .5 * nose->dot(direction), 0., 1.));
  return e;
}

void SoundDirector::update(const SoundFrame& f) {
  const double dt = std::isfinite(f.dt) ? std::clamp(f.dt, 0., .25) : 0;
  const bool flying = f.own && f.alive && finite(f.own->pos_ned) && finite(f.own->vel_ned);

  // ---- The listener ----
  if (finite(f.eye) && finite(f.forward) && finite(f.up)) {
    const Vec3 forward = f.forward.normalized(), right = forward.cross(f.up).normalized();
    if (f.attached && f.own && finite(f.own->vel_ned)) {
      velocity_ = f.own->vel_ned;
    } else if (listenerKnown_ && dt > 1e-4) {
      // A free camera: how fast it is being flown about. A jump is not a speed.
      const Vec3 moved = (f.eye - eye_) / dt;
      if (moved.norm() < 1500) velocity_ += (moved - velocity_) * std::min(1., dt * 8);
    } else {
      velocity_ = {};
    }
    eye_ = f.eye;
    if (forward.norm2() > .5 && right.norm2() > .5) { forward_ = forward; right_ = right; }
    listenerKnown_ = true;
  }
  cockpit_ = f.cockpit;

  SoundScene scene;
  scene.master = f.enabled && (f.focused || !f.muteUnfocused) ? f.master : 0;
  scene.volume = f.volume;
  scene.world = f.held ? 0 : 1;
  scene.interior = f.cockpit ? 1 : 0;
  // Shot down, or with the blood gone from the head, the world goes dull.
  const float strain = float(std::isfinite(f.strain) ? std::clamp(f.strain, 0., 1.) : 0);
  scene.muffle = f.own && !f.alive ? 1 : f.unconscious ? 1 : strain * .85f;
  scene.strain = f.own && f.alive ? std::max(strain, f.unconscious ? 1.f : 0.f) : 0;

  // ---- Engines: the pilot's own, and the four nearest others ----
  if (flying) {
    const Vec3 nose = f.own->att.rotate({1, 0, 0});
    const Emitter heard = hear(f.own->pos_ned, f.own->vel_ned, &nose, 90, 14000);
    JetSound jet = jetOf(*f.own, f.type, kOwnJet);
    if (f.cockpit) {
      jet.gain = .85f; jet.pan = 0; jet.doppler = 1; jet.rear = .7f; jet.interior = 1; jet.air = 20000;
    } else {
      jet.gain = heard.gain; jet.pan = heard.pan; jet.doppler = heard.doppler; jet.rear = heard.rear; jet.air = heard.air;
    }
    scene.jets[0] = jet;
    scene.own = f.cockpit ? 1.f : float(std::clamp(60 / (30 + heard.distance), 0., 1.));
  }
  {
    struct Candidate { double distance; const SoundFrame::Aircraft* aircraft; };
    std::array<Candidate, SoundScene::kJets - 1> nearest{};
    std::size_t found = 0;
    for (const auto& other : f.others) {
      if (!other.state || !other.id || other.id == kOwnJet || !finite(other.state->pos_ned) ||
          !finite(other.state->vel_ned) || aircraftCrashed(*other.state))
        continue;
      Candidate candidate{(other.state->pos_ned - eye_).norm(), &other};
      if (candidate.distance > 14000) continue;
      // Kept in order of distance, nearest first.
      for (std::size_t i = 0; i < found; ++i)
        if (candidate.distance < nearest[i].distance) std::swap(candidate, nearest[i]);
      if (found < nearest.size()) nearest[found++] = candidate;
    }
    // An aircraft keeps the slot it had, so its engines are not restarted when
    // another one comes closer.
    std::array<bool, SoundScene::kJets> taken{true};
    std::array<bool, SoundScene::kJets - 1> placed{};
    const auto fill = [&](std::size_t slot, const Candidate& candidate) {
      const State& state = *candidate.aircraft->state;
      const Vec3 nose = state.att.rotate({1, 0, 0});
      const Emitter heard = hear(state.pos_ned, state.vel_ned, &nose, 90, 14000);
      JetSound jet = jetOf(state, candidate.aircraft->type, candidate.aircraft->id);
      jet.gain = heard.gain * (f.cockpit ? .6f : 1.f);
      jet.pan = heard.pan; jet.doppler = heard.doppler; jet.rear = heard.rear;
      jet.air = f.cockpit ? std::min(heard.air, 2500.f) : heard.air;
      scene.jets[slot] = jet;
      taken[slot] = true;
    };
    for (std::size_t i = 0; i < found; ++i)
      for (std::size_t slot = 1; slot < scene.jets.size(); ++slot)
        if (!taken[slot] && scene_.jets[slot].id == nearest[i].aircraft->id) { fill(slot, nearest[i]); placed[i] = true; break; }
    for (std::size_t i = 0; i < found; ++i) {
      if (placed[i]) continue;
      for (std::size_t slot = 1; slot < scene.jets.size(); ++slot)
        if (!taken[slot]) { fill(slot, nearest[i]); break; }
    }
  }
  // Reheat is heard to light, on any of them.
  for (std::size_t slot = 0; slot < scene.jets.size(); ++slot) {
    const JetSound& jet = scene.jets[slot];
    const float reheat = jet.id ? .5f * (jet.reheat[0] + jet.reheat[1]) : 0;
    if (primed_ && jet.id && jet.id == scene_.jets[slot].id && reheat >= .15f && reheat_[slot] < .15f) {
      const State* state = slot == 0 ? f.own : nullptr;
      for (const auto& other : f.others) if (other.id == jet.id) state = other.state;
      if (state) at(SoundKind::ReheatLight, state->pos_ned, 1, slot == 0);
    }
    reheat_[slot] = reheat;
  }

  // ---- Missiles: a launch is heard once, a motor for as long as it burns ----
  {
    std::vector<std::uint64_t> seen;
    seen.reserve(f.missiles.size());
    struct Candidate { double distance; const SoundFrame::Missile* missile; };
    std::array<Candidate, SoundScene::kMissiles> nearest{};
    std::size_t found = 0;
    for (const auto& missile : f.missiles) {
      if (!missile.id || !finite(missile.position) || !finite(missile.velocity)) continue;
      seen.push_back(missile.id);
      if (primed_ && missile.age < .6 && missile.powered &&
          std::find(missilesSeen_.begin(), missilesSeen_.end(), missile.id) == missilesSeen_.end())
        at(SoundKind::MissileLaunch, missile.position, 1, missile.own);
      if (!missile.powered) continue;
      Candidate candidate{(missile.position - eye_).norm(), &missile};
      if (candidate.distance > 8000) continue;
      for (std::size_t i = 0; i < found; ++i)
        if (candidate.distance < nearest[i].distance) std::swap(candidate, nearest[i]);
      if (found < nearest.size()) nearest[found++] = candidate;
    }
    missilesSeen_ = std::move(seen);
    std::array<bool, SoundScene::kMissiles> taken{}, placed{};
    const auto fill = [&](std::size_t slot, const Candidate& candidate) {
      const Emitter heard = hear(candidate.missile->position, candidate.missile->velocity, nullptr, 45, 8000);
      scene.missiles[slot] = {candidate.missile->id, heard.gain * (f.cockpit ? .65f : 1.f), heard.pan, heard.doppler,
                              f.cockpit ? std::min(heard.air, 2500.f) : heard.air};
      taken[slot] = true;
    };
    for (std::size_t i = 0; i < found; ++i)
      for (std::size_t slot = 0; slot < scene.missiles.size(); ++slot)
        if (!taken[slot] && scene_.missiles[slot].id == nearest[i].missile->id) { fill(slot, nearest[i]); placed[i] = true; break; }
    for (std::size_t i = 0; i < found; ++i) {
      if (placed[i]) continue;
      for (std::size_t slot = 0; slot < scene.missiles.size(); ++slot)
        if (!taken[slot]) { fill(slot, nearest[i]); break; }
    }
  }

  // ---- The air, and the pilot's own airframe in it ----
  const double tas = std::isfinite(f.instruments.tas) ? std::max(0., f.instruments.tas) : 0;
  scene.airspeed = float(f.attached && flying ? tas : velocity_.norm());
  scene.density = float(isaAtAltitude(std::clamp(-eye_.z, 0., 30000.)).rho / 1.225);
  scrape_ = std::max(0., scrape_ - dt * 4);
  if (flying) {
    const bool airborne = !f.onWheels;
    const double alpha = std::isfinite(f.instruments.alpha_deg) ? std::abs(f.instruments.alpha_deg) : 0;
    const double load = std::isfinite(f.instruments.g_load) ? std::abs(f.instruments.g_load) : 1;
    const double mach = std::isfinite(f.instruments.mach) ? f.instruments.mach : 0;
    const double pressure = std::isfinite(f.qbar) ? std::clamp(f.qbar / 9000, 0., 1.) : 0;
    if (airborne && tas > 40) {
      // The airframe shudders as the wing nears its limit, under heavy load,
      // and passing through the speed of sound.
      const double separated = std::clamp((alpha - 12) / 14, 0., 1.) * std::sqrt(pressure);
      const double loaded = std::clamp((load - 5.5) / 3.5, 0., 1.) * .6;
      const double transonic = std::clamp(1 - std::abs(mach - 1) / .06, 0., 1.) * .35;
      scene.buffet = float(std::max({separated, loaded, transonic, f.instruments.stall_warn ? .5 * std::sqrt(pressure) : 0.}));
    }
    const double brake = std::isfinite(f.own->spoiler) ? std::clamp(f.own->spoiler, 0., 1.) : 0;
    scene.airbrake = float(brake * std::clamp(tas * tas / (220. * 220.), 0., 1.));
    scene.gearDrag = float(airborne ? std::clamp(f.gearCommand, 0., 1.) * std::clamp(tas * tas / (110. * 110.), 0., 1.) : 0);
    scene.roll = float(f.onWheels ? std::hypot(f.own->vel_ned.x, f.own->vel_ned.y) : 0);
    scene.scrape = float(std::clamp(scrape_, 0., 1.));

    // The gear is heard travelling, and locking at the end of it.
    if (primed_ && std::abs(f.gearCommand - gearCommand_) > .5)
      gearMotor_ = std::max(.5, aircraftDefinition(f.type).visual.gearSeconds);
    else if (gearMotor_ > 0 && (gearMotor_ -= dt) <= 0)
      at(SoundKind::GearLock, f.own->pos_ned, 1, true);
    gearCommand_ = f.gearCommand;
    scene.gearMotor = gearMotor_ > 0 ? 1 : 0;
    const double flap = std::isfinite(f.own->flap) ? f.own->flap : 0;
    if (primed_ && dt > 0 && std::abs(flap - flap_) / dt > .01) flapMotor_ = .15;
    flapMotor_ = std::max(0., flapMotor_ - dt);
    flap_ = flap;
    scene.flapMotor = flapMotor_ > 0 ? 1 : 0;

    // Wheels meeting the runway after a flight.
    if (primed_ && f.onWheels && !onWheels_ && airborne_ > .5) {
      const double sink = std::max(0., f.own->vel_ned.z);
      at(SoundKind::Touchdown, f.own->pos_ned, float(std::clamp(.35 + sink / 4, .35, 1.3)), true);
    }
    airborne_ = f.onWheels ? 0 : airborne_ + dt;
    onWheels_ = f.onWheels;

    // Damage that needs the pilot's attention.
    for (int e = 0; e < 2; ++e) {
      const bool running = f.own->engine_health[e] > 0;
      if (primed_ && alive_ && engineRunning_[e] && !running) cue(SoundKind::Caution);
      engineRunning_[e] = running;
    }
    if (primed_ && alive_ && f.health < 40 && health_ >= 40) cue(SoundKind::Caution);
  } else {
    gearMotor_ = flapMotor_ = airborne_ = 0;
    engineRunning_ = {true, true};
  }
  if (primed_ && alive_ && f.own && !f.alive) cue(SoundKind::Killed);
  alive_ = !f.own || f.alive;
  health_ = f.health;

  // ---- The flight deck's warnings ----
  if (flying) {
    scene.stall = f.instruments.stall_warn && !f.onWheels ? 1 : 0;
    std::size_t live = 0;
    double urgency = 0;
    for (const auto& threat : f.threats) {
      if (threat.decoyed || !finite(threat.position) || !finite(threat.velocity)) continue;
      ++live;
      const Vec3 to = f.own->pos_ned - threat.position;
      const double range = to.norm();
      const double closing = range > 1 ? (threat.velocity - f.own->vel_ned).dot(to / range) : 0;
      urgency = std::max(urgency, std::clamp(1 - range / std::max(closing, 60.) / 12, .02, 1.));
    }
    scene.warning = float(urgency);
    if (primed_ && live > threats_) cue(SoundKind::MissileAlert);
    threats_ = live;

    const bool heat = f.missileSelected && f.weapon == weapons::WeaponType::Infrared;
    const bool radar = f.missileSelected && f.weapon == weapons::WeaponType::ActiveRadar;
    const double progress = std::isfinite(f.lockProgress) ? std::clamp(f.lockProgress, 0., 1.) : 0;
    if (heat && f.seekerReady) scene.seeker = 1;
    else if (heat && f.seekerTracking) scene.seeker = float(std::clamp(.08 + .9 * progress, .08, .98));
    // A radar missile counts its lock up in beeps, and says when it has it.
    const int step = radar && f.seekerTracking && !f.seekerReady ? 1 + int(progress * 4) : 0;
    if (primed_ && step > lockStep_) cue(SoundKind::LockBeep, .7f, .8f + .08f * float(step));
    lockStep_ = step;
    if (primed_ && radar && f.seekerReady && !seekerReady_) cue(SoundKind::LockBeep, 1, 1.25f);
    seekerReady_ = radar && f.seekerReady;
    if (primed_ && f.radarLocked != radarLocked_) cue(f.radarLocked ? SoundKind::LockBeep : SoundKind::LockLost);
    radarLocked_ = f.radarLocked;
  } else {
    threats_ = 0;
    lockStep_ = 0;
    seekerReady_ = radarLocked_ = false;
  }

  // ---- Everything that happened this frame, from where the camera is ----
  for (const auto& sound : pending_) {
    const Reach reach = reachOf(sound.kind);
    const Emitter heard = hear(sound.position, {}, nullptr, reach.reference, reach.limit);
    SoundEvent event;
    event.kind = sound.kind;
    event.gain = sound.gain * heard.gain;
    event.pan = heard.pan;
    event.distance = float(heard.distance);
    // What is close is heard as it is seen; the rest arrives when sound does.
    event.delay = float(std::max(0., heard.distance - 150) / kSoundSpeed);
    event.pitch = 1 + .06f * random();
    if (f.cockpit) {
      // Through the canopy. The pilot's own aircraft is felt through the seat.
      event.interior = sound.own ? .85f : .6f;
      if (sound.own) { event.gain = sound.gain * .8f; event.pan = 0; event.delay = 0; event.distance = 0; }
      else event.gain *= .7f;
    }
    if (event.gain > .002f) mixer_.play(event);
  }
  pending_.clear();

  scene_ = scene;
  mixer_.setScene(scene);
  primed_ = true;
}

}  // namespace ofs::client
