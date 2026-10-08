#include "effects.hpp"
#include "damage_visuals.hpp"
#include "ofs/atmosphere.hpp"
#include "ofs/ballistics.hpp"

#include <algorithm>
#include <cmath>

namespace ofs::client {
namespace {

// A cheap deterministic hash gives every particle its own variation without a
// random number generator or any global state.
float hashUnit(std::uint32_t seed) {
  seed ^= seed >> 16;
  seed *= 0x7feb352dU;
  seed ^= seed >> 15;
  seed *= 0x846ca68bU;
  seed ^= seed >> 16;
  return static_cast<float>(seed & 0xffffff) / static_cast<float>(0xffffff);
}

// Fraction of the effect budget each quality level may use. Effects are
// presentation only, so dropping them at low quality is always safe.
std::size_t budgetFor(EffectsQuality quality) {
  switch (quality) {
    case EffectsQuality::Off: return 0;
    case EffectsQuality::Low: return 1;
    case EffectsQuality::Medium: return 2;
    case EffectsQuality::High: return 4;
  }
  return 4;
}

}  // namespace

EffectPool::EffectPool(std::size_t capacity) : capacity_(capacity) {
  if (capacity_ > 0) effects_.reserve(capacity_);
}

bool EffectPool::spawn(Effect effect) {
  if (capacity_ == 0 || !std::isfinite(effect.position.norm2()) ||
      !std::isfinite(effect.velocity.norm2()) || !std::isfinite(effect.axis.norm2()) ||
      !std::isfinite(effect.stretch) || effect.stretch < 0 || !(effect.lifetime > 0) ||
      !std::isfinite(effect.lifetime) || !(effect.size > 0) ||
      !std::isfinite(effect.size) || !std::isfinite(effect.drag) || effect.drag < 0 ||
      !std::isfinite(effect.gravity) || !std::isfinite(effect.age) || effect.age < 0) return false;
  if (effect.kind==EffectKind::Vapor && effect.stretch>0 &&
      (effect.axis.norm2()<1e-12 || (!effect.billboard &&
       (!std::isfinite(effect.normal.norm2()) || effect.normal.cross(effect.axis).norm2()<1e-12)))) return false;
  if (effects_.size() < capacity_) {
    effects_.push_back(effect);
  } else {
    // Replace round-robin rather than always the oldest: a burst of tracers
    // must not keep evicting the single most recent explosion.
    effects_[cursor_] = effect;
    cursor_ = (cursor_ + 1) % capacity_;
    ++dropped_;
  }
  peak_ = std::max(peak_, effects_.size());
  return true;
}

void EffectPool::update(double dt) {
  if (!(dt > 0)) return;
  std::vector<Effect> contacts;
  std::size_t write = 0;
  for (std::size_t read = 0; read < effects_.size(); ++read) {
    Effect effect = effects_[read];
    effect.age += static_cast<float>(dt);
    if (effect.age >= effect.lifetime) continue;  // self-cleaning, no leak
    if (effect.kind == EffectKind::Tracer) {
      const auto end=effect.position+ballisticDisplacement(effect.velocity,dt);
      const auto contact=bulletTerrainFraction(effect.position,end);
      if (std::isfinite(contact)) {
        Effect flash; flash.kind=EffectKind::Impact;
        flash.position=effect.position+(end-effect.position)*contact+Vec3{0,0,-.06};
        flash.size=1.2f; flash.lifetime=.18f; flash.tint=0xff80d8ffu;
        contacts.push_back(flash);
        flash.kind=EffectKind::Dust; flash.size=1.8f; flash.lifetime=1.1f;
        flash.velocity={0,0,-1.8}; flash.tint=0x906b8cabu; contacts.push_back(flash);
        continue;
      }
    }
    effect.velocity = effect.velocity * std::exp(-effect.drag * dt);
    effect.position += effect.velocity * dt + Vec3{0,0,.5*effect.gravity*dt*dt};
    effect.velocity.z += effect.gravity * dt;
    if (effect.kind==EffectKind::Debris || effect.kind==EffectKind::Spark) {
      const auto terrain=sampleTerrain(effect.position.x,effect.position.y);
      if(effect.position.z>terrain.heightNed-.03) {
        effect.position.z=terrain.heightNed-.03;
        const double incoming=effect.velocity.dot(terrain.normalNed);
        if(incoming<0)effect.velocity-=terrain.normalNed*(incoming*1.18);
        effect.velocity=effect.velocity*std::exp(-5*dt);
      }
    }
    if (effect.smokeInterval > 0) {
      effect.smokeClock += static_cast<float>(dt);
      if (effect.smokeClock >= effect.smokeInterval) {
        effect.smokeClock = 0;
        Effect puff;
        puff.kind = EffectKind::Smoke;
        puff.position = effect.position;
        puff.velocity = effect.velocity * .08 + Vec3{0, 0, -1.2};
        puff.size = effect.size * 1.6f;
        puff.lifetime = 1.6f;
        puff.drag = .6f;
        puff.tint = 0xa0303438u;
        contacts.push_back(puff);
      }
    }
    if (read == cursor_) cursor_ = write;
    effects_[write] = effect;
    ++write;
  }
  effects_.resize(write);
  if (cursor_ >= effects_.size()) cursor_ = 0;
  for (const auto& contact : contacts) spawn(contact);
}

std::size_t EffectPool::countOf(EffectKind kind) const {
  std::size_t count = 0;
  for (const Effect& effect : effects_)
    if (effect.kind == kind) ++count;
  return count;
}
void EffectPool::retireProjectile(std::uint64_t id) {
  if (id) std::erase_if(effects_,[&](const auto& e){return e.projectile==id;});
  if (cursor_ >= effects_.size()) cursor_=0;
}

CombatEffects::CombatEffects(EffectPool& pool, EffectsQuality quality)
    : pool_(pool), quality_(quality) {}

void CombatEffects::onShot(const Vec3& position, const Vec3& velocity, double lifetime,
                           bool ownAircraft, std::uint64_t projectile, const Vec3& carrier) {
  const std::size_t budget = budgetFor(quality_);
  if (budget == 0) return;

  Effect tracer;
  tracer.kind = EffectKind::Tracer;
  tracer.projectile = projectile;
  tracer.gravity = static_cast<float>(kG0);
  tracer.position = position;
  tracer.velocity = velocity;
  tracer.lifetime = static_cast<float>(std::min(lifetime, 3.0));
  // A tracer is a stretched quad along its own velocity, not a billboard.
  tracer.billboard = false;
  tracer.stretch = 14.0f;
  tracer.size = 0.34f;
  tracer.tint = ownAircraft ? 0xff58c4ffu : 0xff3478ffu;
  pool_.spawn(tracer);

  const double speed = velocity.norm();
  if (speed < 1e-6) return;
  const Vec3 along = velocity / speed;
  // Everything at the muzzle stays with the gun for the instant it lasts.
  const Vec3 ride = carrier.norm2() > 0 ? carrier : velocity - along * std::min(speed, 950.);
  const auto seed = static_cast<std::uint32_t>(projectile * 2654435761u + ++shots_);
  const float a = hashUnit(seed), b = hashUnit(seed + 101);

  Effect flash;
  flash.kind = EffectKind::MuzzleFlash;
  flash.position = position + along * .35;
  flash.velocity = ride;
  flash.lifetime = 0.055f;
  flash.size = 0.55f + 0.5f * a;
  flash.seed = a;
  flash.tint = 0xff6ac8ffu;
  pool_.spawn(flash);
  if (budget < 2) return;
  // The jet of flame ahead of the barrel, and the light it throws.
  Effect tongue = flash;
  tongue.kind = EffectKind::Fire;
  tongue.position = position + along * (.9 + .5 * b);
  tongue.velocity = ride + along * 25.;
  tongue.size = 0.3f + 0.2f * b;
  tongue.lifetime = 0.05f;
  tongue.tint = 0xff58b8ffu;
  pool_.spawn(tongue);
  Effect glow = flash;
  glow.kind = EffectKind::Light;
  glow.size = 1.5f;
  glow.tint = 0x403c96ffu;
  pool_.spawn(glow);
  // Gun gas hangs behind the aircraft as a thin grey line of puffs.
  Effect gas;
  gas.kind = EffectKind::Smoke;
  gas.position = position + along * .5;
  gas.velocity = ride * .9 + along * 6. + Vec3{0, 0, -.6};
  gas.drag = 1.4f;
  gas.lifetime = 0.55f + 0.3f * b;
  gas.size = 0.45f + 0.3f * a;
  gas.seed = a;
  gas.tint = 0x34888c90u;
  pool_.spawn(gas);
  if (budget < 4 || shots_ % 2) return;
  // A spent case tumbling away under the wing.
  Effect brass;
  brass.kind = EffectKind::Debris;
  brass.position = position - along * 1.2;
  brass.velocity = ride + Vec3{(a - .5) * 6, (b - .5) * 6, 5 + 4 * a};
  brass.billboard = false;
  brass.stretch = 0.16f;
  brass.size = 0.05f;
  brass.gravity = static_cast<float>(kG0);
  brass.drag = 0.9f;
  brass.lifetime = 0.8f;
  brass.tint = 0xff58b4dcu;
  pool_.spawn(brass);
}

void CombatEffects::onHit(const Vec3& position, bool ownAircraft, std::uint64_t projectile,
                          const Vec3& targetVelocity) {
  pool_.retireProjectile(projectile);
  const std::size_t budget = budgetFor(quality_);
  if (budget == 0) return;

  Effect spark;
  spark.kind = EffectKind::Impact;
  spark.position = position;
  spark.velocity = targetVelocity;
  spark.lifetime = 0.16f;
  spark.size = ownAircraft ? 1.3f : 0.9f;
  spark.tint = 0xffc0f0ffu;
  pool_.spawn(spark);
  Effect flash = spark;
  flash.kind = EffectKind::Flash;
  flash.lifetime = 0.09f;
  flash.size = ownAircraft ? 2.6f : 1.9f;
  flash.seed = hashUnit(static_cast<std::uint32_t>(projectile) + 7);
  flash.tint = 0xff8cdcffu;
  pool_.spawn(flash);

  if (budget < 2) return;
  Effect smoke;
  smoke.kind = EffectKind::Smoke;
  smoke.position = position;
  smoke.velocity = targetVelocity * .7 + Vec3{0, 0, -1.2};
  smoke.drag = 1.2f;
  smoke.lifetime = 0.9f;
  smoke.size = 1.0f;
  smoke.tint = 0xa0484c50u;
  pool_.spawn(smoke);

  if (budget < 4) return;
  // A radial spray retains a white-hot core and slower orange spark tails.
  const auto seed=static_cast<std::uint32_t>(std::abs(position.x*31+position.y*17));
  for (unsigned i=0;i<12;++i) {
    const float a=hashUnit(seed+i*7919), b=hashUnit(seed+i*3571+3);
    Effect debris=spark; debris.kind=EffectKind::Spark;
    debris.lifetime=.3f+b*.45f; debris.size=.045f+b*.06f;
    debris.velocity=targetVelocity*.85+Vec3{std::cos(a*6.283)* (5+14*b), std::sin(a*6.283)*(5+14*b),-3-9*b};
    debris.billboard=false; debris.stretch=.5f+b*1.1f;
    debris.gravity=float(kG0); debris.drag=1.3f; debris.tint=0xff60baffu;
    pool_.spawn(debris);
  }
  // Torn skin: a few dark fragments left tumbling in the slipstream.
  for (unsigned i=0;i<5;++i) {
    const float a=hashUnit(seed+i*1291+11), b=hashUnit(seed+i*2909+5);
    Effect chip;
    chip.kind=EffectKind::Debris;
    chip.position=position;
    chip.velocity=targetVelocity*.6+Vec3{(a-.5)*16,(b-.5)*16,-2-8*a};
    chip.billboard=false; chip.stretch=.25f+.3f*b; chip.size=.07f+.08f*a;
    chip.gravity=float(kG0); chip.drag=.8f; chip.lifetime=.9f+b;
    chip.tint=0xff34383cu;
    pool_.spawn(chip);
  }
}

void CombatEffects::updateMissile(std::uint64_t id, Vec3 position, Vec3 velocity, Quat attitude,
                                  double length, double diameter, double age, bool powered, double dt) {
  if (quality_ == EffectsQuality::Off || dt <= 0) return;
  auto& emitter = missiles_[id];
  emitter.seen = true;
  const Vec3 aft = attitude.rotate({-1, 0, 0});
  const Vec3 nozzle = position + aft * (length * .5);
  if (!emitter.primed) {
    emitter.primed = true;
    emitter.nozzle = nozzle;
  }
  if (!powered) {
    emitter.nozzle = nozzle;
    emitter.lit = false;
    return;
  }
  const auto seed = static_cast<std::uint32_t>(id * 2654435761u);
  if (!emitter.lit && age < 2) {
    // Ignition: a flash at the nozzle and a ring of exhaust blown back along
    // the launch line, left behind as the missile pulls away.
    emitter.lit = true;
    Effect flash;
    flash.kind = EffectKind::MuzzleFlash;
    flash.position = nozzle;
    flash.velocity = velocity;
    flash.lifetime = .14f;
    flash.size = float(diameter * 16);
    flash.tint = 0xff8ad8ffu;
    pool_.spawn(flash);
    for (unsigned i = 0; i < (quality_ == EffectsQuality::High ? 10u : 5u); ++i) {
      const float a = hashUnit(seed + i * 7919) * 6.2832f, b = hashUnit(seed + i * 3571 + 5);
      Effect puff;
      puff.kind = EffectKind::Smoke;
      puff.position = nozzle + aft * (b * 2.5);
      puff.velocity = velocity * .35 + aft * (20 + 30 * b) +
                      attitude.rotate({0, std::cos(a), std::sin(a)}) * (2 + 5 * b);
      puff.drag = 2.2f;
      puff.lifetime = 1.4f + b;
      puff.size = float(diameter * (7 + 6 * b));
      puff.tint = 0xb0dcdad6u;
      pool_.spawn(puff);
    }
  }
  emitter.lit = true;
  // The plume itself is drawn by the renderer as a volume; here is the light
  // it throws, which rides with the missile for one frame at a time.
  Effect glow;
  glow.kind = EffectKind::Light;
  glow.position = nozzle + aft * (diameter * 3);
  glow.velocity = velocity;
  glow.lifetime = float(std::clamp(dt * 1.5, .02, .08));
  glow.size = float(diameter * 4.5);
  glow.tint = 0x7090d8ffu;
  pool_.spawn(glow);
  // Smoke is laid in lengths between successive nozzle positions, so the trail
  // is unbroken at any speed and frame rate.
  emitter.sinceSegment += dt;
  const Vec3 run = nozzle - emitter.nozzle;
  const double distance = run.norm();
  const double interval = quality_ == EffectsQuality::High ? 1. / 60 : 1. / 30;
  if (distance > 400) {
    emitter.nozzle = nozzle;  // a correction, not flight
  } else if (emitter.sinceSegment >= interval && distance > .2) {
    Effect smoke;
    smoke.kind = EffectKind::Trail;
    smoke.position = emitter.nozzle + run * .5;
    smoke.axis = run / distance;
    smoke.stretch = float(distance);
    smoke.velocity = aft * 6 + Vec3{0, 0, -.4};
    smoke.drag = 1.5f;
    smoke.size = float(diameter * 3.4);
    smoke.lifetime = quality_ == EffectsQuality::High ? 6.5f : 3.5f;
    smoke.tint = 0xa8e8e6e2u;
    pool_.spawn(smoke);
    emitter.nozzle = nozzle;
    emitter.sinceSegment = 0;
  }
}

void CombatEffects::retireMissiles(std::size_t active) {
  if (missiles_.size() > active)
    std::erase_if(missiles_, [](const auto& entry) { return !entry.second.seen; });
  for (auto& [id, emitter] : missiles_) {
    (void)id;
    emitter.seen = false;
  }
}

void CombatEffects::onDestroyed(const Vec3& position, const Vec3& velocity) {
  const std::size_t budget = budgetFor(quality_);
  if (budget == 0) return;

  Effect fireball;
  fireball.kind = EffectKind::Explosion;
  fireball.position = position;
  fireball.velocity = velocity * .25;
  fireball.drag = 1.5f;
  fireball.lifetime = 1.25f;
  fireball.size = 15.0f;
  fireball.tint = 0xff40a0ffu;
  pool_.spawn(fireball);
  Effect flash;
  flash.kind = EffectKind::Flash;
  flash.position = position;
  flash.lifetime = 0.14f;
  flash.size = 24.0f;
  flash.tint = 0xffa0e0ffu;
  pool_.spawn(flash);

  if (budget >= 2) {
    Effect ring;
    ring.kind = EffectKind::Shockwave;
    ring.position = position;
    ring.lifetime = 0.5f;
    ring.size = 55.0f;
    ring.tint = 0x40ffffffu;
    pool_.spawn(ring);
    // Fuel burning off in ragged tongues around the main fireball.
    for (unsigned i=0;i<18;++i) {
      const float a=hashUnit(i*3571+19), b=hashUnit(i*7919+41);
      Effect flame=fireball; flame.kind=EffectKind::Fire;
      flame.position+=Vec3{(a-.5)*6,(b-.5)*6,-a*3};
      flame.velocity=velocity*.3+Vec3{(a-.5)*26,(b-.5)*26,-4-b*14};
      flame.size=2+a*3.5f; flame.lifetime=.6f+b*1.1f; flame.seed=a;
      flame.drag=1.8f; pool_.spawn(flame);
    }
  }
  if (budget < 2) return;
  // A column of smoke carried on with the wreck, each puff rising and spreading.
  const int puffs = budget >= 4 ? 26 : 9;
  for (int i = 0; i < puffs; ++i) {
    const float phase = hashUnit(static_cast<std::uint32_t>(i * 7919 + 13));
    Effect smoke;
    smoke.kind = EffectKind::Smoke;
    smoke.position = position + velocity * (0.015 * i) + Vec3{(phase-.5)*7, std::sin(i*2.4)*3,-phase*5};
    smoke.velocity = velocity * .12 + Vec3{(phase - 0.5) * 3.0, (phase - 0.5) * 3.0, -2.2 - phase * 1.6};
    smoke.lifetime = 5.5f + phase * 3.5f;
    smoke.size = 3.2f + phase * 3.4f;
    smoke.seed = phase;
    smoke.tint = 0xb03c4046u;
    smoke.drag = 0.25f;
    pool_.spawn(smoke);
  }
  if (budget < 4) return;
  // Wreckage thrown clear. The larger pieces are alight and trail smoke as they fall.
  for (int i = 0; i < 28; ++i) {
    const float a = hashUnit(static_cast<std::uint32_t>(i * 2654435761u));
    const float b = hashUnit(static_cast<std::uint32_t>(i * 40503u + 7));
    const double theta = a * 6.2831853;
    const double elevation = (b - 0.5) * 1.6;
    const double speed = 20.0 + a * 50.0;
    Effect debris;
    debris.kind = EffectKind::Debris;
    debris.position = position;
    debris.velocity = Vec3{std::cos(theta) * speed * std::cos(elevation),
                           std::sin(theta) * speed * std::cos(elevation), -std::sin(elevation) * speed - 6};
    debris.velocity = debris.velocity + velocity * 0.7;
    debris.lifetime = 2.6f + b * 2.2f;
    debris.size = 0.4f + a * 0.8f;
    debris.stretch = 2.2f + 2 * b;
    debris.billboard = false;
    debris.tint = i % 3 == 0 ? 0xff2878ffu : 0xff383a3cu;
    debris.gravity = 9.81f;
    debris.drag = 0.35f;
    if (i % 3 == 0) debris.smokeInterval = 0.07f;
    pool_.spawn(debris);
  }
}

void CombatEffects::onDetonation(const Vec3& position) {
  const std::size_t budget = budgetFor(quality_);
  if (budget == 0) return;
  // A warhead is a sharp white flash and a ball of fragments, gone in a moment,
  // leaving a knot of grey smoke.
  Effect flash;
  flash.kind = EffectKind::Flash;
  flash.position = position;
  flash.lifetime = 0.1f;
  flash.size = 13.0f;
  flash.tint = 0xffc8f0ffu;
  pool_.spawn(flash);
  Effect fireball;
  fireball.kind = EffectKind::Explosion;
  fireball.position = position;
  fireball.lifetime = 0.55f;
  fireball.size = 6.5f;
  fireball.tint = 0xff58b0ffu;
  pool_.spawn(fireball);
  if (budget < 2) return;
  Effect ring;
  ring.kind = EffectKind::Shockwave;
  ring.position = position;
  ring.lifetime = 0.32f;
  ring.size = 30.0f;
  ring.tint = 0x38ffffffu;
  pool_.spawn(ring);
  const auto seed = static_cast<std::uint32_t>(std::abs(position.x * 13 + position.y * 7 + position.z * 3));
  const unsigned puffs = budget >= 4 ? 12 : 5;
  for (unsigned i = 0; i < puffs; ++i) {
    const float a = hashUnit(seed + i * 977), b = hashUnit(seed + i * 613 + 9), c = hashUnit(seed + i * 389 + 21);
    Effect smoke;
    smoke.kind = EffectKind::Smoke;
    smoke.position = position + Vec3{(a - .5) * 4, (b - .5) * 4, (c - .5) * 4};
    smoke.velocity = Vec3{(a - .5) * 14, (b - .5) * 14, (c - .5) * 14 - 1};
    smoke.drag = 2.2f;
    smoke.lifetime = 2.6f + 2 * c;
    smoke.size = 2.4f + 2.2f * a;
    smoke.seed = b;
    smoke.tint = 0xa8585c60u;
    pool_.spawn(smoke);
  }
  if (budget < 4) return;
  // Fragments leave in every direction as short hot streaks.
  for (unsigned i = 0; i < 36; ++i) {
    const float a = hashUnit(seed + i * 7919), b = hashUnit(seed + i * 3571 + 3);
    const double theta = a * 6.2831853, z = b * 2 - 1, r = std::sqrt(std::max(0., 1 - z * z));
    const double speed = 90 + 110 * hashUnit(seed + i * 131 + 5);
    Effect fragment;
    fragment.kind = EffectKind::Spark;
    fragment.position = position;
    fragment.velocity = Vec3{r * std::cos(theta), r * std::sin(theta), z} * speed;
    fragment.billboard = false;
    fragment.stretch = 2.4f;
    fragment.size = 0.09f;
    fragment.gravity = float(kG0);
    fragment.drag = 2.4f;
    fragment.lifetime = 0.35f + 0.4f * b;
    fragment.tint = 0xff70c8ffu;
    pool_.spawn(fragment);
  }
}

void CombatEffects::onPartLost(const Vec3& position, const Vec3& velocity) {
  const std::size_t budget = budgetFor(quality_);
  if (budget == 0) return;
  Effect flash;
  flash.kind = EffectKind::Flash;
  flash.position = position;
  flash.velocity = velocity;
  flash.lifetime = 0.1f;
  flash.size = 3.0f;
  flash.tint = 0xff80d0ffu;
  pool_.spawn(flash);
  Effect puff;
  puff.kind = EffectKind::Smoke;
  puff.position = position;
  puff.velocity = velocity * .5;
  puff.drag = 1.2f;
  puff.lifetime = 1.8f;
  puff.size = 2.6f;
  puff.tint = 0xa0585c62u;
  pool_.spawn(puff);
  if (budget < 2) return;
  const auto seed = static_cast<std::uint32_t>(std::abs(position.x * 29 + position.y * 11));
  const unsigned count = budget >= 4 ? 16 : 7;
  for (unsigned i = 0; i < count; ++i) {
    const float a = hashUnit(seed + i * 811), b = hashUnit(seed + i * 499 + 3);
    Effect fragment;
    fragment.kind = i % 2 ? EffectKind::Spark : EffectKind::Debris;
    fragment.position = position;
    fragment.velocity = velocity * .8 + Vec3{(a - .5) * 22, (b - .5) * 22, -3 - 10 * a};
    fragment.billboard = false;
    fragment.stretch = i % 2 ? .9f : .5f + .6f * b;
    fragment.size = i % 2 ? .06f : .12f + .14f * a;
    fragment.gravity = float(kG0);
    fragment.drag = .9f;
    fragment.lifetime = .7f + 1.2f * b;
    fragment.tint = i % 2 ? 0xff60baffu : 0xff3a3e42u;
    pool_.spawn(fragment);
  }
}

void CombatEffects::onPieceSmoke(const Vec3& position, const Vec3& velocity, bool burning) {
  if (budgetFor(quality_) < 2) return;
  Effect puff;
  puff.kind = EffectKind::Smoke;
  puff.position = position;
  puff.velocity = {0, 0, -.8};
  puff.lifetime = burning ? 4.5f : 2.2f;
  puff.size = burning ? 3.2f : 1.3f;
  puff.seed = hashUnit(++shots_);
  puff.tint = burning ? 0xc02a2c30u : 0x80484c52u;
  pool_.spawn(puff);
  if (!burning) return;
  // A wreck burns all the way down.
  Effect flame;
  flame.kind = EffectKind::Fire;
  flame.position = position;
  flame.velocity = velocity * .9;
  flame.lifetime = .22f;
  flame.size = 2.2f + 1.6f * puff.seed;
  flame.seed = puff.seed;
  flame.tint = 0xff48a8ffu;
  pool_.spawn(flame);
}

void CombatEffects::laySmoke(Vec3& from, const Vec3& to, const Vec3& drift, float size, float lifetime,
                             std::uint32_t tint) {
  const Vec3 run = to - from;
  const double distance = run.norm();
  // A jump is a respawn or a correction, not flight: nothing is drawn across it.
  if (distance > .05 && distance < 300) {
    Effect smoke;
    smoke.kind = EffectKind::Trail;
    smoke.position = from + run * .5;
    smoke.axis = run / distance;
    smoke.stretch = float(distance);
    smoke.velocity = drift;
    smoke.drag = .8f;
    smoke.size = size;
    smoke.lifetime = lifetime;
    smoke.tint = tint;
    pool_.spawn(smoke);
  }
  from = to;
}

void CombatEffects::onGroundImpact(const Simulator::GroundImpact& impact) {
  const std::size_t budget=budgetFor(quality_);
  if(!budget || (impact.closingSpeed<4 && impact.scrapeSpeed<8))return;
  if(impact.closingSpeed<4 && impact.damage<.02) {
    if(scrapeClock_<.1)return;
    scrapeClock_=0;
  }
  const bool asphalt=std::abs(impact.position.y)<25 && std::abs(impact.position.x)<1300;
  const unsigned count=unsigned(budget)*(impact.damage>.05?6:2);
  const std::uint32_t seed=static_cast<std::uint32_t>(std::abs(impact.position.x*31+impact.position.y*17));
  for(unsigned i=0;i<count;++i) {
    const float a=hashUnit(seed+i*733),b=hashUnit(seed+i*911+5);
    Effect e;e.position=impact.position+impact.normal*.12;
    e.velocity=impact.velocity*.06+impact.normal*(1+4*b)+Vec3{(a-.5)*7,(b-.5)*7,0};
    e.kind=asphalt?EffectKind::Smoke:EffectKind::Dust;
    e.lifetime=1.5f+b*2;e.size=.8f+a*1.8f;e.drag=.8f;
    e.tint=asphalt?0x786a6a6au:0x906a8ba5u;pool_.spawn(e);
    if(impact.bodyContact && (asphalt || impact.damage>.02)) {
      e.kind=EffectKind::Spark;e.billboard=false;e.stretch=.6f;e.size=.08f;
      e.velocity=impact.velocity*.2+impact.normal*(3+6*b)+Vec3{(a-.5)*14,(b-.5)*14,0};
      e.gravity=float(kG0);e.drag=.4f;e.lifetime=.4f+b*.8f;e.tint=0xff55c0ffu;pool_.spawn(e);
    }
  }
}

void CombatEffects::updateAircraft(const State& aircraft, double dt, AircraftType type,
                                   std::uint64_t id, double load, double health, const Weather& weather) {
  if (!(dt > 0)) return;
  const std::size_t budget = budgetFor(quality_);
  if (budget == 0) return;

  if (!emitters_.contains(id) && emitters_.size() >= 64) emitters_.erase(emitters_.begin());
  auto& emitter = emitters_[id];
  if (!emitter.primed || (aircraft.pos_ned-emitter.previous).norm()>100) {
    emitter.previous=aircraft.pos_ned;
    emitter.primed=true;
  }
  if(airframeIntegrity(aircraft)>emitter.integrity+.1) emitter.integrity=1;
  if(aircraftCrashed(aircraft) && emitter.integrity>.001)
    onDestroyed(aircraft.pos_ned,aircraft.vel_ned);
  emitter.integrity=airframeIntegrity(aircraft);
  emitter.time += std::min(dt, 0.1);
  const auto& definition = aircraftDefinition(type);
  const auto& visual = definition.visual;
  const auto cg=loadedCg(definition.flight,aircraft);
  const double altitude = -aircraft.pos_ned.z;
  const Vec3 airflow = aircraft.vel_ned-weather.wind_ned;
  const double speed = airflow.norm();
  const Vec3 bodyFlow = aircraft.att.inverseRotate(airflow);
  const double alpha = std::abs(std::atan2(bodyFlow.z,bodyFlow.x));
  const auto air = isaAtAltitude(altitude,weather.temp_offset_c);
  // Qualitative local expansion/cooling model, not Su-57 CFD. Supersaturation
  // follows local temperature and humidity, rather than a load-only switch.
  // Background: NASA NTRS 19880034912, natural condensation flow visualization.
  const double humidity = std::isfinite(humidity_) ? clamp(humidity_,0.,1.) : 0.;
  const double demand = std::max(clamp((std::abs(load)-1.8)/3.,0.,1.),
                                 clamp((alpha-12*kDeg2Rad)/(23*kDeg2Rad),0.,1.));
  const double qbar = .5*air.rho*speed*speed;
  const double pressureRatio = clamp(1.-qbar*(.3+.8*demand)/air.pressure,.65,1.);
  const double localTemp = air.temp*std::pow(pressureRatio,2./7.);
  const auto saturation = [](double kelvin) {
    const double celsius=clamp(kelvin-273.15,-70.,50.);
    return 610.94*std::exp(17.625*celsius/(celsius+243.04));
  };
  const double localHumidity = humidity*saturation(air.temp)*pressureRatio/saturation(localTemp);
  const double vaporStrength = demand*clamp((localHumidity-1.)/.4,0.,1.);
  const auto terrain = sampleTerrain(aircraft.pos_ned.x,aircraft.pos_ned.y);
  const bool airborne = terrain.heightNed-aircraft.pos_ned.z > 10.;
  if(id==0)scrapeClock_+=std::min(dt,.1);
  emitter.groundTime+=std::min(dt,.1);
  if(emitter.groundTime>.10) {
    emitter.groundTime=0;
    if(aircraftCrashed(aircraft)) {
      const float variation=hashUnit(++emitter.sequence);
      Effect smoke;smoke.kind=EffectKind::Smoke;smoke.position=aircraft.pos_ned;
      smoke.velocity={1.2,.4,-3};smoke.drag=.12f;smoke.size=2.5f+variation*2;
      smoke.lifetime=5.f;smoke.tint=0xb0353a40u;pool_.spawn(smoke);
      if(aircraft.fuel_mass>0) {
        Effect fire=smoke;fire.kind=EffectKind::Fire;fire.velocity={0,0,-2};
        fire.size=1.2f+variation;fire.lifetime=.65f;fire.tint=0xe02585ffu;
        pool_.spawn(fire);
      }
    }
  }
  // ---- Battle damage: smoke and fire from the parts that were hit ----
  const DamageView damage = damageView(aircraft, health);
  if (!aircraftCrashed(aircraft)) {
    const bool underway = (aircraft.pos_ned - emitter.previous).norm2() > .0004;
    const Vec3 drift = weather.wind_ned + Vec3{0, 0, -.5};
    emitter.smokeClock += std::min(dt, .1);
    const bool lay = emitter.smokeClock >= (quality_ == EffectsQuality::High ? 1. / 40 : 1. / 20);
    if (lay) emitter.smokeClock = 0;
    const bool lasting = quality_ == EffectsQuality::High;
    for (std::size_t index = 0; index < damagePartCount; ++index) {
      const DamagePart part = DamagePart(index);
      const float hurt = damage.parts[index];
      const bool isEngine = part == DamagePart::LeftEngine || part == DamagePart::RightEngine;
      const bool isWing = part == DamagePart::LeftWing || part == DamagePart::RightWing;
      // A torn wing smokes from where it now ends.
      const Vec3 body = isWing ? wingPoint(type, part == DamagePart::RightWing,
                                           std::clamp(wingRemaining(hurt) - .1, .05, .5))
                               : damagePoint(type, part);
      const Vec3 source = aircraft.pos_ned + aircraft.att.rotate(body - cg);
      // An engine going from running to wrecked is seen: a flash and a belch of flame.
      if (isEngine && hurt >= 1 && emitter.damage[index] < 1 && emitter.damageKnown && budget >= 2) {
        Effect burst;
        burst.kind = EffectKind::Flash;
        burst.position = source;
        burst.velocity = aircraft.vel_ned;
        burst.lifetime = .12f;
        burst.size = 3.5f;
        burst.tint = 0xff70c0ffu;
        pool_.spawn(burst);
        burst.kind = EffectKind::Explosion;
        burst.lifetime = .3f;
        burst.size = 1.8f;
        burst.tint = 0xff48a0ffu;
        pool_.spawn(burst);
      }
      emitter.damage[index] = hurt;
      // What each part gives off, once it is hurt enough to show.
      const float threshold = isEngine ? .3f : part == DamagePart::Fuselage ? .35f : .3f;
      if (hurt < threshold || budget < 2 || !underway) {
        emitter.smoking[index] = false;
        continue;
      }
      if (!emitter.smoking[index]) {
        emitter.smoking[index] = true;
        emitter.smokeFrom[index] = source;
      }
      if (!lay) continue;
      const float strength = std::clamp((hurt - threshold) / (1 - threshold), 0.f, 1.f);
      if (isEngine && hurt >= 1) {
        // Burnt out: oily black smoke, with flame at the jet pipe while fuel remains.
        laySmoke(emitter.smokeFrom[index], source, drift, float(visual.radius * .075), lasting ? 5.5f : 3.f, 0xd0202226u);
        if (aircraft.fuel_mass != 0) {
          Effect flame;
          flame.kind = EffectKind::Fire;
          flame.position = source + aircraft.att.rotate({-visual.radius * .05 * (1 + hashUnit(emitter.sequence + 5)), 0, 0});
          flame.velocity = aircraft.vel_ned * .97;
          flame.lifetime = .09f;
          flame.size = float(visual.radius * (.05 + .035 * hashUnit(emitter.sequence + 9)));
          flame.seed = hashUnit(emitter.sequence + 13);
          flame.tint = 0xff48a8ffu;
          pool_.spawn(flame);
        }
      } else if (isEngine) {
        laySmoke(emitter.smokeFrom[index], source, drift, float(visual.radius * .045), lasting ? 3.f : 2.f,
                 (std::uint32_t(70 + 110 * strength) << 24) | 0x00585c60u);
      } else if (isWing) {
        // Fuel and vapour streaming from a holed wing; darker smoke from a stub.
        const bool gone = hurt >= 1;
        laySmoke(emitter.smokeFrom[index], source, drift, float(visual.radius * (gone ? .05 : .028)),
                 lasting ? (gone ? 3.5f : 1.8f) : 1.4f,
                 gone ? 0xb0484a50u : (std::uint32_t(28 + 46 * strength) << 24) | 0x00d8dadcu);
      } else {
        laySmoke(emitter.smokeFrom[index], source, drift, float(visual.radius * (.03 + .03 * strength)), lasting ? 3.f : 2.f,
                 (std::uint32_t(60 + 110 * strength) << 24) | 0x00505458u);
        if (part == DamagePart::Fuselage && hurt > .7f && aircraft.fuel_mass != 0 && budget >= 4) {
          Effect flame;
          flame.kind = EffectKind::Fire;
          flame.position = source;
          flame.velocity = aircraft.vel_ned * .97;
          flame.lifetime = .08f;
          flame.size = float(visual.radius * .04);
          flame.seed = hashUnit(emitter.sequence + 17);
          flame.tint = 0xff48a8ffu;
          pool_.spawn(flame);
        }
      }
    }
  }
  emitter.damageKnown = true;
  auto nozzleRotate=[&](unsigned engine,const Vec3& v){const auto axis=definition.flight.engines[engine].vector_axis.normalized();const double a=aircraft.nozzle_angle[engine];return v*std::cos(a)+axis.cross(v)*std::sin(a)+axis*(axis.dot(v)*(1-std::cos(a)));};
  auto nozzleExit=[&](unsigned engine){const auto& physical=definition.flight.engines[engine];const auto hinge=physical.articulated_nozzle?physical.nozzle_pivot:physical.position;return hinge-cg+nozzleRotate(engine,visual.exhaust[engine]-hinge);};
  // Shared interval per aircraft, never per rendered frame. Contrail particles
  // stay in the air instead of travelling along with their aircraft.
  const bool moving=(aircraft.pos_ned-emitter.previous).norm2()>.0004;
  const bool vapor = vapor_ && moving && airborne && !aircraftCrashed(aircraft) &&
      quality_ >= EffectsQuality::Medium && speed > 65 && air.temp > 238 && vaporStrength > .01;
  const bool trailing=contrails_ && moving && quality_ >= EffectsQuality::Medium && altitude > 7000 && speed > 90 && humidity > .5;
  const double interval = vapor ? std::clamp(.75/speed, .004, .02) :
                          trailing ? std::clamp(2.0/speed, .008, .04) :
                                    (quality_ == EffectsQuality::High ? 0.04 : 0.08);
  while (emitter.time >= interval) {
    emitter.time -= interval;
    const float variation = hashUnit(++emitter.sequence + static_cast<std::uint32_t>(id));
    for (unsigned side=0;side<2;++side) {
      // A wingtip that has been shot away takes its light with it.
      if (wingRemaining(damage[side==0 ? DamagePart::LeftWing : DamagePart::RightWing])<1) continue;
      Effect light;
      light.kind=EffectKind::Light;
      light.position=aircraft.pos_ned+aircraft.att.rotate(visual.wingtip[side]-cg);
      light.size=.16f; light.lifetime=static_cast<float>(interval*1.2);
      light.tint=side==0 ? 0xe02020ffu : 0xe040ff40u;
      pool_.spawn(light);
      if (definition.type==AircraftType::A320 && emitter.sequence%28<2) {
        light.size=.65f; light.tint=0xc0ffffffu; pool_.spawn(light);
      }
    }
    // Broad cold-layer envelope, not a humidity/Schmidt-Appleman model. The
    // warm upper stratosphere no longer produces permanent trails.
    if (trailing && altitude > 7000 &&
        air.temp<233 && speed > 90) {
      for (unsigned engine = 0; engine < definition.flight.engine_count; ++engine) {
        if (aircraft.engine_health[engine] <= 0 || aircraft.fuel_mass == 0 || aircraft.n1[engine] < .2) continue;
        Effect trail;
        trail.kind = EffectKind::Contrail;
        const double fraction=1.0-std::clamp(emitter.time/std::min(dt,.1),0.0,1.0);
        trail.position = emitter.previous+(aircraft.pos_ned-emitter.previous)*fraction +
                         aircraft.att.rotate(nozzleExit(engine));
        trail.velocity = weather.wind_ned + Vec3{0, 0, -0.05};
        trail.lifetime = 8.0f;
        trail.size = 1.1f + .25f*variation;
        trail.tint = 0x38f0eeeau;
        pool_.spawn(trail);
      }
    }
    if (vapor) {
      const double fraction=1.-clamp(emitter.time/std::min(dt,.1),0.,1.);
      const Vec3 position=emitter.previous+(aircraft.pos_ned-emitter.previous)*fraction;
      for (const Vec3& tip : visual.wingtip) {
        Effect puff;
        puff.kind=EffectKind::Vapor;
        puff.position=position+aircraft.att.rotate(tip-cg);
        puff.velocity=weather.wind_ned;
        puff.lifetime=static_cast<float>(clamp(8./speed,.025,.10));
        puff.size=static_cast<float>(.08+.10*vaporStrength);
        puff.stretch=static_cast<float>(.9+.6*vaporStrength);
        puff.axis=airflow.normalized();
        puff.tint=(static_cast<std::uint32_t>(48*vaporStrength)<<24)|0x00fffaf4u;
        // Historical emissions already have an age at the end of this frame.
        // Giving every sample age zero stacks bright bands on slow frames.
        puff.age=static_cast<float>(emitter.time);
        puff.position+=weather.wind_ned*puff.age;
        if(puff.age<puff.lifetime) pool_.spawn(puff);
        if (type == AircraftType::Su57) {
          // A single translucent layer follows the wing plane. Keeping the
          // width and overlap small avoids a cloud-like ridge in exterior views.
          const unsigned patches=quality_==EffectsQuality::High ? 7 : 4;
          const double side=tip.y<0 ? -1. : 1.;
          const Vec3 root{3.1,side*1.25,tip.z};
          const Vec3 sweep=tip-root;
          puff.axis=aircraft.att.rotate(sweep.normalized());
          puff.normal=aircraft.att.rotate({0,0,load<0?1.:-1.});
          puff.billboard=false;
          for (unsigned patch=0;patch<patches;++patch) {
            const double span=(patch+.35)/patches;
            const double ripple=hashUnit(emitter.sequence+patch*37)-.5;
            const Vec3 wing=root+sweep*span+Vec3{ripple*.12,0,load<0?.30:-.30};
            puff.position=position+aircraft.att.rotate(wing-cg)+weather.wind_ned*puff.age;
            puff.lifetime=static_cast<float>(clamp((3.0-1.2*span)/speed,.010,.04));
            puff.stretch=static_cast<float>(sweep.norm()/patches*1.35);
            puff.size=static_cast<float>((.22+.16*vaporStrength)*(1.-.3*span));
            puff.tint=(static_cast<std::uint32_t>(42*vaporStrength*(1.-.35*span))<<24)|0x00fffaf4u;
            if(puff.age<puff.lifetime) pool_.spawn(puff);
          }
        }
      }
    }
    if (heat_ && quality_ >= EffectsQuality::Medium) {
      for (unsigned engine = 0; engine < definition.flight.engine_count; ++engine) {
        const double spool = aircraft.n1[engine];
        if (spool <= .25) continue;
        Effect heat;
        heat.kind = EffectKind::EngineHeat;
        heat.position = aircraft.pos_ned + aircraft.att.rotate(nozzleExit(engine));
        heat.velocity = aircraft.att.rotate(nozzleRotate(engine,{-6 - spool * 14, (variation-.5)*.6, 0}));
        heat.lifetime = 0.22f + static_cast<float>(spool) * .2f;
        heat.size = .8f;
        const bool reheatEngine=definition.flight.afterburner_thrust_each>0;
        if (reheatEngine) {heat.size=.35f*static_cast<float>(visual.exhaustRadiusScale);heat.lifetime=.18f;}
        const double dryFraction=reheatEngine ? 1.-aircraft.afterburner[engine] : 1.;
        const double density=std::sqrt(std::min(1.,isaAtAltitude(altitude).rho/1.225));
        const auto alpha = static_cast<std::uint32_t>(255 * visual.exhaustOpacity * spool * spool*dryFraction*density);
        if (reheatEngine && alpha==0) continue;
        heat.tint = (alpha << 24) | 0x00d8d2c8u;
        pool_.spawn(heat);
      }
    }
  }
  emitter.previous=aircraft.pos_ned;
}

}  // namespace ofs::client
