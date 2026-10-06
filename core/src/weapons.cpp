#include "ofs/weapons.hpp"
#include "ofs/terrain.hpp"
#include <algorithm>
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
    d.motor = {24, 12000, 3, 0, 0};
    d.aero.area = kPi * .13 * .13 / 4;
    d.seeker = {24 * kDeg2Rad, 60 * kDeg2Rad, 120 * kDeg2Rad, 8000, .12, .5, 0};
    return d;
  }();
  static const MissileDefinition ar = [] {
    MissileDefinition d{};
    d.type = WeaponType::ActiveRadar;
    d.name = "Dev AR-157";
    d.length = 3.66;
    d.diameter = .178;
    d.launchMass = 157;
    d.motor = {55, 18000, 3, 5500, 6};
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
void Inventory::reset(AircraftType type) {
  stations.clear();
  selected = WeaponType::Infrared;
  if (type == AircraftType::Typhoon)
    stations = {{{0, -4.3, .5}, 6, WeaponType::Infrared},
                {{0, 4.3, .5}, 6, WeaponType::Infrared},
                {{-.5, -1.5, .8}, 6, WeaponType::ActiveRadar},
                {{-.5, 1.5, .8}, 6, WeaponType::ActiveRadar}};
  if (type == AircraftType::Su57)
    stations = {{{.5, -3.5, .4}, 6, WeaponType::Infrared},
                {{.5, 3.5, .4}, 6, WeaponType::Infrared},
                {{0, -.6, .7}, 6, WeaponType::ActiveRadar},
                {{0, .6, .7}, 6, WeaponType::ActiveRadar}};
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
      const auto mass = missileDefinition(station.mounted).launchMass;
      total += mass;
      moment += station.position * mass;
      point(mass, station.position);
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
  if (a.z >= groundHeightNed(a.x, a.y) || b.z >= groundHeightNed(b.x, b.y))
    return false;
  const double distance = (b - a).norm();
  const double horizon = std::sqrt(2 * 6371000 * std::max(0., -a.z)) +
                         std::sqrt(2 * 6371000 * std::max(0., -b.z));
  if (distance > horizon)
    return false;
  const unsigned steps =
      std::min(256u, std::max(1u, unsigned(distance / 500) + 1));
  for (unsigned i = 1; i < steps; ++i) {
    const auto p = a + (b - a) * (double(i) / steps);
    if (p.z >= groundHeightNed(p.x, p.y))
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
  if (t.type == AircraftType::SR71) {
    front = 2;
    side = 8;
    rear = 4;
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
  s.velocity = aircraft.vel_ned + aircraft.att.rotate({0, 0, 4});
  s.attitude = aircraft.att;
  s.omega = aircraft.omega_body;
  s.propellant = d.motor.propellant;
  s.mass = d.launchMass;
  s.inertia = {.5 * s.mass * std::pow(d.diameter / 2, 2),
               s.mass * d.length * d.length / 12,
               s.mass * d.length * d.length / 12};
  s.seeker.boresight = aircraft.att.rotate({1, 0, 0});
  s.midcourse = target;
  s.seeker.phase = d.type == WeaponType::ActiveRadar ? SeekerPhase::Midcourse
                                                     : SeekerPhase::Searching;
  return s;
}
Measurement updateSeeker(const MissileDefinition &d, MissileState &s,
                         const SensorTarget *target, double dt) {
  auto &seeker = s.seeker;
  bool detectable = target && target->alive;
  Vec3 los{};
  double range = 0;
  if (detectable) {
    los = target->position - s.position;
    range = los.norm();
    detectable = range > 1e-3;
  }
  const auto forward = s.attitude.rotate({1, 0, 0});
  if (detectable) {
    const double snr =
        d.type == WeaponType::Infrared
            ? infraredSignal(*target, s.position) *
                  std::pow(d.seeker.signalRange / std::max(range, 1.), 2)
            : targetRcs(*target, s.position) *
                  std::pow(d.seeker.signalRange / std::max(range, 1.), 4);
    detectable = range <= d.seeker.signalRange * 3 && snr >= 1 &&
                 angle(forward, los) <= d.seeker.gimbal &&
                 angle(seeker.boresight, los) <= d.seeker.fov / 2 &&
                 lineOfSight(s.position, target->position);
  }
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
    const auto air = isaAtAltitude(-s.position.z, weather.temp_offset_c);
    const auto airVelocity = s.velocity - weather.wind_ned;
    const double speed = airVelocity.norm(), q = .5 * air.rho * speed * speed,
                 mach = speed / air.sound;
    const auto body = s.attitude.inverseRotate(airVelocity);
    const double alpha = std::atan2(body.z, std::max(1., body.x)),
                 beta = std::atan2(body.y, std::hypot(body.x, body.z));
    Vec3 command{};
    if (measurement.valid) {
      const auto r = measurement.position - s.position,
                 rv = measurement.velocity - s.velocity;
      command =
          proportionalNavigation(r, rv, s.velocity, d.navigation, d.maxG * kG0);
      // Bounded initial heading capture through the same autopilot. PN alone
      // gives zero demand for a stationary target from an off-axis launch.
      const auto direction = s.velocity.normalized();
      command += limited((r.normalized() - direction) * speed * .6, 8 * kG0);
      command.z -= kG0;
      command = limited(command, d.maxG * kG0);
    }
    const auto cb = s.attitude.inverseRotate(command);
    const double effectiveness = std::max(q * d.aero.area, 1.);
    const double desiredAlpha =
        clamp(-cb.z * s.mass / (effectiveness * d.aero.normalSlope),
              -d.maxAlpha, d.maxAlpha);
    const double desiredBeta =
        clamp(-cb.y * s.mass / (effectiveness * d.aero.normalSlope),
              -d.maxAlpha, d.maxAlpha);
    // Static stability + attitude-rate feedback; control creates moments, then
    // body incidence creates lift. No kinematic turning or velocity rotation.
    const double pitch = clamp((d.aero.stability * desiredAlpha +
                                4 * (desiredAlpha - alpha) - 2 * s.omega.y) /
                                   d.aero.controlMoment,
                               -d.controlLimit, d.controlLimit);
    const double yaw = clamp((-d.aero.stability * desiredBeta -
                              4 * (desiredBeta - beta) - 2 * s.omega.z) /
                                 d.aero.controlMoment,
                             -d.controlLimit, d.controlLimit);
    s.pitchControl = approach(s.pitchControl, pitch, d.actuatorRate * h);
    s.yawControl = approach(s.yawControl, yaw, d.actuatorRate * h);
    const double degradation =
        1 / (1 + std::pow(std::hypot(alpha, beta) / d.maxAlpha, 4));
    const double cnz = -d.aero.normalSlope * std::sin(alpha) * std::cos(alpha) +
                       d.aero.controlLift * s.pitchControl * degradation;
    const double cny = -d.aero.normalSlope * std::sin(beta) * std::cos(beta) -
                       d.aero.controlLift * s.yawControl * degradation;
    Vec3 lateral =
        limited(Vec3{0, cny, cnz} * q * d.aero.area, d.maxG * kG0 * s.mass);
    const double cd = dragCoefficient(d.aero, mach, std::hypot(cnz, cny));
    const double thrust = s.propellant > 0 ? motorThrust(d.motor, s.age) : 0;
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
    s.velocity += (forceWorld / s.mass + Vec3{0, 0, kG0}) * h;
    const Vec3 delta = s.velocity * h;
    s.position += delta;
    s.distance += delta.norm();
    const double damping =
        d.aero.damping * d.length / (2 * std::max(speed, 1.));
    const double momentScale = q * d.aero.area * d.length;
    const Vec3 moment =
        Vec3{0,
             d.aero.controlMoment * s.pitchControl * degradation -
                 d.aero.stability * std::sin(alpha),
             d.aero.controlMoment * s.yawControl * degradation +
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
    s.motor = thrust <= 0                 ? MotorPhase::Burnout
              : s.age < d.motor.boostTime ? MotorPhase::Boost
                                          : MotorPhase::Sustain;
    s.telemetry = {
        speed,
        mach,
        -s.position.z,
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
  const auto air = isaAtAltitude(-s.pos_ned.z);
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
