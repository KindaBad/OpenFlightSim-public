#pragma once
// Parts that break off a damaged aircraft.
//
// When a wing or fin loses more of itself, the piece that went is not simply
// removed: it leaves as an object of its own, carrying on at the aircraft's
// speed, tumbling, slowing in the air and falling. This is presentation only.
// The simulation already flies the aircraft without the piece.

#include "animation.hpp"
#include "damage_visuals.hpp"
#include "ofs/terrain.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace ofs::client {

struct BreakawayPiece {
  AircraftType type{AircraftType::A320};
  // A wing, the tail, or DamagePart::Fuselage for the whole wreck of an
  // aircraft that blew up, less whatever wings and fin left it.
  DamagePart part{DamagePart::LeftWing};
  // The slab that left, as fractions of the wing's span or the fin's height.
  float inner{}, outer{};
  float seed{};
  // The aircraft frame the piece is drawn in, frozen at the moment it left.
  Vec3 cg{};
  std::vector<AssetMatrix> deltas;
  // The piece itself: it turns about `pivot`, a point in its own body axes.
  Vec3 pivot{}, position{}, velocity{}, spin{};
  Quat attitude{};
  double age{}, smoke{};
  bool grounded{};
  bool struck{};  // set on the update in which it reached the ground
  bool wreck() const { return part == DamagePart::Fuselage; }
};

class Breakaways {
 public:
  static constexpr std::size_t kCapacity = 24;
  static constexpr double kLifetime = 12, kWreckLifetime = 40;

  // Compares an aircraft's damage with what it was when last seen, and
  // releases whatever has gone since. An aircraft seen for the first time is
  // taken as it is. Returns the pieces released by this call.
  std::size_t observe(std::uint64_t entity, AircraftType type, const State& state, const DamageView& damage,
                      const std::vector<AssetMatrix>& deltas, float seed) {
    auto [known, added] = seen_.try_emplace(entity);
    Seen& seen = known->second;
    seen.current = true;
    std::size_t released = 0;
    if (!added && seen.type == type)
      for (const DamagePart part : kParts)
        released += release(type, part, remaining(part, damage[part]), remaining(part, seen.damage[part]), state, deltas,
                            seed, 1);
    seen.type = type;
    seen.damage = damage;
    return released;
  }

  // An aircraft blown apart: whatever wings and fin it still had are thrown
  // clear of the fireball. Returns the pieces released.
  std::size_t shatter(std::uint64_t entity, AircraftType type, const State& state,
                      const std::vector<AssetMatrix>& deltas, float seed) {
    const auto known = seen_.find(entity);
    const DamageView damage = known != seen_.end() && known->second.type == type ? known->second.damage
                                                                                 : damageView(state, 100);
    std::size_t released = 0;
    for (const DamagePart part : kParts)
      released += release(type, part, remaining(part, 1), remaining(part, damage[part]), state, deltas, seed, 4);
    // What is left falls as one burning wreck, slowly rolling.
    BreakawayPiece wreck;
    wreck.type = type;
    wreck.part = DamagePart::Fuselage;
    wreck.seed = seed;
    wreck.cg = wreck.pivot = loadedCg(aircraftDefinition(type).flight, state);
    wreck.deltas = deltas;
    wreck.attitude = state.att;
    wreck.position = state.pos_ned;
    wreck.velocity = state.vel_ned;
    const double a = hash(seed, released_), b = hash(seed + 1.9f, released_);
    wreck.spin = state.omega_body + Vec3{(a - .5) * 5, .5 + b, (b - .5) * 1.5};
    if (pieces_.size() >= kCapacity) pieces_.erase(pieces_.begin());
    pieces_.push_back(std::move(wreck));
    ++released_;
    // It is gone now; a respawn under the same identity starts whole.
    seen_.erase(entity);
    return released + 1;
  }

  // Forgets aircraft that were not observed since the last call.
  void endFrame() {
    std::erase_if(seen_, [](const auto& entry) { return !entry.second.current; });
    for (auto& [entity, seen] : seen_) {
      (void)entity;
      seen.current = false;
    }
  }

