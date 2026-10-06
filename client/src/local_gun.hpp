#pragma once
#include "ofs/ballistics.hpp"
#include "ofs/simulator.hpp"
#include <vector>

namespace ofs::client {
// Solo simulation, independent of rendering and available without networking.
class LocalGun {
 public:
  struct Event { bool shot; Vec3 position, velocity; double lifetime; std::uint64_t projectile; };
  explicit LocalGun(AircraftType type) : type_(type) { reset(); }
  void reset() { rounds_.clear(); events_.clear(); time_=readyTime_=0; ammo_=definition().gun ? definition().gun->ammo : 0; }
  unsigned ammo() const { return ammo_; }
  bool ready() const { return ammo_ && time_+1e-9 >= readyTime_; }
  std::size_t activeRounds() const { return rounds_.size(); }
  std::vector<Event> takeEvents() { auto result=std::move(events_); events_.clear(); return result; }
  void step(const State& state, double dt, bool held) {
    if (!(dt>0) || !std::isfinite(dt)) return;
    if (held && ready() && !aircraftCrashed(state) && std::isfinite(state.pos_ned.norm2()) &&
        std::isfinite(state.vel_ned.norm2()) && std::isfinite(state.att.rotate({1,0,0}).norm2())) {
      const auto& gun=*definition().gun;
      const auto position=state.pos_ned+state.att.rotate(gun.muzzle-loadedCg(definition().flight,state));
      const auto velocity=state.vel_ned+state.att.rotate(gunShotDirection(gun,nextId_))*gun.muzzleVelocity;
      rounds_.push_back({position,velocity,0,0,nextId_});
      events_.push_back({true,position,velocity,
          std::min(gun.lifetime,gun.range/std::max(1.,velocity.norm())),nextId_++});
      --ammo_; readyTime_=time_+std::ceil(7200/gun.rpm)/120.;
    }
    time_+=dt;
    if (!definition().gun) return;
    const auto& gun=*definition().gun;
    std::erase_if(rounds_,[&](Round& round) {
      const Vec3 delta=ballisticDisplacement(round.velocity,dt);
      const double distance=delta.norm();
      const double valid=std::clamp(std::min((gun.lifetime-round.age)/dt,
          distance>0 ? (gun.range-round.distance)/distance : 1.),0.,1.);
      const auto end=round.position+delta*valid;
      const double hit=bulletTerrainFraction(round.position,end);
      if (std::isfinite(hit)) {
        events_.push_back({false,round.position+(end-round.position)*hit,{},0,round.id});
        return true;
      }
      round.position+=delta; round.velocity.z+=kG0*dt;
      round.age+=dt; round.distance+=distance;
      return round.age>=gun.lifetime || round.distance>=gun.range;
    });
  }
 private:
  const AircraftDefinition& definition() const { return aircraftDefinition(type_); }
  struct Round { Vec3 position,velocity; double age,distance; std::uint64_t id; };
  AircraftType type_; unsigned ammo_{}; double time_{},readyTime_{};
  std::uint64_t nextId_{1};
  std::vector<Round> rounds_; std::vector<Event> events_;
};
} // namespace ofs::client
