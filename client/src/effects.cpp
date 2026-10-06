#include "effects.hpp"
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
                           bool ownAircraft, std::uint64_t projectile) {
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
  tracer.stretch = 10.0f;
  tracer.size = 0.38f;
  tracer.tint = ownAircraft ? 0xffbfe8ffu : 0xff609affu;
  pool_.spawn(tracer);

  {
    Effect flash;
    flash.kind = EffectKind::MuzzleFlash;
    flash.position = position;
    flash.velocity = velocity.normalized()*12.;
    flash.lifetime = 0.075f;
    flash.size = 0.85f;
    flash.tint = 0xff7ad2ffu;
    pool_.spawn(flash);
  }
}

void CombatEffects::onHit(const Vec3& position, bool ownAircraft, std::uint64_t projectile) {
  pool_.retireProjectile(projectile);
  const std::size_t budget = budgetFor(quality_);
  if (budget == 0) return;

  Effect spark;
  spark.kind = EffectKind::Impact;
  spark.position = position;
  spark.velocity = Vec3{0, 0, 0};
  spark.lifetime = 0.16f;
  spark.size = 0.9f;
  spark.tint = 0xffc0f0ffu;
  pool_.spawn(spark);

  if (budget < 2) return;
  Effect smoke;
  smoke.kind = EffectKind::Smoke;
  smoke.position = position;
  smoke.velocity = Vec3{0, 0, -1.2};
  smoke.lifetime = 0.55f;
  smoke.size = 0.7f;
  smoke.tint = 0x90909090u;
  pool_.spawn(smoke);

  if (budget < 4) return;
  (void)ownAircraft;
  // A radial spray retains a white-hot core and slower orange spark tails.
  const auto seed=static_cast<std::uint32_t>(std::abs(position.x*31+position.y*17));
  for (unsigned i=0;i<12;++i) {
    const float a=hashUnit(seed+i*7919), b=hashUnit(seed+i*3571+3);
    Effect debris=spark; debris.kind=EffectKind::Spark;
    debris.lifetime=.3f+b*.45f; debris.size=.045f+b*.06f;
    debris.velocity={std::cos(a*6.283)* (5+14*b), std::sin(a*6.283)*(5+14*b),-3-9*b};
    debris.billboard=false; debris.stretch=.5f+b*1.1f;
    debris.gravity=float(kG0); debris.drag=1.3f; debris.tint=0xff60baffu;
    pool_.spawn(debris);
  }
}

void CombatEffects::updateMissile(Vec3 position, Quat attitude, double length,
                                  double diameter, bool powered, double dt) {
  if (!powered || quality_ == EffectsQuality::Off || dt <= 0)
    return;
  const auto aft = attitude.rotate({-1, 0, 0});
  Effect flame;
  flame.kind = EffectKind::Fire;
  flame.position = position + aft * (length * .5);
  flame.velocity = aft * 18;
  flame.lifetime = float(std::min(.06, dt));
  flame.size = float(diameter * .6);
  flame.stretch = float(diameter * 8);
  flame.billboard = false;
  flame.tint = 0xff70c0ff;
  pool_.spawn(flame);
  Effect smoke;
  smoke.kind = EffectKind::Smoke;
  smoke.position = flame.position;
  smoke.velocity = aft * 3;
  smoke.size = float(diameter * 1.5);
  smoke.lifetime = 1.5f;
  smoke.tint = 0x908e9399;
  smoke.stretch = 1.5f;
  pool_.spawn(smoke);
}

void CombatEffects::onDestroyed(const Vec3& position, const Vec3& velocity) {
  const std::size_t budget = budgetFor(quality_);
  if (budget == 0) return;

  Effect fireball;
  fireball.kind = EffectKind::Explosion;
  fireball.position = position;
  fireball.velocity = Vec3{0, 0, 0};
  fireball.lifetime = 1.15f;
  fireball.size = 14.0f;
  fireball.tint = 0xff40a0ffu;
  pool_.spawn(fireball);

  if (budget >= 2) for (unsigned i=0;i<16;++i) {
    const float a=hashUnit(i*3571+19), b=hashUnit(i*7919+41);
    Effect flame=fireball; flame.kind=EffectKind::Fire;
    flame.position+=Vec3{(a-.5)*5,(b-.5)*5,-a*3};
    flame.velocity=velocity*.12+Vec3{(a-.5)*22,(b-.5)*22,-4-b*12};
    flame.size=2+a*3; flame.lifetime=.5f+b*.9f; flame.seed=a;
    flame.drag=1.8f; pool_.spawn(flame);
  }
  if (budget < 2) return;
  // A short column of smoke, each puff rising and expanding.
  const int puffs = budget >= 4 ? 24 : 8;
  for (int i = 0; i < puffs; ++i) {
    const float phase = hashUnit(static_cast<std::uint32_t>(i * 7919 + 13));
    Effect smoke;
    smoke.kind = EffectKind::Smoke;
    smoke.position = position + velocity * (0.015 * i) + Vec3{(phase-.5)*7, std::sin(i*2.4)*3,-phase*5};
    smoke.velocity = Vec3{(phase - 0.5) * 3.0, (phase - 0.5) * 3.0, -2.2 - phase * 1.6};
    smoke.lifetime = 5.0f + phase * 3.0f;
    smoke.size = 3.0f + phase * 3.0f;
    smoke.seed = phase;
    smoke.tint = 0xa04e5358u;
    smoke.drag = 0.12f;
    pool_.spawn(smoke);
  }
  if (budget < 4) return;
  // Debris streaks. Direction is arbitrary but bounded; a real breakup model is
  // explicitly out of scope for this milestone.
  for (int i = 0; i < 24; ++i) {
    const float a = hashUnit(static_cast<std::uint32_t>(i * 2654435761u));
    const float b = hashUnit(static_cast<std::uint32_t>(i * 40503u + 7));
    const double theta = a * 6.2831853;
    const double elevation = (b - 0.5) * 1.2;
    const double speed = 25.0 + a * 45.0;
    Effect debris;
    debris.kind = EffectKind::Debris;
    debris.position = position;
    debris.velocity = Vec3{std::cos(theta) * speed * std::cos(elevation),
                           std::sin(theta) * speed * std::cos(elevation), -std::abs(std::sin(elevation)) * speed-8};
    debris.velocity = debris.velocity + velocity * 0.35;
    debris.lifetime = 1.6f + b * 0.8f;
    debris.size = 0.5f + a * 0.7f;
    debris.stretch = 3.0f;
    debris.billboard = false;
    debris.tint = 0xff404040u;
    debris.gravity = 9.81f;
    debris.drag = 0.3f;
    pool_.spawn(debris);
  }
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
    if (health<65 || airframeIntegrity(aircraft)<.65) {
      Effect smoke;
      smoke.kind=EffectKind::Smoke;
      smoke.position=aircraft.pos_ned+aircraft.att.rotate(visual.exhaust[0]-cg);
      smoke.velocity={variation-.5,0,-1.2}; smoke.lifetime=3+variation;
      smoke.size=1+variation; smoke.tint=0x90505560u; smoke.drag=.1f;
      pool_.spawn(smoke);
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
