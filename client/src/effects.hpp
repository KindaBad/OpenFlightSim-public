#pragma once
// Bounded, purely visual combat and atmospheric effects.
//
// Everything here is presentation only. It is driven by server-authoritative
// combat events supplied by the caller and by the simulated aircraft state, and
// it never feeds anything back into the simulation. All pools are fixed-size
// and self-cleaning. A single bounded transient particle vertex stream is built
// each frame, without allocating individual particle objects.

#include "ofs/aircraft.hpp"  // Vec3, State, Quat
#include "ofs/aircraft_definition.hpp"
#include "ofs/damage.hpp"
#include "ofs/simulator.hpp"
#include "settings.hpp"      // EffectsQuality

#include <array>
#include <cstdint>
#include <vector>
#include <map>

namespace ofs::client {

// The effect kinds the renderer knows how to draw. Kept small on purpose.
enum class EffectKind : std::uint8_t {
  Tracer,        // gun tracer round, stretched along its velocity
  MuzzleFlash,   // short-lived flash at the gun muzzle
  Impact,        // hit spark at a server-confirmed impact point
  Explosion,     // destruction fireball
  Smoke,         // destruction smoke puff
  Debris,        // destruction debris streak
  Contrail,      // condensation trail segment
  Vapor,         // swept wing condensation sheet or vortex wisp
  EngineHeat,    // exhaust shimmer marker
  Light,
  Dust,
  Fire,
  Spark,
  Trail,         // one length of a smoke trail, along `axis`
  Flash,         // a burst of light with rays: gun muzzle, warhead, impact
  Shockwave,     // the expanding ring of a detonation
  Count
};

struct Effect {
  EffectKind kind{EffectKind::Tracer};
  Vec3 position{};
  Vec3 velocity{};      // world m/s; the renderer derives the trail direction
  float size{1};        // radius, metres
  float age{0};         // seconds since spawn
  float lifetime{1};    // seconds
  float seed{0};        // per-particle variation, keeps the pool deterministic
  std::uint32_t tint{0xffffffff}; // ABGR packed: low byte is red.
  // Contrail/vapor trail length in metres, 0 for a point sprite.
  float stretch{0};
  Vec3 axis{1,0,0};     // world orientation for stretched vapor, independent of wind
  Vec3 normal{0,0,-1}; // wing-plane normal for non-billboard condensation sheets
  // Camera-facing billboard flag; streaks use their velocity instead.
  bool billboard{true};
  float drag{}, gravity{};
  std::uint64_t projectile{};
  // Burning wreckage leaves smoke behind it: seconds between puffs, 0 for none.
  float smokeInterval{}, smokeClock{};
};

// Fixed-capacity pool. Exceeding capacity replaces effects round-robin rather than
// growing, so a burst of server events can never exhaust memory.
class EffectPool {
 public:
  explicit EffectPool(std::size_t capacity = 2048);

  // Replaces an effect when full; rejects zero capacity or invalid particles.
  bool spawn(Effect effect);
  // Advances all effects and retires expired ones. `dt` may be zero.
  void update(double dt);
  void clear() { effects_.clear(); }
  void retireProjectile(std::uint64_t id);

  const std::vector<Effect>& effects() const { return effects_; }
  std::size_t size() const { return effects_.size(); }
  std::size_t capacity() const { return capacity_; }
  std::size_t peakSize() const { return peak_; }
  std::uint64_t dropped() const { return dropped_; }
  // Number of effects by kind, for the developer overlay.
  std::size_t countOf(EffectKind kind) const;

 private:
  std::vector<Effect> effects_;
  std::size_t capacity_;
  std::size_t peak_{};
  std::uint64_t dropped_{};
  std::size_t cursor_{};
};

// Translates one server combat event into visual effects.
//
// This is the only place combat events become visuals, which keeps the "visuals
// derive from authoritative events" rule checkable in one place.
class CombatEffects {
 public:
  CombatEffects(EffectPool& pool, EffectsQuality quality);

  CombatEffects(const CombatEffects&) = delete;
  CombatEffects& operator=(const CombatEffects&) = delete;
  // Only the quality is mutable at runtime, so rebinding is explicit and does
  // not need a deleted assignment operator.
  void setQuality(EffectsQuality quality) { quality_ = quality; }
  void setEmissions(bool contrails, bool heat) { contrails_ = contrails; heat_ = heat; }

  // A gun shot: a tracer, and at the muzzle a flash, a puff of gun gas and a
  // spent case. `carrier` is the firing aircraft's velocity, which the muzzle
  // effects ride along with; left at zero it is estimated from the round.
  void onShot(const Vec3& position, const Vec3& velocity, double lifetime,
              bool ownAircraft, std::uint64_t projectile = 0, const Vec3& carrier = {});
  // A server-confirmed hit: flash, sparks, fragments and a puff of smoke,
  // carried along by the struck aircraft at `targetVelocity`.
  void onHit(const Vec3& position, bool ownAircraft, std::uint64_t projectile = 0,
             const Vec3& targetVelocity = {});
  // An aircraft destroyed: fireball, shock ring, smoke column and burning debris.
  void onDestroyed(const Vec3& position, const Vec3& velocity);
  // A warhead going off: smaller and sharper than an aircraft blowing up.
  void onDetonation(const Vec3& position);
  // A wing or fin breaking away: fragments and a puff where it parted.
  void onPartLost(const Vec3& position, const Vec3& velocity);
  // Smoke left by a piece that is falling away; a burning wreck adds flame.
  void onPieceSmoke(const Vec3& position, const Vec3& velocity, bool burning);
  void onGroundImpact(const Simulator::GroundImpact& impact);
  // A missile in flight: motor flame and glow, a smoke trail laid along its
  // path, and a burst of exhaust the moment the motor lights.
  void updateMissile(std::uint64_t id, Vec3 position, Vec3 velocity, Quat attitude, double length,
                     double diameter, double age, bool powered, double dt);
  // Forgets missiles that were not updated this frame.
  void retireMissiles(std::size_t active);
  void setCondensation(bool enabled, double humidity) { vapor_ = enabled; humidity_ = humidity; }
  // Exhaust contrails and wing condensation, spawned from simulated airflow.
  void updateAircraft(const State& aircraft, double dt, AircraftType type,
                      std::uint64_t id, double load = 1, double health = 100, const Weather& weather = {});

 private:
  EffectPool& pool_;
  EffectsQuality quality_;
  bool contrails_{true}, heat_{true}, vapor_{true};
  double humidity_{.75};
  double scrapeClock_{};
  std::uint32_t shots_{};
  // Lays one length of a smoke trail from where its source was to where it is.
  void laySmoke(Vec3& from, const Vec3& to, const Vec3& drift, float size, float lifetime, std::uint32_t tint);
  struct Emitter {
    double time{}, groundTime{};
    std::uint32_t sequence{};
    Vec3 previous{};
    bool primed{};
    double integrity{1};
    // Battle damage: where each part's smoke was last laid, and what the
    // damage was, so an engine being shot out is seen as an event.
    std::array<Vec3, damagePartCount> smokeFrom{};
    std::array<bool, damagePartCount> smoking{};
    std::array<float, damagePartCount> damage{};
    double smokeClock{};
    bool damageKnown{};  // false until an aircraft has been seen once
  };
  std::map<std::uint64_t, Emitter> emitters_; // at most 64 aircraft
  struct MissileEmitter { Vec3 nozzle{}; double sinceSegment{}; bool primed{}, lit{}, seen{}; };
  std::map<std::uint64_t, MissileEmitter> missiles_; // at most the server's pool of 128
};

}  // namespace ofs::client
