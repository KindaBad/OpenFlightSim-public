#include "ofs/weapons.hpp"
#include "ofs/terrain.hpp"
#include "ofs/world.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace ofs::weapons {
namespace {
Vec3 limited(Vec3 v, double max) {
  const double n = v.norm();
  return n > max ? v * (max / n) : v;
}
double angle(Vec3 a, Vec3 b) {
  return std::acos(clamp(a.normalized().dot(b.normalized()), -1, 1));
}
double approach(double a, double b, double step) {
  return a + clamp(b - a, -step, step);
}
} // namespace
const MissileDefinition &missileDefinition(WeaponType type) {
  static const MissileDefinition ir = [] {
    MissileDefinition d{};
    d.type = WeaponType::Infrared;
    d.name = "Dev IR-90";
    d.length = 3;
    d.diameter = .13;
    d.launchMass = 90;
    // Boost-sustain grain at about 235 s of specific impulse, typical of a
    // reduced-smoke solid motor.
    d.motor = {24, 15000, 2.2, 4500, 5};
    d.aero.area = kPi * .13 * .13 / 4;
    d.seeker = {24 * kDeg2Rad, 60 * kDeg2Rad, 120 * kDeg2Rad, 8000, .12, .5, 0};
    return d;
  }();
  static const MissileDefinition ar = [] {
    MissileDefinition d{};
    d.type = WeaponType::ActiveRadar;
    d.model = MissileModel::DevRadar;
    d.name = "Dev AR-157";
    d.length = 3.66;
    d.diameter = .178;
    d.launchMass = 157;
    d.motor = {55, 22000, 3.5, 6500, 7.7, .3};
    d.aero.area = kPi * .178 * .178 / 4;
    d.aero.cd0 = .4;
    d.seeker = {16 * kDeg2Rad, 45 * kDeg2Rad, 80 * kDeg2Rad, 18000, .15, .7,
                12000};
    d.maxG = 30;
    d.fuseRadius = 9;
    d.damageRadius = 25;
    d.damage = 230;
    d.lifetime = 75;
    d.minimumRange = 600;
    return d;
  }();
  if (type == WeaponType::Infrared)
    return ir;
  if (type == WeaponType::ActiveRadar)
    return ar;
  throw std::invalid_argument("invalid missile type");
}
const MissileDefinition &missileDefinition(MissileModel model) {
  // A real missile: its published length, diameter and launch mass, and a
  // motor given as propellant, boost thrust and time, sustain thrust and time.
  const auto make = [](WeaponType type, MissileModel model, const char *name, double length, double diameter,
                       double mass, MotorDefinition motor) {
    MissileDefinition d{};
    d.type = type;
    d.model = model;
    d.name = name;
    d.length = length;
    d.diameter = diameter;
    d.launchMass = mass;
    d.motor = motor;
    d.aero.area = kPi * diameter * diameter / 4;
    if (type == WeaponType::ActiveRadar) {
      d.aero.cd0 = .4;
      d.seeker = {16 * kDeg2Rad, 45 * kDeg2Rad, 80 * kDeg2Rad, 18000, .15, .7, 12000};
      d.maxG = 30;
      d.fuseRadius = 9;
      d.damageRadius = 25;
      d.damage = 230;
      d.lifetime = 75;
      d.minimumRange = 600;
      d.loft = .25;
    } else {
      d.seeker = {24 * kDeg2Rad, 60 * kDeg2Rad, 120 * kDeg2Rad, 8000, .12, .5, 0};
    }
    return d;
  };
  static const std::array<MissileDefinition, std::size_t(MissileModel::Count)> models = [&] {
    std::array<MissileDefinition, std::size_t(MissileModel::Count)> m{};
    m[std::size_t(MissileModel::DevInfrared)] = missileDefinition(WeaponType::Infrared);
    m[std::size_t(MissileModel::DevRadar)] = missileDefinition(WeaponType::ActiveRadar);
    {
      // IRIS-T: imaging seeker that looks 90 degrees off the nose, thrust
      // vectoring, 60 g. An 11.4 kg warhead. About 25 km.
      auto &d = m[std::size_t(MissileModel::IrisT)] =
          make(WeaponType::Infrared, MissileModel::IrisT, "IRIS-T", 2.936, .127, 87.4, {26, 17500, 2.4, 4000, 4.75});
      d.seeker.gimbal = 90 * kDeg2Rad; d.seeker.rate = 180 * kDeg2Rad; d.seeker.signalRange = 9500;
      d.seeker.memoryTime = .8;
      d.maxG = 60; d.maxAlpha = 40 * kDeg2Rad; d.actuatorRate = 260 * kDeg2Rad; d.navigation = 4;
      d.thrustVector = 1; d.anticipation = 1;
      d.damage = 185; d.minimumRange = 250;
    }
    {
      // Meteor: a boost motor, then a ducted ramjet throttled to hold its
      // speed, which burns for most of the flight, so it still has thrust
      // where a rocket is coasting. The air it breathes is not carried, which
      // is why so little propellant gives so much impulse. Over 100 km.
      auto &d = m[std::size_t(MissileModel::Meteor)] =
          make(WeaponType::ActiveRadar, MissileModel::Meteor, "Meteor", 3.65, .178, 190, {50, 30000, 3.0, 8000, 24, .3, 3.6});
      d.aero.cd0 = .46;   // the two intakes
      d.seeker.gimbal = 50 * kDeg2Rad; d.seeker.signalRange = 19000; d.seeker.activationRange = 13000;
      d.maxG = 36; d.damage = 235; d.anticipation = 1;
    }
    {
      // R-74M2 (izdeliye 760): the R-73 family's seeker to 75 degrees off the
      // nose and gas-vane thrust vectoring, in a body made to fit the Su-57's
      // side bays. A 7.4 kg warhead. About 40 km.
      auto &d = m[std::size_t(MissileModel::R74M2)] =
          make(WeaponType::Infrared, MissileModel::R74M2, "R-74M2", 2.9, .17, 106, {34, 21000, 2.6, 5500, 6.0, .25});
      d.seeker.gimbal = 75 * kDeg2Rad; d.seeker.rate = 150 * kDeg2Rad; d.seeker.signalRange = 10500;
      d.maxG = 50; d.maxAlpha = 35 * kDeg2Rad; d.actuatorRate = 220 * kDeg2Rad;
      d.thrustVector = .8; d.anticipation = 1;
      d.damage = 165; d.minimumRange = 300;
    }
    {
      // R-77M (izdeliye 180): a two-pulse motor, given here as a boost and a
      // long second burn, and a seeker that wakes further out. A 22.5 kg
      // warhead. About 190 km.
      auto &d = m[std::size_t(MissileModel::R77M)] =
          make(WeaponType::ActiveRadar, MissileModel::R77M, "R-77M", 3.71, .2, 190, {77, 24000, 3.2, 9500, 11, .3});
      d.seeker.gimbal = 55 * kDeg2Rad; d.seeker.signalRange = 21000; d.seeker.activationRange = 14000;
      d.maxG = 35; d.damage = 235; d.anticipation = 1;
    }
    {
      // PL-5EII: a Sidewinder-pattern missile with canards and rollerons, 40 g,
      // a seeker that looks 40 degrees off the nose. A 6 kg warhead. 16 km.
      auto &d = m[std::size_t(MissileModel::PL5EII)] =
          make(WeaponType::Infrared, MissileModel::PL5EII, "PL-5EII", 2.893, .127, 83, {19, 14000, 2.0, 3600, 4.6});
      d.seeker.fov = 20 * kDeg2Rad; d.seeker.gimbal = 40 * kDeg2Rad; d.seeker.rate = 90 * kDeg2Rad;
      d.seeker.signalRange = 7000;
      d.maxG = 40; d.damage = 150; d.minimumRange = 400;
    }
    {
      // SD-10A (the export PL-12): 38 g, a 24 kg warhead. 70 km.
      auto &d = m[std::size_t(MissileModel::SD10A)] =
          make(WeaponType::ActiveRadar, MissileModel::SD10A, "SD-10A", 3.934, .203, 199, {60, 24000, 3.5, 7000, 8, .3});
      d.maxG = 38; d.damage = 240; d.anticipation = 1;
    }
    return m;
  }();
  if (model >= MissileModel::Count)
    throw std::invalid_argument("invalid missile model");
  return models[std::size_t(model)];
}
MissileModel missileModel(WeaponType type, AircraftType aircraft) {
  const bool infrared = type == WeaponType::Infrared;
  if (!infrared && type != WeaponType::ActiveRadar)
    throw std::invalid_argument("invalid missile type");
  switch (aircraft) {
  case AircraftType::Typhoon: return infrared ? MissileModel::IrisT : MissileModel::Meteor;
  case AircraftType::Su57: return infrared ? MissileModel::R74M2 : MissileModel::R77M;
  case AircraftType::JF17: return infrared ? MissileModel::PL5EII : MissileModel::SD10A;
  default: return infrared ? MissileModel::DevInfrared : MissileModel::DevRadar;
  }
}
const BombDefinition &bombDefinition(WeaponType type) {
  // Mk 82 and Mk 84 general-purpose bombs and a B83-class weapon, which is
  // given enough drag that the aircraft that dropped it can get clear.
  static const BombDefinition mk82{WeaponType::Bomb500, "Mk 82", 227, 2.21, .273, .0075, 70, 160, 50, 75, .10};
  static const BombDefinition mk84{WeaponType::Bomb2000, "Mk 84", 925, 3.28, .457, .021, 150, 320, 120, 240, .30};
  static const BombDefinition nuclear{WeaponType::Nuclear, "B83", 1100, 3.67, .46, .60, 8000, 1000, 6000, 100000, 1};
  if (type == WeaponType::Bomb500)
    return mk82;
  if (type == WeaponType::Bomb2000)
    return mk84;
  if (type == WeaponType::Nuclear)
    return nuclear;
  throw std::invalid_argument("invalid bomb type");
}
unsigned bombLoadouts(AircraftType type) {
  return validAircraftType(type) && aircraftDefinition(type).bomber ? 3 : 0;
}
BombLoad bombLoad(AircraftType type, unsigned loadout) {
  if (!bombLoadouts(type))
    return {};
  // B-52H: 27 Mk 82 in the bay and 24 under the wings, or 18 Mk 84, or one
  // weapon in the bay.
  switch (loadout % bombLoadouts(type)) {
  case 1: return {WeaponType::Bomb2000, 18, "18 x Mk 84 (2,000 lb)"};
  case 2: return {WeaponType::Nuclear, 1, "1 x B83 nuclear"};
  default: return {WeaponType::Bomb500, 51, "51 x Mk 82 (500 lb)"};
  }
}
void advanceBomb(const BombDefinition &d, Vec3 &position, Vec3 &velocity, const Weather &weather, double dt) {
  const auto air = isaAtAltitude(std::max(0., world().altitude(position)), weather.temp_offset_c);
  const Vec3 relative = velocity - world().fromLocal(position, weather.wind_ned);
  const Vec3 acceleration = world().down(position) * kG0 - relative * (.5 * air.rho * relative.norm() * d.dragArea / d.mass);
  position += velocity * dt + acceleration * (.5 * dt * dt);
  velocity += acceleration * dt;
}
bool bombImpact(const BombDefinition &d, Vec3 position, Vec3 velocity, const Weather &weather, Vec3 &impact, double &seconds) {
  constexpr double step = .1;
  for (seconds = 0; seconds < d.lifetime; seconds += step) {
    const Vec3 before = position;
    advanceBomb(d, position, velocity, weather, step);
    const double clearance = world().clearance(position);
    if (clearance <= 0) {
      const double above = world().clearance(before), below = -clearance;
      impact = before + (position - before) * (above / std::max(1e-6, above + below));
      return true;
    }
  }
  impact = position;
  return false;
}
void Inventory::reset(AircraftType type, unsigned loadout) {
  aircraft = type;
  stations.clear();
  selected = WeaponType::Infrared;
  const auto load = bombLoad(type, loadout);
  bombType = load.type;
  bombs = load.count;
  bay = {.5, 0, 1.6};
  if (isBomb(bombType))
    selected = bombType;
  // Each aircraft's usual air-to-air load, placed against its visual model.
  // The heat seekers come first; see docs/AIRCRAFT_REFERENCE.md.
  // Typhoon: two IRIS-T on the outer wing pylons and four Meteor half sunk
  // into the fuselage, the forward pair under the intake and outboard.
  if (type == AircraftType::Typhoon)
    stations = {{{-2.8, -4.2, .52}, 6, WeaponType::Infrared, true},
                {{-2.8, 4.2, .52}, 6, WeaponType::Infrared, true},
                {{1.3, -1.0, .60}, 6, WeaponType::ActiveRadar},
                {{1.3, 1.0, .60}, 6, WeaponType::ActiveRadar},
                {{-2.6, -.8, .64}, 6, WeaponType::ActiveRadar},
                {{-2.6, .8, .64}, 6, WeaponType::ActiveRadar}};
  // Su-57: everything is carried inside. Two R-74M2 in the wing-root bays and
  // four R-77M in the tandem bays between the engines, so nothing shows.
  if (type == AircraftType::Su57)
    stations = {{{-.3, -2.9, .10}, 6, WeaponType::Infrared},
                {{-.3, 2.9, .10}, 6, WeaponType::Infrared},
                {{1.2, -.42, .30}, 6, WeaponType::ActiveRadar},
                {{1.2, .42, .30}, 6, WeaponType::ActiveRadar},
                {{-3.4, -.42, .30}, 6, WeaponType::ActiveRadar},
                {{-3.4, .42, .30}, 6, WeaponType::ActiveRadar}};
  // Dev aircraft: two heat seekers and two radar missiles under the chines.
  if (type == AircraftType::Dev)
    stations = {{{-3, -3, .9}, 6, WeaponType::Infrared},
                {{-3, 3, .9}, 6, WeaponType::Infrared},
                {{2, -1.6, 1.1}, 6, WeaponType::ActiveRadar},
                {{2, 1.6, 1.1}, 6, WeaponType::ActiveRadar}};
  // JF-17: two PL-5EII on the wingtip rails and four SD-10A under the model's
  // own outer and inner wing pylons.
  if (type == AircraftType::JF17)
    stations = {{{-1.1, -4.786, .43}, 6, WeaponType::Infrared, true},
                {{-1.1, 4.786, .43}, 6, WeaponType::Infrared, true},
                {{-1.1, -2.94, .80}, 6, WeaponType::ActiveRadar, true},
                {{-1.1, 2.94, .80}, 6, WeaponType::ActiveRadar, true},
                {{-.2, -2.06, .80}, 6, WeaponType::ActiveRadar, true},
                {{-.2, 2.06, .80}, 6, WeaponType::ActiveRadar, true}};
}
unsigned Inventory::remaining(WeaponType type) const {
  return std::count_if(stations.begin(), stations.end(),
                       [&](const auto &s) { return s.mounted == type; });
}
int Inventory::nextStation() const {
  for (unsigned i = 0; i < stations.size(); ++i)
    if (stations[i].mounted == selected)
      return int(i);
  return -1;
}
bool Inventory::consume(unsigned i, WeaponType type) {
  if (i >= stations.size() || type == WeaponType::None ||
      stations[i].mounted != type ||
      !(stations[i].compatible & (1 << unsigned(type))))
    return false;
  stations[i].mounted = WeaponType::None;
  return true;
}
void Inventory::applyPayload(const AircraftConfig &cfg, State &state) const {
  double total = cfg.initial_payload;
  Vec3 moment = cfg.payload_position * total;
  Vec3 diagonal{}, products{};
  const auto point = [&](double mass, Vec3 p) {
    diagonal += Vec3{p.y * p.y + p.z * p.z, p.x * p.x + p.z * p.z,
                     p.x * p.x + p.y * p.y} *
                mass;
    products -= Vec3{p.x * p.y, p.x * p.z, p.y * p.z} * mass;
  };
  point(total, cfg.payload_position);
  for (const auto &station : stations)
    if (station.mounted != WeaponType::None) {
      const auto mass = missileDefinition(station.mounted, aircraft).launchMass;
      total += mass;
      moment += station.position * mass;
      point(mass, station.position);
    }
  if (bombs && isBomb(bombType)) {
    // Spread along the bay, about its middle.
    const double mass = bombs * bombDefinition(bombType).mass;
    total += mass;
    moment += bay * mass;
    point(mass, bay);
    diagonal += Vec3{0, 6, 6} * mass;
  }
  const Vec3 centroid = total > 0 ? moment / total : cfg.payload_position;
  point(-total, centroid);
  state.payload_mass = total;
  state.payload_offset = centroid - cfg.payload_position;
  state.payload_inertia_correction =
      diagonal - cfg.payload_inertia_per_kg * (total - cfg.initial_payload);
  state.payload_products_correction = products;
}
double motorThrust(const MotorDefinition &d, double age) {
  age -= d.ignitionDelay;
  if (age < 0)
    return 0;
  if (age < d.boostTime)
    return d.boostThrust;
  if (age < d.boostTime + d.sustainTime)
    return d.sustainThrust;
  return 0;
}
double dragCoefficient(const AeroDefinition &d, double mach, double cn) {
  const double rise = d.transonic * std::exp(-std::pow((mach - 1.05) / .22, 2));
  const double wave = mach > 1 ? d.wave * (1 - std::exp(-(mach - 1))) : 0;
  return d.cd0 + rise + wave + d.induced * cn * cn;
}
Vec3 proportionalNavigation(Vec3 r, Vec3 rv, Vec3 velocity, double n,
                            double limit) {
  const double r2 = r.norm2();
  if (r2 < 1e-6 || velocity.norm2() < 1e-6)
    return {};
  const double closing = std::max(0., -r.dot(rv) / std::sqrt(r2));
  const Vec3 losRate = r.cross(rv) / r2;
  // Pure PN: acceleration normal to missile velocity, never attitude
  // assignment.
  return limited(losRate.cross(velocity.normalized()) * (n * closing), limit);
}
bool lineOfSight(Vec3 a, Vec3 b) {
  // Earth-curvature horizon plus deterministic terrain screening at <=500 m.
  if (world().clearance(a) <= 0 || world().clearance(b) <= 0)
    return false;
  const double distance = (b - a).norm();
  const double horizon = std::sqrt(2 * 6371000 * std::max(0., world().altitude(a))) +
                         std::sqrt(2 * 6371000 * std::max(0., world().altitude(b)));
  if (distance > horizon)
    return false;
  const unsigned steps =
      std::min(256u, std::max(1u, unsigned(distance / 500) + 1));
  for (unsigned i = 1; i < steps; ++i) {
    const auto p = a + (b - a) * (double(i) / steps);
    if (world().clearance(p) <= 0)
      return false;
  }
  return true;
}
double targetRcs(const SensorTarget &t, Vec3 observer) {
  const Vec3 aspect =
      t.attitude.inverseRotate((observer - t.position).normalized());
  double front = 3, side = 12, rear = 5;
  if (t.type == AircraftType::Su57) {
    front = .15;
    side = 3;
    rear = 1;
  }
  if (t.type == AircraftType::A320) {
    front = 20;
    side = 60;
    rear = 25;
  }
  if (t.type == AircraftType::SR71 || t.type == AircraftType::Dev) {
    front = 2;
    side = 8;
    rear = 4;
  }
  if (t.type == AircraftType::JF17) { // A small conventional airframe.
    front = 2;
    side = 8;
    rear = 3.5;
  }
  const double axial = aspect.x * aspect.x;
  return (aspect.x >= 0 ? front : rear) * axial + side * (1 - axial);
}
double infraredSignal(const SensorTarget &t, Vec3 observer) {
  const auto aspect =
      t.attitude.inverseRotate((observer - t.position).normalized());
  const double hot = std::max(0., -aspect.x);
  return (.12 + .88 * hot) * (.25 + .75 * clamp(t.power, 0, 1)) *
         (1 + 3 * clamp(t.afterburner, 0, 1));
}
MissileState launchState(const MissileDefinition &d, const State &aircraft,
                         Vec3 station, Measurement target) {
  MissileState s;
  s.position = aircraft.pos_ned + aircraft.att.rotate(station);
  // A rail launch leaves along the rail; an ejected store is pushed clear and
  // lights its motor below the aircraft.
  s.velocity = aircraft.vel_ned +
               aircraft.att.rotate({0, 0, d.motor.ignitionDelay > 0 ? 6. : 1.});
  s.attitude = aircraft.att;
  s.omega = aircraft.omega_body;
  s.propellant = d.motor.propellant;
  s.mass = d.launchMass;
  s.inertia = {.5 * s.mass * std::pow(d.diameter / 2, 2),
               s.mass * d.length * d.length / 12,
               s.mass * d.length * d.length / 12};
  s.seeker.boresight = aircraft.att.rotate({1, 0, 0});
  // The seeker leaves the rail looking where it was told its target is, if it
  // can look that far round: a lock held off the nose is not thrown away.
  if (const Vec3 line = target.position - s.position;
      target.valid && line.norm2() > 1 &&
      angle(s.seeker.boresight, line) <= d.seeker.gimbal)
    s.seeker.boresight = line.normalized();
  s.midcourse = target;
  s.seeker.phase = d.type == WeaponType::ActiveRadar ? SeekerPhase::Midcourse
                                                     : SeekerPhase::Searching;
  return s;
}
namespace {
// What a seeker at `position` receives from a source, relative to the least it
// can follow: heat falls off with the square of the range, a radar echo with
// the fourth power.
double seekerSignal(const MissileDefinition &d, Vec3 position,
                    const SensorTarget &source) {
  const double range = std::max((source.position - position).norm(), 1.);
  if (d.type == WeaponType::Infrared)
    return (source.signature >= 0 ? source.signature
                                  : infraredSignal(source, position)) *
           std::pow(d.seeker.signalRange / range, 2);
  return (source.signature >= 0 ? source.signature
                                : targetRcs(source, position)) *
         std::pow(d.seeker.signalRange / range, 4);
}
} // namespace
bool seekerDetects(const MissileDefinition &d, Vec3 position, Vec3 forward,
                   Vec3 boresight, const SensorTarget &target) {
  if (!target.alive)
    return false;
  const Vec3 los = target.position - position;
  const double range = los.norm();
  if (range <= 1e-3)
    return false;
  const double snr = seekerSignal(d, position, target);
  return range <= d.seeker.signalRange * 3 && snr >= 1 &&
         angle(forward, los) <= d.seeker.gimbal &&
         angle(boresight, los) <= d.seeker.fov / 2 &&
         lineOfSight(position, target.position);
}
const DecoyDefinition &decoyDefinition(DecoyType type) {
  // A flare outshines an engine at military power seen from behind (1.0) but
  // not one in reheat (up to 4.0). Chaff blooms to several times the echo of a
  // fighter seen side-on.
  static const DecoyDefinition flare{3.6, .12, 1.4, 1.5, .9, 1.};
  static const DecoyDefinition chaff{6., .35, 2.5, 45., 3.5, .06};
  return type == DecoyType::Flare ? flare : chaff;
}
unsigned decoyCapacity(AircraftType type) {
  const auto &definition = aircraftDefinition(type);
  // A bomber cannot turn away from a missile and carries far more to throw.
  return definition.bomber ? 60 : definition.gun ? 16 : 0;
}
double decoyStrength(DecoyType type, double age) {
  const auto &d = decoyDefinition(type);
  if (!(age >= 0) || age >= d.lifetime)
    return 0;
  if (age < d.rise)
    return d.peak * age / d.rise;
  if (age < d.hold)
    return d.peak;
  return d.peak * (d.lifetime - age) / (d.lifetime - d.hold);
}
Decoy releaseDecoy(DecoyType type, EntityRef owner, const AircraftConfig &cfg,
                   const State &aircraft, unsigned count) {
  Decoy decoy;
  decoy.type = type;
  decoy.owner = owner;
  // The dispensers sit between the engines, under the tail.
  const Vec3 dispenser = (cfg.engine_pos_l + cfg.engine_pos_r) * .5 +
                         Vec3{0, 0, .5} - loadedCg(cfg, aircraft);
  decoy.position = aircraft.pos_ned + aircraft.att.rotate(dispenser);
  decoy.velocity = aircraft.vel_ned +
                   aircraft.att.rotate({-8, count % 2 ? 12. : -12., 22});
  return decoy;
}
void advanceDecoy(Decoy &decoy, const Weather &weather, double dt) {
  if (!(dt > 0) || !std::isfinite(dt))
    return;
  const auto &d = decoyDefinition(decoy.type);
  // The air soon stops it; after that it drifts with the wind and sinks.
  const Vec3 wind = world().fromLocal(decoy.position, weather.wind_ned);
  const Vec3 slip = (decoy.velocity - wind) * std::exp(-d.drag * dt);
  const Vec3 next = wind + slip + world().down(decoy.position) * (d.gravity * kG0 * dt);
  decoy.position += (decoy.velocity + next) * (.5 * dt);
  decoy.velocity = next;
  decoy.age += dt;
}
SensorTarget decoySensor(const Decoy &decoy) {
  SensorTarget sensor;
  sensor.entity = {};
  sensor.position = decoy.position;
  sensor.velocity = decoy.velocity;
  sensor.signature = decoyStrength(decoy.type, decoy.age);
  sensor.alive = sensor.signature > 0;
  return sensor;
}
const Decoy *seekerDecoy(const MissileDefinition &d, const MissileState &s,
                         const SensorTarget *target,
                         std::span<const Decoy> decoys, std::uint64_t held,
                         const Weather &weather) {
  // Nearly always there is nothing in the air to be fooled by.
  if (decoys.empty())
    return nullptr;
  // A radar missile still flying on its launch aircraft's guidance has its own
  // seeker switched off: there is nothing to fool yet.
  if (d.type == WeaponType::ActiveRadar && !s.autonomous && s.midcourse.valid &&
      (s.midcourse.position - s.position).norm() > d.seeker.activationRange)
    return nullptr;
  const DecoyType wanted =
      d.type == WeaponType::Infrared ? DecoyType::Flare : DecoyType::Chaff;
  const Vec3 forward = s.attitude.rotate({1, 0, 0});
  const auto seen = [&](const SensorTarget &source) {
    return seekerDetects(d, s.position, forward, s.seeker.boresight, source);
  };
  // What the seeker is following now.
  const Decoy *following = nullptr;
  for (const auto &decoy : decoys)
    if (held && decoy.id == held && decoy.type == wanted &&
        seen(decoySensor(decoy)))
      following = &decoy;
  const bool onTarget = !following && target && seen(*target);
  if (!following && !onTarget)
    return nullptr;
  const SensorTarget current = following ? decoySensor(*following) : *target;
  // Sources in the same field of view are told apart by how strong they are,
  // not by which happens to be nearer: a flare falling behind does not grow
  // brighter to a missile coming up from astern.
  const auto strength = [&](const SensorTarget &source) {
    return source.signature >= 0              ? source.signature
           : d.type == WeaponType::Infrared ? infraredSignal(source, s.position)
                                            : targetRcs(source, s.position);
  };
  // A source the seeker already follows is not given up for one barely
  // stronger.
  constexpr double kHold = 1.25;
  const double threshold = strength(current) * kHold;
  const Vec3 line = (current.position - s.position).normalized();
  // Chaff stops in the air almost at once, so to a radar it closes at the
  // missile's own speed. An aircraft looks the same only while it flies across
  // the line of sight; one coming or going stands clear of the cloud.
  const auto competes = [&](Vec3 a, Vec3 b) {
    return d.type == WeaponType::Infrared ||
           std::abs((a - b).dot(line)) <= d.seeker.dopplerGate;
  };
  const Vec3 currentSpeed = following ? world().fromLocal(current.position, weather.wind_ned) : current.velocity;
  const Decoy *best = nullptr;
  double strongest = threshold;
  for (const auto &decoy : decoys) {
    if (decoy.type != wanted || &decoy == following ||
        !competes(world().fromLocal(decoy.position, weather.wind_ned), currentSpeed))
      continue;
    const auto sensor = decoySensor(decoy);
    const double signal = strength(sensor);
    if (signal > strongest && seen(sensor)) {
      strongest = signal;
      best = &decoy;
    }
  }
  if (best)
    return best;
  // Back to the aircraft once it outshines a fading decoy it is still beside.
  if (following && target && competes(target->velocity, currentSpeed) &&
      strength(*target) > threshold && seen(*target))
    return nullptr;
  return following;
}
Measurement updateSeeker(const MissileDefinition &d, MissileState &s,
                         const SensorTarget *target, double dt) {
  auto &seeker = s.seeker;
  const auto forward = s.attitude.rotate({1, 0, 0});
  bool detectable = target && seekerDetects(d, s.position, forward,
                                            seeker.boresight, *target);
  const Vec3 los = detectable ? target->position - s.position : Vec3{};
  if (detectable) {
    const double separation = angle(seeker.boresight, los);
    const double fraction =
        separation > 1e-8 ? std::min(1., d.seeker.rate * dt / separation) : 1;
    seeker.boresight =
        (seeker.boresight * (1 - fraction) + los.normalized() * fraction)
            .normalized();
    detectable = separation <= d.seeker.rate * dt + d.seeker.fov / 2;
  }
  if (detectable) {
    seeker.missed = 0;
    seeker.acquisition += dt;
    if (seeker.acquisition >= d.seeker.acquireTime) {
      seeker.phase = SeekerPhase::Tracking;
      seeker.measurement = {target->position, target->velocity, true};
    }
  } else {
    seeker.acquisition = 0;
    seeker.missed += dt;
    if (seeker.missed > d.seeker.memoryTime) {
      seeker.phase = SeekerPhase::Lost;
      seeker.measurement.valid = false;
      seeker.boresight = forward;
    } else if (seeker.measurement.valid)
      seeker.measurement.position += seeker.measurement.velocity * dt;
  }
  return seeker.measurement;
}
void advanceMissile(const MissileDefinition &d, MissileState &s,
                    const SensorTarget *target, const Measurement *support,
                    const Weather &weather, double dt) {
  if (!std::isfinite(dt) || dt <= 0 || dt > .1)
    throw std::invalid_argument("missile step 0..0.1 seconds");
  const unsigned steps = unsigned(std::ceil(dt * 480));
  const double h = dt / steps;
  for (unsigned step = 0; step < steps; ++step) {
    s.estimateAge += h;
    if (!s.autonomous && support && support->valid) {
      s.midcourse = *support;
      s.estimateAge = 0;
    } else if (s.midcourse.valid)
      s.midcourse.position += s.midcourse.velocity * h;
    Measurement measurement;
    if (d.type == WeaponType::ActiveRadar && !s.autonomous &&
        s.midcourse.valid &&
        (s.midcourse.position - s.position).norm() > d.seeker.activationRange) {
      s.seeker.phase = SeekerPhase::Midcourse;
      measurement = s.midcourse;
    } else {
      measurement = updateSeeker(d, s, target, h);
      if (measurement.valid && s.seeker.phase == SeekerPhase::Tracking)
        s.autonomous = true;
      if (!s.autonomous && d.type == WeaponType::ActiveRadar &&
          s.estimateAge < 12)
        measurement = s.midcourse;
    }
    // The target's acceleration, from how its measured velocity changes and
    // smoothed over a quarter of a second. What is measured arrives in steps
    // (a track from the launch aircraft several times a second), so each
    // change is taken over the time since the last one; a seeker that goes
    // over to a decoy sees the velocity jump once, which then counts for little.
    s.targetInterval += h;
    if (!measurement.valid || !s.targetMeasured) {
      s.targetAcceleration = {};
      s.targetVelocity = measurement.velocity;
      s.targetInterval = 0;
    } else if ((measurement.velocity - s.targetVelocity).norm2() > 1e-12) {
      const auto seen = limited((measurement.velocity - s.targetVelocity) / s.targetInterval, 12 * kG0);
      s.targetAcceleration += (seen - s.targetAcceleration) * std::min(1., s.targetInterval / .25);
      s.targetVelocity = measurement.velocity;
      s.targetInterval = 0;
    } else if (s.targetInterval > .3)
      s.targetAcceleration -= s.targetAcceleration * std::min(1., h / .25);
    s.targetMeasured = measurement.valid;
    const auto air = isaAtAltitude(world().altitude(s.position), weather.temp_offset_c);
    const auto airVelocity = s.velocity - world().fromLocal(s.position, weather.wind_ned);
    const double speed = airVelocity.norm(), q = .5 * air.rho * speed * speed,
                 mach = speed / air.sound;
    const auto body = s.attitude.inverseRotate(airVelocity);
    const double alpha = std::atan2(body.z, std::max(1., body.x)),
                 beta = std::atan2(body.y, std::hypot(body.x, body.z));
    const bool burning = s.propellant > 0 && s.age >= d.motor.ignitionDelay;
    const bool ramjet = d.motor.cruiseMach > 0 && burning &&
                        s.age >= d.motor.ignitionDelay + d.motor.boostTime;
    double thrust = burning ? motorThrust(d.motor, s.age) : 0;
    if (ramjet) {
      // Throttled to hold the cruise speed, never below a third, and starved
      // in thin air.
      const double throttle = clamp(.65 + 4 * (d.motor.cruiseMach - mach), .3, 1);
      thrust = d.motor.sustainThrust * throttle * clamp(air.rho / .5, .35, 1);
    }
    Vec3 command{};
    if (measurement.valid) {
      auto r = measurement.position - s.position;
      const auto rv = measurement.velocity - s.velocity;
      const double range = r.norm();
      if (d.loft > 0 && s.seeker.phase == SeekerPhase::Midcourse &&
          range > d.loftRange)
        r -= world().down(s.position) * (d.loft * (range - d.loftRange));
      command =
          proportionalNavigation(r, rv, s.velocity, d.navigation, d.maxG * kG0);
      // The part of the target's turn that is across the line of sight.
      if (d.anticipation > 0 && range > 1) {
        const auto line = r / range;
        command += (s.targetAcceleration - line * s.targetAcceleration.dot(line)) *
                   (d.anticipation * d.navigation * .5);
      }
      // Bounded initial heading capture through the same autopilot, towards
      // where the target will be when the missile gets there. PN alone gives
      // zero demand for a stationary target from an off-axis launch.
      const auto direction = s.velocity.normalized();
      const double closing = range > 1 ? -r.dot(rv) / range : 0;
      const double toGo =
          std::min(range / std::max({closing, speed * .5, 100.}), 20.);
      const auto lead = r + measurement.velocity * toGo;
      // Hard while the motor can pay for it, gently once it is coasting.
      command += limited((lead.normalized() - direction) * speed * .6,
                         (thrust > 0 ? d.maxG * .5 : 8) * kG0);
      command -= world().down(s.position) * kG0;
      command = limited(command, d.maxG * kG0);
    }
    const auto cb = s.attitude.inverseRotate(command);
    const double effectiveness = std::max(q * d.aero.area, 1.);
    // A vectored motor holds the body far off the airflow, and the thrust
    // itself then does the turning.
    const bool vectored = d.thrustVector > 0 && thrust > 0;
    const double alphaLimit = vectored ? std::max(d.maxAlpha, d.vectoredAlpha) : d.maxAlpha;
    const double turning = effectiveness * d.aero.normalSlope + thrust;
    const double desiredAlpha =
        clamp(-cb.z * s.mass / turning, -alphaLimit, alphaLimit);
    const double desiredBeta =
        clamp(-cb.y * s.mass / turning, -alphaLimit, alphaLimit);
    // Static stability + attitude-rate feedback; control creates moments, then
    // body incidence creates lift. No kinematic turning or velocity rotation.
    const double momentScale = q * d.aero.area * d.length;
    // The jet's share of the control moment, as a coefficient like the fins'.
    const double jet = vectored ? thrust * .5 * d.thrustVector / std::max(q * d.aero.area, 1.) : 0;
    const double authority = d.aero.controlMoment + jet;
    const double pitch = clamp((d.aero.stability * desiredAlpha +
                                4 * (desiredAlpha - alpha) - 2 * s.omega.y) /
                                   authority,
                               -d.controlLimit, d.controlLimit);
    const double yaw = clamp((-d.aero.stability * desiredBeta -
                              4 * (desiredBeta - beta) - 2 * s.omega.z) /
                                 authority,
                             -d.controlLimit, d.controlLimit);
    s.pitchControl = approach(s.pitchControl, pitch, d.actuatorRate * h);
    s.yawControl = approach(s.yawControl, yaw, d.actuatorRate * h);
    const double degradation =
        1 / (1 + std::pow(std::hypot(alpha, beta) / alphaLimit, 4));
    const double cnz = -d.aero.normalSlope * std::sin(alpha) * std::cos(alpha) +
                       d.aero.controlLift * s.pitchControl * degradation;
    const double cny = -d.aero.normalSlope * std::sin(beta) * std::cos(beta) -
                       d.aero.controlLift * s.yawControl * degradation;
    Vec3 lateral =
        limited(Vec3{0, cny, cnz} * q * d.aero.area, d.maxG * kG0 * s.mass);
    const double cd = dragCoefficient(d.aero, mach, std::hypot(cnz, cny));
    const double impulse = d.motor.boostThrust * d.motor.boostTime +
                           d.motor.sustainThrust * d.motor.sustainTime;
    const double burn =
        impulse > 0 ? d.motor.propellant * thrust / impulse * h : 0;
    s.propellant = std::max(0., s.propellant - burn);
    s.mass = d.launchMass - d.motor.propellant + s.propellant;
    s.inertia = {.5 * s.mass * std::pow(d.diameter / 2, 2),
                 s.mass * d.length * d.length / 12,
                 s.mass * d.length * d.length / 12};
    const auto bodyDirection = body.normalized();
    lateral -= bodyDirection * lateral.dot(bodyDirection);
    lateral = limited(lateral, d.maxG * kG0 * s.mass);
    const Vec3 forceWorld = s.attitude.rotate(Vec3{thrust, 0, 0} + lateral) -
                            airVelocity.normalized() * (q * d.aero.area * cd);
    s.velocity += (forceWorld / s.mass + world().down(s.position) * kG0) * h;
    const Vec3 delta = s.velocity * h;
    s.position += delta;
    s.distance += delta.norm();
    const double damping =
        d.aero.damping * d.length / (2 * std::max(speed, 1.));
    const Vec3 moment =
        Vec3{0,
             (d.aero.controlMoment * degradation + jet) * s.pitchControl -
                 d.aero.stability * std::sin(alpha),
             (d.aero.controlMoment * degradation + jet) * s.yawControl +
                 d.aero.stability * std::sin(beta)} *
        momentScale;
    const InertiaTensor tensor{s.inertia.x, s.inertia.y, s.inertia.z};
    const Vec3 driven =
        s.omega +
        tensor.solve(moment - s.omega.cross(tensor.apply(s.omega))) * h;
    // Treat aerodynamic angular damping implicitly. Axial inertia is small;
    // explicit damping becomes unstable even at 480 Hz during combined turns.
    s.omega = {driven.x / (1 + h * damping * momentScale / s.inertia.x),
               driven.y / (1 + h * damping * momentScale / s.inertia.y),
               driven.z / (1 + h * damping * momentScale / s.inertia.z)};
    const double turn = s.omega.norm() * h;
    if (turn > 1e-12) {
      const auto axis = s.omega.normalized();
      s.attitude =
          (s.attitude * Quat{std::cos(turn / 2), axis.x * std::sin(turn / 2),
                             axis.y * std::sin(turn / 2),
                             axis.z * std::sin(turn / 2)})
              .normalized();
    }
    s.age += h;
    s.motor = s.age < d.motor.ignitionDelay && s.propellant > 0
                  ? MotorPhase::Ignition
              : thrust <= 0 ? MotorPhase::Burnout
              : s.age < d.motor.ignitionDelay + d.motor.boostTime
                  ? MotorPhase::Boost
                  : MotorPhase::Sustain;
    s.telemetry = {
        speed,
        mach,
        world().altitude(s.position),
        .5 * s.mass * speed * speed,
        s.propellant,
        s.age,
        s.distance,
        measurement.valid ? (measurement.position - s.position).norm() : 0,
        command.norm() / kG0,
        lateral.norm() / s.mass / kG0,
        thrust,
        q};
  }
}
Envelope estimateEnvelope(const MissileDefinition &d, const State &s,
                          const Measurement &target) {
  const auto r = target.position - s.pos_ned;
  const double range = r.norm(),
               closure = range > 1
                             ? -(target.velocity - s.vel_ned).dot(r / range)
                             : 0;
  const auto air = isaAtAltitude(world().altitude(s.pos_ned));
  const double impulse = d.motor.boostThrust * d.motor.boostTime +
                         d.motor.sustainThrust * d.motor.sustainTime;
  const double avgMass = d.launchMass - d.motor.propellant * .5;
  const double peak = std::min(1400., s.vel_ned.norm() + impulse / avgMass);
  const double coast =
      avgMass / std::max(.5 * air.rho * d.aero.area * .8 * peak, 1.);
  const double flight = std::min(
      d.lifetime, d.motor.boostTime + d.motor.sustainTime + coast * .7);
  const double estimate =
      std::max(d.minimumRange, (peak * .5 + closure * .4) * flight * .65);
  return {d.minimumRange, estimate, range, closure,
          target.valid && range >= d.minimumRange && range <= estimate};
}
void Radar::reset() {
  tracks_.clear();
  selected = locked = {};
  mode = RadarMode::Search;
  nextScan_ = 0;
}
const Track *Radar::find(EntityRef ref) const {
  for (const auto &t : tracks_)
    if (t.entity == ref)
      return &t;
  return nullptr;
}
double Radar::signal(const State &own, const SensorTarget &target) const {
  const auto origin = own.pos_ned + own.att.rotate(definition.position);
  const auto r = own.att.inverseRotate(target.position - origin);
  const double distance = r.norm();
  if (!emitting || !target.alive || distance < 1 ||
      distance > definition.range || r.x <= 0 ||
      std::abs(std::atan2(r.y, r.x)) > definition.azimuth ||
      std::abs(std::atan2(-r.z, std::hypot(r.x, r.y))) > definition.elevation)
    return 0;
  const double snr = targetRcs(target, origin) *
                     std::pow(definition.referenceRange / distance, 4);
  if (snr < 1)
    return snr;
  return lineOfSight(origin, target.position) ? snr : 0;
}
void Radar::update(const State &own, std::span<const SensorTarget> targets,
                   EntityRef self, double time) {
  for (auto &track : tracks_) {
    const double elapsed = std::max(0., time - track.age);
    track.position += track.velocity * elapsed;
    track.age = time;
    track.quality = std::max(0., track.quality - .06 * elapsed);
  }
  std::erase_if(tracks_, [&](const auto &track) {
    return time - track.lastDetection > definition.coastTime ||
           std::none_of(targets.begin(), targets.end(), [&](const auto &t) {
             return t.entity == track.entity && t.alive;
           });
  });
  if (!find(selected))
    selected = {};
  if (!find(locked)) {
    locked = {};
    mode = RadarMode::Search;
  }
  if (time + 1e-9 < nextScan_)
    return;
  nextScan_ = time + definition.revisit;
  const double az =
      -definition.azimuth + 2 * definition.azimuth *
                                std::fmod(time, definition.scanPeriod) /
                                definition.scanPeriod;
  for (const auto &t : targets) {
    if (t.entity == self)
      continue;
    const auto body = own.att.inverseRotate(t.position - own.pos_ned);
    if (t.entity != locked &&
        std::abs(std::atan2(body.y, body.x) - az) > definition.beamWidth / 2)
      continue;
    if (signal(own, t) < 1)
      continue;
    auto it =
        std::find_if(tracks_.begin(), tracks_.end(), [&](const auto &track) {
          return track.entity == t.entity;
        });
    if (it == tracks_.end()) {
      if (tracks_.size() >= 16)
        continue;
      tracks_.push_back({t.entity, t.position, t.velocity, time, .4, time});
    } else {
      it->position = it->position * .25 + t.position * .75;
      it->velocity = it->velocity * .25 + t.velocity * .75;
      it->lastDetection = time;
      it->quality = std::min(1., it->quality + .25);
    }
  }
  if (const auto *t = find(locked); t && time - t->lastDetection > .5) {
    locked = {};
    mode = RadarMode::Search;
  }
}
void Radar::cycle(int direction) {
  if (tracks_.empty()) {
    selected = {};
    return;
  }
  auto it = std::find_if(tracks_.begin(), tracks_.end(),
                         [&](const auto &t) { return t.entity == selected; });
  const int size = int(tracks_.size()), index = it == tracks_.end()
                                                    ? (direction < 0 ? 0 : -1)
                                                    : int(it - tracks_.begin());
  selected = tracks_[(index + (direction < 0 ? -1 : 1) + size) % size].entity;
}
bool Radar::lockNearest(const State &own) {
  const Track *best = nullptr;
  double bestCosine = -2;
  const auto nose = own.att.rotate({1, 0, 0});
  for (const auto &track : tracks_) {
    if (track.quality < .35)
      continue;
    const double cosine =
        nose.dot((track.position - own.pos_ned).normalized());
    if (cosine > bestCosine) {
      bestCosine = cosine;
      best = &track;
    }
  }
  if (!best)
    return false;
  selected = locked = best->entity;
  mode = RadarMode::Track;
  return true;
}
bool Radar::toggleLock() {
  if (locked.id) {
    locked = {};
    mode = RadarMode::Search;
    return true;
  }
  if (const auto *t = find(selected); t && t->quality >= .35) {
    locked = selected;
    mode = RadarMode::Track;
    return true;
  }
  return false;
}
} // namespace ofs::weapons