  void update(double dt, const Vec3& wind = {}) {
    if (!(dt > 0)) return;
    for (BreakawayPiece& piece : pieces_) {
      piece.age += dt;
      piece.smoke += dt;
      piece.struck = false;
      if (piece.grounded) continue;
      // A flat plate tumbling broadside sheds its speed within a few seconds;
      // a whole fuselage carries on much further.
      const Vec3 relative = piece.velocity - wind;
      piece.velocity = wind + relative * std::exp(-(piece.wreck() ? .12 : .55) * dt);
      piece.velocity.z += kG0 * dt;
      piece.position += piece.velocity * dt;
      const double rate = piece.spin.norm();
      if (rate > 1e-9) {
        const double half = rate * dt * .5;
        const Vec3 axis = piece.spin / rate;
        piece.attitude = (piece.attitude * Quat{std::cos(half), axis.x * std::sin(half), axis.y * std::sin(half),
                                                axis.z * std::sin(half)}).normalized();
      }
      if (piece.position.z >= groundHeightNed(piece.position.x, piece.position.y)) {
        piece.position.z = groundHeightNed(piece.position.x, piece.position.y);
        piece.velocity = {};
        piece.grounded = piece.struck = true;
        // A wreck does not lie about for long: it burns out where it fell.
        if (piece.wreck()) piece.age = std::max(piece.age, kWreckLifetime - 1.5);
      }
    }
    std::erase_if(pieces_, [](const BreakawayPiece& piece) {
      return piece.age >= (piece.wreck() ? kWreckLifetime : kLifetime);
    });
  }

  void clear() {
    pieces_.clear();
    seen_.clear();
  }
  const std::vector<BreakawayPiece>& pieces() const { return pieces_; }
  std::vector<BreakawayPiece>& pieces() { return pieces_; }

 private:
  static constexpr DamagePart kParts[3]{DamagePart::LeftWing, DamagePart::RightWing, DamagePart::Tail};
  static double remaining(DamagePart part, double damage) {
    return part == DamagePart::Tail ? finRemaining(damage) : wingRemaining(damage);
  }
  // Releases the slab of `part` between `inner` and `outer`. `violence` scales
  // how hard it is thrown: 1 for a part that failed, more for an explosion.
  std::size_t release(AircraftType type, DamagePart part, double inner, double outer, const State& state,
                      const std::vector<AssetMatrix>& deltas, float seed, double violence) {
    // Slivers are not worth a draw; they go in the burst of fragments.
    if (std::min(outer, 1.) - inner < .06) return 0;
    const bool fin = part == DamagePart::Tail;
    BreakawayPiece piece;
    piece.type = type;
    piece.part = part;
    piece.inner = float(inner);
    piece.outer = float(outer);
    piece.seed = seed;
    piece.cg = loadedCg(aircraftDefinition(type).flight, state);
    piece.deltas = deltas;
    const double middle = (inner + std::min(outer, 1.)) * .5;
    const auto geometry = damageGeometry(type);
    piece.pivot = fin ? Vec3{geometry.finFore - (geometry.finTop - geometry.finBase) * .6, 0,
                             -(geometry.finBase + (geometry.finTop - geometry.finBase) * middle)}
                      : wingPoint(type, part == DamagePart::RightWing, middle);
    const Vec3 arm = piece.pivot - piece.cg;
    piece.attitude = state.att;
    piece.position = state.pos_ned + state.att.rotate(arm);
    // It leaves outward and upward, and a little behind, at a few metres a second.
    const double outward = fin ? 0 : part == DamagePart::RightWing ? 1 : -1;
    const double a = hash(seed, released_), b = hash(seed + 3.7f, released_);
    piece.velocity = state.vel_ned + state.att.rotate(state.omega_body.cross(arm) +
                                                      Vec3{-3 - 4 * a, outward * (3 + 3 * b), -2 - 4 * b} * violence);
    piece.spin = Vec3{outward * (2.5 + 4 * a) + (fin ? 3 * (a - .5) : 0), (b - .5) * 5, (a - .5) * 4 + (fin ? 3 : 0)} *
                 std::sqrt(violence);
    if (pieces_.size() >= kCapacity) pieces_.erase(pieces_.begin());
    pieces_.push_back(std::move(piece));
    ++released_;
    return 1;
  }
  static double hash(float seed, std::size_t index) {
    const double value = std::sin(double(seed) * 12.9898 + double(index) * 78.233) * 43758.5453;
    return value - std::floor(value);
  }
  struct Seen {
    AircraftType type{AircraftType::A320};
    DamageView damage;
    bool current{};
  };
  std::map<std::uint64_t, Seen> seen_;
  std::vector<BreakawayPiece> pieces_;
  std::size_t released_{};
};

}  // namespace ofs::client
