#include "ofs/net/replication.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace ofs::net {
namespace {
using Clock = std::chrono::steady_clock;
double micros(Clock::time_point start) {
  return std::chrono::duration<double, std::micro>(Clock::now() - start)
      .count();
}
struct Writer {
  Bytes b;
  Writer() { b.reserve(128); }
  void u(std::uint64_t v, unsigned n) {
    for (unsigned i = n; i > 0; --i)
      b.push_back(v >> ((i - 1) * 8));
  }
  void small(double v) { u(std::bit_cast<std::uint32_t>(float(v)), 4); }
  void f(double v) { u(std::bit_cast<std::uint64_t>(v), 8); }
  void vec(Vec3 v) {
    f(v.x);
    f(v.y);
    f(v.z);
  }
  void append(std::span<const std::uint8_t> v) {
    b.insert(b.end(), v.begin(), v.end());
  }
};
struct Reader {
  std::span<const std::uint8_t> b;
  std::size_t p{};
  bool ok{true};
  std::uint64_t u(unsigned n) {
    if (n > b.size() - p) {
      ok = false;
      return 0;
    }
    std::uint64_t v = 0;
    while (n--)
      v = (v << 8) | b[p++];
    return v;
  }
  double small() { return std::bit_cast<float>(std::uint32_t(u(4))); }
  double f() { return std::bit_cast<double>(u(8)); }
  Vec3 vec() {
    double x = f(), y = f(), z = f();
    return {x, y, z};
  }
  Bytes take(std::size_t n) {
    if (n > b.size() - p) {
      ok = false;
      return {};
    }
    Bytes v(b.begin() + p, b.begin() + p + n);
    p += n;
    return v;
  }
  bool end() const { return ok && p == b.size(); }
};
std::int64_t signedValue(std::uint64_t n, unsigned bits) {
  return n & (std::uint64_t{1} << (bits - 1))
             ? std::int64_t(n) - std::int64_t(std::uint64_t{1} << bits)
             : std::int64_t(n);
}
void q(Writer &w, double v, double scale, unsigned n) {
  if (!std::isfinite(v))
    throw std::invalid_argument("nonfinite quantized value");
  const auto max = double((std::uint64_t{1} << (n * 8 - 1)) - 1);
  w.u(std::uint64_t(
          std::int64_t(std::llround(std::clamp(v * scale, -max, max)))),
      n);
}
double q(Reader &r, double scale, unsigned n) {
  auto v = r.u(n);
  if (v == (std::uint64_t{1} << (n * 8 - 1)))
    r.ok = false;
  return double(signedValue(v, n * 8)) / scale;
}
void unit(Writer &w, double v) {
  w.u(std::llround(std::clamp(v, 0., 1.) * 255), 1);
}
double unit(Reader &r) { return double(r.u(1)) / 255; }
// Full owner projection is split along explicit serializer fields. No object
// layout is used.
constexpr std::array<std::size_t, 16> ownerSizes{
    16, 24, 24, 32, 24, 24, 45, 35, 40, 49, 8, 8, 32, 112, 2, 136};
std::size_t frameMemory(const NetFrame &f) {
  return sizeof(f) + f.entities.capacity() * sizeof(NetEntities::value_type);
}
Bytes positionField(Vec3 position, Vec3 ref) {
  Writer pos;
  const auto p = position - ref;
  bool relative = std::abs(p.x) <= 83886.07 && std::abs(p.y) <= 83886.07 &&
                  std::abs(p.z) <= 83886.07;
  pos.u(!relative, 1);
  if (relative) {
    q(pos, p.x, 100, 3);
    q(pos, p.y, 100, 3);
    q(pos, p.z, 100, 3);
  } else
    pos.vec(position);
  return std::move(pos.b);
}
constexpr std::size_t headerBytes = 99;
static_assert(headerBytes + 20 + 32 + 611 <= snapshotPayload,
              "full owner record must fit one application chunk");
void header(Writer &w, const NetFrame &f, unsigned index, unsigned count,
            unsigned records) {
  w.u(0x4f46534e, 4);
  w.u(protocolVersion, 2);
  w.u(unsigned(Type::SnapshotChunk), 1);
  w.u(0, 1);
  w.u(f.tick, 8);
  w.u(f.sequence, 8);
  w.u(f.baseline, 8);
  w.u(index, 1);
  w.u(count, 1);
  w.u(records, 1);
  w.vec(f.reference);
  w.vec(f.weather.wind_ned);
  w.f(f.weather.turbulence01);
  w.f(f.weather.temp_offset_c);
}
bool readHeader(Reader &r, NetFrame &f, unsigned &index, unsigned &count,
                unsigned &records) {
  if (r.u(4) != 0x4f46534e || r.u(2) != protocolVersion ||
      r.u(1) != unsigned(Type::SnapshotChunk) || r.u(1) != 0)
    return false;
  f.tick = r.u(8);
  f.sequence = r.u(8);
  f.baseline = r.u(8);
  index = r.u(1);
  count = r.u(1);
  records = r.u(1);
  f.reference = r.vec();
  f.weather.wind_ned = r.vec();
  f.weather.turbulence01 = r.f();
  f.weather.temp_offset_c = r.f();
  return r.ok && f.tick <= maxTimelineTick && f.sequence &&
         f.baseline < f.sequence && count && count <= fragmentLimit &&
         index < count && records <= maxPlayers &&
         std::isfinite(f.reference.norm2()) &&
         std::abs(f.reference.x) <= 1e12 && std::abs(f.reference.y) <= 1e12 &&
         std::abs(f.reference.z) <= 1e12 &&
         std::isfinite(f.weather.wind_ned.norm2()) &&
         std::abs(f.weather.wind_ned.x) <= 150 &&
         std::abs(f.weather.wind_ned.y) <= 150 &&
         std::abs(f.weather.wind_ned.z) <= 150 &&
         std::isfinite(f.weather.turbulence01) && f.weather.turbulence01 >= 0 &&
         f.weather.turbulence01 <= 1 &&
         std::isfinite(f.weather.temp_offset_c) &&
         std::abs(f.weather.temp_offset_c) <= 100;
}
bool sameHeader(const NetFrame &a, const NetFrame &b) {
  return a.sequence == b.sequence && a.baseline == b.baseline &&
         a.tick == b.tick && (a.reference - b.reference).norm2() == 0 &&
         (a.weather.wind_ned - b.weather.wind_ned).norm2() == 0 &&
         a.weather.turbulence01 == b.weather.turbulence01 &&
         a.weather.temp_offset_c == b.weather.temp_offset_c;
}
} // namespace
std::uint64_t nextSnapshotSequence(std::uint64_t current) {
  if (current == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("snapshot sequence exhausted; reconnect");
  return current + 1;
}
Tier interestTier(double d, Tier p) {
  if (!std::isfinite(d) || d < 0)
    return Tier::Outside;
  // 10% exit margin; promotion is immediate for combat proximity.
  if (d <= 5000 || (p == Tier::Near && d <= 5500))
    return Tier::Near;
  if (d <= 20000 || (p == Tier::Medium && d <= 22000))
    return Tier::Medium;
  if (d <= 50000 || (p == Tier::Far && d <= 55000))
    return Tier::Far;
  return Tier::Outside;
}
unsigned tierPeriod(Tier t) {
  switch (t) {
  case Tier::Owner:
  case Tier::Near:
    return 5;
  case Tier::Medium:
    return 12;
  case Tier::Far:
    return 60;
  default:
    return 120;
  }
}
AircraftNetState projectAircraft(const Aircraft &a, bool owner, Tier tier,
                                 Tick tick, Vec3 ref) {
  if (!a.id || !finiteState(a.state) || !validControls(a.controls))
    throw std::invalid_argument("invalid projection");
  AircraftNetState s;
  s.owner = owner;
  s.tier = tier;
  s.sampledTick = tick;
  if (owner) {
    auto exact = a;
    exact.state.time = 0;
    auto bytes = encodeAircraft(exact);
    std::size_t offset = 0;
    for (unsigned i = 0; i < 16; ++i) {
      s.fields[i] =
          Bytes(bytes.begin() + offset, bytes.begin() + offset + ownerSizes[i]);
      offset += ownerSizes[i];
    }
    if (offset != bytes.size())
      throw std::logic_error("owner projection layout");
    return s;
  }
  s.fields[0] = positionField(a.state.pos_ned, ref);
  auto quat = a.state.att.normalized();
  std::array<double, 4> v{quat.w, quat.x, quat.y, quat.z};
  unsigned largest = 0;
  for (unsigned i = 1; i < 4; ++i)
    if (std::abs(v[i]) > std::abs(v[largest]))
      largest = i;
  const double sign = v[largest] < 0 ? -1 : 1;
  Writer orientation;
  orientation.u(largest, 1);
  for (unsigned i = 0; i < 4; ++i)
    if (i != largest)
      q(orientation, v[i] * sign, 32767 * std::sqrt(2.), 2);
  s.fields[1] = std::move(orientation.b);
  Writer velocity;
  for (double x : {a.state.vel_ned.x, a.state.vel_ned.y, a.state.vel_ned.z})
    q(velocity, x, 16, 2);
  s.fields[2] = std::move(velocity.b);
  Writer omega;
  for (double x :
       {a.state.omega_body.x, a.state.omega_body.y, a.state.omega_body.z})
    q(omega, x, 2048, 2);
  s.fields[3] = std::move(omega.b);
  Writer controls;
  for (double x : {a.controls.gear01, a.controls.flap01, a.controls.spoiler01,
                   a.controls.throttle[0], a.controls.throttle[1]})
    unit(controls, x);
  q(controls, a.controls.steering, 127, 1);
  controls.u(a.controls.maneuver_mode, 1);
  s.fields[4] = std::move(controls.b);
  Writer engine;
  for (unsigned i = 0; i < 2; ++i) {
    unit(engine, a.state.n1[i]);
    unit(engine, a.state.afterburner[i]);
    unit(engine, a.state.inlet_spike[i]);
    q(engine, a.state.nozzle_angle[i], 32767 / (30 * kDeg2Rad), 2);
  }
  s.fields[5] = std::move(engine.b);
  Writer surfaces;
  for (double x :
       {a.state.elevator, a.state.aileron, a.state.rudder, a.state.flap,
        a.state.spoiler, a.state.canard, a.state.elevon_l, a.state.elevon_r})
    q(surfaces, x, 32767, 2);
  unit(surfaces, airframeIntegrity(a.state));
  s.fields[6] = std::move(surfaces.b);
  Writer life;
  life.u(unsigned(a.type) | (a.state.fuel_mass > 0 ? 0x80 : 0) |
             (a.life.alive() ? 0x10 : 0) |
             (aircraftCrashed(a.state) ? 0x08 : 0),
         1);
  life.u(a.life.generation, 4);
  life.u(std::llround(std::clamp(a.life.health, 0., 100.) * 100), 2);
  life.u(a.life.ammo, 2);
  life.u(a.life.readyTick, 8);
  life.u(a.life.respawnTick, 8);
  life.u(a.life.kills, 4);
  life.u(a.life.deaths, 4);
  s.fields[7] = std::move(life.b);
  Writer loading;
  for (double mass : {a.state.fuel_mass, a.state.payload_mass})
    loading.u(mass < 0 ? 0xffffff : std::uint64_t(std::llround(mass * 16)), 3);
  for (double offset : {a.state.payload_offset.x, a.state.payload_offset.y,
                        a.state.payload_offset.z})
    q(loading, offset, 1000, 2);
  for (double v : {a.state.payload_inertia_correction.x,
                   a.state.payload_inertia_correction.y,
                   a.state.payload_inertia_correction.z,
                   a.state.payload_products_correction.x,
                   a.state.payload_products_correction.y,
                   a.state.payload_products_correction.z})
    loading.small(v);
  s.fields[8] = std::move(loading.b);
  return s;
}
bool expandAircraft(EntityId id, const AircraftNetState &s, Vec3 ref,
                    Aircraft &a) {
  if (!id || unsigned(s.tier) > 3 || (s.owner != (s.tier == Tier::Owner)))
    return false;
  a = {};
  a.id = id;
  if (s.owner) {
    Writer w;
    for (unsigned i = 0; i < 16; ++i) {
      if (s.fields[i].size() != ownerSizes[i])
        return false;
      w.append(s.fields[i]);
    }
    if (!decodeAircraft(w.b, a) || a.id != id)
      return false;
  } else {
    constexpr std::array<unsigned, 9> sizes{0, 7, 6, 6, 7, 10, 17, 33, 36};
    for (unsigned i = 1; i < 9; ++i)
      if (s.fields[i].size() != sizes[i])
        return false;
    for (unsigned i = 9; i < 16; ++i)
      if (!s.fields[i].empty())
        return false;
    Reader pos{s.fields[0]};
    auto mode = pos.u(1);
    if (mode > 1)
      return false;
    if (mode == 0) {
      double x = q(pos, 100, 3), y = q(pos, 100, 3), z = q(pos, 100, 3);
      a.state.pos_ned = ref + Vec3{x, y, z};
    } else
      a.state.pos_ned = pos.vec();
    if (!pos.end())
      return false;
    Reader rot{s.fields[1]};
    auto index = rot.u(1);
    if (index > 3)
      return false;
    std::array<double, 4> v{};
    double sum = 0;
    for (unsigned i = 0; i < 4; ++i)
      if (i != index) {
        v[i] = q(rot, 32767 * std::sqrt(2.), 2);
        sum += v[i] * v[i];
      }
    if (!rot.end() || sum > 1.0001)
      return false;
    v[index] = std::sqrt(std::max(0., 1 - sum));
    a.state.att = Quat{v[0], v[1], v[2], v[3]}.normalized();
    Reader vel{s.fields[2]}, om{s.fields[3]};
    double x = q(vel, 16, 2), y = q(vel, 16, 2), z = q(vel, 16, 2);
    a.state.vel_ned = {x, y, z};
    x = q(om, 2048, 2);
    y = q(om, 2048, 2);
    z = q(om, 2048, 2);
    a.state.omega_body = {x, y, z};
    Reader ctrl{s.fields[4]};
    a.controls.gear01 = unit(ctrl);
    a.controls.flap01 = unit(ctrl);
    a.controls.spoiler01 = unit(ctrl);
    a.controls.throttle[0] = unit(ctrl);
    a.controls.throttle[1] = unit(ctrl);
    a.controls.steering = q(ctrl, 127, 1);
    const auto maneuver = ctrl.u(1);
    if (maneuver > 1) return false;
    a.controls.maneuver_mode = maneuver == 1;
    Reader eng{s.fields[5]};
    for (unsigned i = 0; i < 2; ++i) {
      a.state.n1[i] = unit(eng);
      a.state.afterburner[i] = unit(eng);
      a.state.inlet_spike[i] = unit(eng);
      a.state.nozzle_angle[i] = q(eng, 32767 / (30 * kDeg2Rad), 2);
    }
    Reader surf{s.fields[6]};
    for (double *p : {&a.state.elevator, &a.state.aileron, &a.state.rudder,
                      &a.state.flap, &a.state.spoiler, &a.state.canard,
                      &a.state.elevon_l, &a.state.elevon_r})
      *p = q(surf, 32767, 2);
    a.state.surface_health[5] = unit(surf);
    a.state.actuators_initialized = true;
    Reader life{s.fields[7]};
    const auto typeFlags = life.u(1);
    if (typeFlags & 0x60)
      return false;
    a.type = AircraftType(typeFlags & 7);
    const bool hasFuel = typeFlags & 0x80;
    a.state.surface_health[5] =
        (typeFlags & 0x08) ? 0 : std::max(.00101, a.state.surface_health[5]);
    a.life.generation = life.u(4);
    a.life.health = double(life.u(2)) / 100;
    // Quantization must never turn positive sub-cent health into destruction.
    // Existence/life classification is exact; this value is presentation only.
    if (typeFlags & 0x10) {
      if (a.life.health == 0)
        a.life.health = std::numeric_limits<double>::epsilon();
    } else if (a.life.health != 0)
      return false;
    a.life.ammo = life.u(2);
    a.life.readyTick = life.u(8);
    a.life.respawnTick = life.u(8);
    a.life.kills = life.u(4);
    a.life.deaths = life.u(4);
    Reader loading{s.fields[8]};
    auto mass = [&]() {
      const auto value = loading.u(3);
      return value == 0xffffff ? -1. : double(value) / 16.;
    };
    a.state.fuel_mass = mass();
    a.state.payload_mass = mass();
    if (a.state.fuel_mass > 1e6 || a.state.payload_mass > 1e6 ||
        (hasFuel && a.state.fuel_mass < 0))
      return false;
    double px = q(loading, 1000, 2), py = q(loading, 1000, 2),
           pz = q(loading, 1000, 2);
    a.state.payload_offset = {px, py, pz};
    const double offsetNorm = a.state.payload_offset.norm();
    if (offsetNorm > 20 + std::sqrt(3.) * .0005 + 1e-9)
      return false;
    if (offsetNorm > 20)
      a.state.payload_offset =
          a.state.payload_offset * ((20 - 1e-9) / offsetNorm);
    if (a.state.fuel_mass >= 0)
      a.state.fuel_mass = hasFuel ? std::max(.0001, a.state.fuel_mass) : 0.;
    a.state.payload_inertia_correction = {loading.small(), loading.small(),
                                          loading.small()};
    a.state.payload_products_correction = {loading.small(), loading.small(),
                                           loading.small()};
    if (!loading.end())
      return false;
    if (!vel.end() || !om.end() || !ctrl.end() || !eng.end() || !surf.end() ||
        !life.end())
      return false;
    if (!validAircraftType(a.type) || a.life.health > 100 ||
        a.life.ammo > 600 || (!aircraftDefinition(a.type).gun && a.life.ammo) ||
        (a.life.alive() && a.life.respawnTick))
      return false;
    // Quantization can overshoot a physical limit by half a quantum; clamp only
    // presentation.
    for (unsigned i = 0; i < 2; ++i)
      a.state.nozzle_angle[i] =
          std::clamp(a.state.nozzle_angle[i],
                     -aircraftDefinition(a.type).flight.engines[i].vector_limit,
                     aircraftDefinition(a.type).flight.engines[i].vector_limit);
  }
  a.state.time = double(s.sampledTick) * tickSeconds;
  return finiteState(a.state) && validControls(a.controls);
}
std::vector<Bytes> packetize(const NetFrame &f, const NetFrame *base) {
  if (!f.sequence || f.baseline >= f.sequence ||
      (bool(f.baseline) != bool(base)) || f.entities.size() > maxPlayers ||
      (base && (base->sequence != f.baseline || base->sequence >= f.sequence)))
    throw std::invalid_argument("snapshot baseline");
  std::vector<Bytes> bodies(1);
  std::vector<unsigned> counts(1);
  for (const auto &[id, s] : f.entities) {
    const AircraftNetState *previous = nullptr;
    if (base) {
      auto it = base->entities.find(id);
      if (it != base->entities.end() && it->second.owner == s.owner)
        previous = &it->second;
    }
    std::uint16_t mask = 0;
    for (unsigned i = 0; i < 16; ++i)
      if (!previous || previous->fields[i] != s.fields[i])
        mask |= std::uint16_t(1u << i);
    if (previous && !mask && previous->sampledTick == s.sampledTick &&
        previous->tier == s.tier)
      continue;
    Writer w;
    w.u(id, 8);
    w.u(s.owner, 1);
    w.u(unsigned(s.tier), 1);
    w.u(s.sampledTick, 8);
    w.u(mask, 2);
    for (unsigned i = 0; i < 16; ++i)
      if (mask & (1u << i)) {
        w.u(s.fields[i].size(), 2);
        w.append(s.fields[i]);
      }
    if (w.b.size() + headerBytes > snapshotPayload)
      throw std::length_error("entity exceeds application payload budget");
    if (bodies.back().size() + w.b.size() + headerBytes > snapshotPayload) {
      if (bodies.size() == fragmentLimit)
        throw std::length_error("snapshot exceeds chunk count budget");
      bodies.emplace_back();
      counts.push_back(0);
    }
    bodies.back().insert(bodies.back().end(), w.b.begin(), w.b.end());
    ++counts.back();
  }
  if (bodies.size() > fragmentLimit)
    throw std::length_error("snapshot exceeds fragment bound");
  std::vector<Bytes> packets;
  for (unsigned i = 0; i < bodies.size(); ++i) {
    Writer w;
    header(w, f, i, bodies.size(), counts[i]);
    w.append(bodies[i]);
    packets.push_back(std::move(w.b));
  }
  return packets;
}
void ReplicationStats::record(std::size_t n) {
  snapshotSizes.push_back(n);
  while (snapshotSizes.size() > 4096)
    snapshotSizes.pop_front();
}
std::size_t ReplicationStats::percentile(double fraction) const {
  if (snapshotSizes.empty())
    return 0;
  std::vector<std::size_t> v(snapshotSizes.begin(), snapshotSizes.end());
  std::sort(v.begin(), v.end());
  return v[std::size_t(std::clamp(fraction, 0., 1.) * double(v.size() - 1))];
}
InterestGrid::Cell InterestGrid::cell(Vec3 p) {
  auto axis = [](double v) {
    return std::int64_t(std::floor(std::clamp(v, -1e12, 1e12) / 20000));
  };
  return {axis(p.x), axis(p.y), axis(p.z)};
}
void InterestGrid::rebuild(std::span<const Aircraft> aircrafts) {
  const auto start = Clock::now();
  cells_.clear();
  aircrafts_.clear();
  remote_.clear();
  for (const auto &a : aircrafts) {
    cells_[cell(a.state.pos_ned)].push_back(a.id);
    aircrafts_[a.id] = &a;
  }
  indexUs_ = micros(start);
  const auto projection = Clock::now();
  for (const auto &a : aircrafts) {
    remote_[a.id] = projectAircraft(a, false, Tier::Near, 0, {});
  }
  projectionUs_ = micros(projection);
}
std::vector<EntityId> InterestGrid::query(Vec3 origin) const {
  auto [x, y, z] = cell(origin);
  std::vector<EntityId> ids;
  if (cells_.size() < 343) {
    for (const auto &[c, entities] : cells_) {
      auto [cx, cy, cz] = c;
      if (std::abs(cx - x) <= 3 && std::abs(cy - y) <= 3 &&
          std::abs(cz - z) <= 3)
        ids.insert(ids.end(), entities.begin(), entities.end());
    }
    return ids;
  }
  for (int i = -3; i <= 3; ++i)
    for (int j = -3; j <= 3; ++j)
      for (int k = -3; k <= 3; ++k) {
        auto it = cells_.find({x + i, y + j, z + k});
        if (it != cells_.end())
          ids.insert(ids.end(), it->second.begin(), it->second.end());
      }
  return ids;
}
AircraftNetState InterestGrid::remote(EntityId id, Tier tier, Tick tick,
                                      Vec3 reference) const {
  auto s = remote_.at(id);
  s.tier = tier;
  s.sampledTick = tick;
  s.fields[0] = positionField(aircrafts_.at(id)->state.pos_ned, reference);
  return s;
}
bool ReplicationSender::acknowledge(std::uint64_t sequence, bool recovery) {
  if (recovery)
    ++stats_.recoveryRequests;
  if (sequence > sequence_) {
    ++stats_.rejectedAcks;
    if (recovery)
      ++stats_.recoveryCoalesced;
    return false;
  }
  // Check monotonicity BEFORE history lookup. Expired old ACKs are harmless.
  if (sequence < acknowledged_ && sequence != 0) {
    ++stats_.staleAcks;
    if (recovery)
      ++stats_.recoveryCoalesced;
    return true;
  }
  bool missing = false;
  if (sequence > acknowledged_ &&
      std::none_of(history_.begin(), history_.end(),
                   [&](const auto &f) { return f.sequence == sequence; })) {
    ++stats_.baselineMisses;
    missing = true;
  } else if (sequence > acknowledged_) {
    acknowledged_ = sequence;
    if (recovery_ == Recovery::Waiting && sequence >= recoveryFirstSequence_) {
      recovery_ = Recovery::Normal;
      recoveryPending_ = false;
      ++stats_.recoveryCompleted;
    }
  } else {
    ++stats_.staleAcks;
  }
  // Zero is a baseline-reset request, not a rewind of the accepted ACK.
  if (recovery || missing) {
    if (recovery_ != Recovery::Normal ||
        (recoverySequence_ &&
         lastBuildTick_ - lastRecoveryTick_ < recoveryRetryTicks)) {
      if (recovery)
        ++stats_.recoveryCoalesced;
      if (recovery_ == Recovery::Normal)
        recoveryPending_ = true;
    } else {
      recovery_ = Recovery::Requested;
    }
  }
  return true;
}
ReplicationOutput ReplicationSender::build(Tick tick, EntityId owner,
                                           std::span<const Aircraft> aircrafts,
                                           const InterestGrid &grid,
                                           const Weather &weather) {
  const auto start = Clock::now();
  ReplicationOutput out;
  (void)aircrafts;
  const auto &all = grid.aircrafts();
  if (!all.contains(owner))
    return out;
  if (tick < lastBuildTick_ || tick > maxTimelineTick)
    throw std::invalid_argument("replication tick");
  lastBuildTick_ = tick;
  if (recoveryPending_ && recovery_ == Recovery::Normal &&
      tick - lastRecoveryTick_ >= recoveryRetryTicks) {
    recovery_ = Recovery::Requested;
    recoveryPending_ = false;
  }
  const Vec3 origin = all.at(owner)->state.pos_ned;
  auto candidates = grid.query(origin);
  candidates.push_back(owner);
  std::map<EntityId, Tier> selected;
  for (auto id : candidates) {
    if (!all.contains(id))
      continue;
    Tier tier =
        id == owner
            ? Tier::Owner
            : interestTier((all.at(id)->state.pos_ned - origin).norm(),
                           tiers_.contains(id) ? tiers_.at(id) : Tier::Outside);
    if (tier != Tier::Outside)
      selected[id] = tier;
  }
  stats_.queryUs = micros(start);
  bool entered = false;
  for (auto it = known_.begin(); it != known_.end();) {
    if (!selected.contains(it->first)) {
      Message m;
      m.type = Type::Despawn;
      m.tick = tick;
      m.entity = it->first;
      out.lifecycle.push_back(m);
      sentAt_.erase(it->first);
      loadingAt_.erase(it->first);
      it = known_.erase(it);
    } else
      ++it;
  }
  for (const auto &[id, tier] : selected) {
    (void)tier;
    const auto &a = *all.at(id);
    if (!known_.contains(id) || known_.at(id).type != a.type ||
        known_.at(id).life.generation != a.life.generation ||
        known_.at(id).life.alive() != a.life.alive()) {
      Message m;
      m.type = Type::Spawn;
      m.tick = tick;
      m.aircraft = a;
      out.lifecycle.push_back(m);
      known_[id] = a;
      entered = true;
    }
  }
  const NetFrame *base = nullptr;
  for (const auto &f : history_)
    if (f.sequence == acknowledged_)
      base = &f;
  // While awaiting the first usable ACK, depend on the sent recovery frame.
  // If it was lost these deltas are unusable, but retries remain bounded.
  if (!base && recovery_ == Recovery::Waiting)
    for (const auto &f : history_)
      if (f.sequence == recoverySequence_)
        base = &f;
  Vec3 reference{std::floor(origin.x / 1000) * 1000,
                 std::floor(origin.y / 1000) * 1000,
                 std::floor(origin.z / 1000) * 1000};
  const bool retry = recovery_ == Recovery::Waiting &&
                     (tick - lastRecoveryTick_ >= recoveryRetryTicks || !base);
  const bool recovering = recovery_ == Recovery::Requested || retry || !base;
  const bool keyframe = recovering || entered ||
                        tick - lastKeyframe_ >= keyframeTicks ||
                        (base->reference - reference).norm2() != 0;
  NetFrame f;
  f.tick = tick;
  f.sequence = sequence_ = nextSnapshotSequence(sequence_);
  f.baseline = keyframe ? 0 : base->sequence;
  f.reference = reference;
  f.weather = weather;
  if (keyframe) {
    ++stats_.keyframes;
    if (recovering) {
      ++stats_.recoveries;
      ++stats_.recoveryKeyframes;
      if (recovery_ == Recovery::Waiting)
        ++stats_.recoveryRetries;
      if (recovery_ != Recovery::Waiting)
        recoveryFirstSequence_ = f.sequence;
      recoverySequence_ = f.sequence;
      lastRecoveryTick_ = tick;
      recovery_ = Recovery::Waiting;
    }
    lastKeyframe_ = tick;
  } else {
    ++stats_.deltas;
    f.entities = base->entities;
  }
  f.entities.reserve(selected.size());
  for (auto it = f.entities.begin(); it != f.entities.end();)
    if (!selected.contains(it->first))
      it = f.entities.erase(it);
    else
      ++it;
  stats_.tierCounts = {};
  const auto projection = Clock::now();
  for (const auto &[id, tier] : selected) {
    ++stats_.tierCounts[unsigned(tier)];
    const bool due = tier == Tier::Owner || keyframe || !sentAt_.contains(id) ||
                     tick - sentAt_.at(id) >= tierPeriod(tier) ||
                     !tiers_.contains(id) || tiers_.at(id) != tier;
    // Each delta is independently applicable to the ACK baseline. Retain the
    // most recently sampled state even if it was sent in an unacknowledged
    // frame.
    if (due) {
      f.entities[id] = id == owner ? projectAircraft(*all.at(id), true, tier,
                                                     tick, reference)
                                   : grid.remote(id, tier, tick, reference);
      if (id != owner) {
        const bool loadingDue = keyframe || !loadingAt_.contains(id) ||
                                tick - loadingAt_.at(id) >= 120;
        if (!loadingDue && !history_.empty() &&
            history_.back().entities.contains(id))
          f.entities[id].fields[8] = std::span<const std::uint8_t>(
              history_.back().entities.at(id).fields[8]);
        else
          loadingAt_[id] = tick;
      }
      if (keyframe || !sentAt_.contains(id) || !tiers_.contains(id) ||
          tiers_.at(id) != tier)
        sentAt_[id] = tick;
      else {
        const auto period = tierPeriod(tier);
        sentAt_[id] += ((tick - sentAt_[id]) / period) * period;
      }
    } else if (!history_.empty() && history_.back().entities.contains(id))
      f.entities[id] = history_.back().entities.at(id);
  }
  tiers_ = selected;
  stats_.projectionUs = micros(projection);
  const auto encodeStart = Clock::now();
  out.packets = packetize(f, keyframe ? nullptr : base);
  stats_.deltaEncodeUs = micros(encodeStart);
  ++stats_.snapshots;
  stats_.entities += f.entities.size();
  std::size_t bytes = 0;
  for (const auto &p : out.packets) {
    ++stats_.chunks;
    bytes += p.size();
    stats_.maxChunk = std::max(stats_.maxChunk, p.size());
  }
  stats_.bytes += bytes;
  stats_.record(bytes);
  historyBytes_ += frameMemory(f);
  history_.push_back(std::move(f));
  while (history_.size() > baselineLimit) {
    historyBytes_ -= frameMemory(history_.front());
    history_.pop_front();
  }
  stats_.historyPeak = std::max(stats_.historyPeak, history_.size());
  return out;
}
std::size_t ReplicationSender::memoryBytes() const {
  std::size_t n = sizeof(*this) + known_.size() * (sizeof(Aircraft) + 48) +
                  tiers_.size() * 64 + sentAt_.size() * 64 +
                  loadingAt_.size() * 64;
  n += historyBytes_;
  n += stats_.snapshotSizes.size() * sizeof(std::size_t);
  return n;
}
bool ReplicationReceiver::spawn(const Aircraft &a, Tick tick) {
  if (!a.id || !finiteState(a.state) || !validControls(a.controls) ||
      !validAircraftType(a.type) || tick > maxTimelineTick ||
      (tombstones_.contains(a.id) && tick <= tombstones_.at(a.id)))
    return false;
  if (known_.size() >= maxPlayers && !known_.contains(a.id)) {
    recovery_ = true;
    return false;
  }
  auto it = known_.find(a.id);
  if (it != known_.end() &&
      (tick < it->second.second ||
       a.life.generation < it->second.first.life.generation))
    return false;
  known_[a.id] = {a, tick};
  tombstones_.erase(a.id);
  return true;
}
void ReplicationReceiver::despawn(EntityId id, Tick tick) {
  known_.erase(id);
  tombstones_[id] = tick;
  while (tombstones_.size() > maxPlayers * 2)
    tombstones_.erase(tombstones_.begin());
}
bool ReplicationReceiver::expire(Tick now) {
  const auto before = stats_.expiredAssemblies;
  for (auto it = assemblies_.begin(); it != assemblies_.end();)
    if (now >= it->second.arrived && now - it->second.arrived > assemblyTicks) {
      it = assemblies_.erase(it);
      ++stats_.expiredAssemblies;
    } else
      ++it;
  if (stats_.expiredAssemblies != before)
    recovery_ = true;
  return stats_.expiredAssemblies != before;
}
std::optional<NetFrame>
ReplicationReceiver::receive(std::span<const std::uint8_t> packet, Tick now,
                             EntityId expectedOwner) {
  const auto start = Clock::now();
  auto fail = [&]() -> std::optional<NetFrame> {
    ++stats_.decodeFailures;
    recovery_ = true;
    return std::nullopt;
  };
  expire(now);
  if (packet.size() < headerBytes || packet.size() > snapshotPayload)
    return fail();
  Reader r{packet};
  NetFrame h;
  unsigned index, count, records;
  if (!readHeader(r, h, index, count, records))
    return fail();
  if (h.sequence <= acknowledged_) {
    ++stats_.stale;
    return std::nullopt;
  }
  if (!assemblies_.contains(h.sequence)) {
    if (assemblies_.size() >= assemblyLimit) {
      assemblies_.erase(assemblies_.begin());
      ++stats_.expiredAssemblies;
    }
    assemblies_[h.sequence] = {h, now, std::uint8_t(count), {}};
  }
  auto &a = assemblies_.at(h.sequence);
  if (count != a.count || !sameHeader(a.header, h)) {
    assemblies_.erase(h.sequence);
    return fail();
  }
  if (a.chunks.contains(index)) {
    if (a.chunks.at(index) != Bytes(packet.begin(), packet.end())) {
      assemblies_.erase(h.sequence);
      return fail();
    }
    ++stats_.duplicates;
    return std::nullopt;
  }
  a.chunks[index] = Bytes(packet.begin(), packet.end());
  stats_.assemblyPeak = std::max(stats_.assemblyPeak, assemblies_.size());
  if (a.chunks.size() != count)
    return std::nullopt;
  NetFrame f = h;
  if (h.baseline) {
    auto it =
        std::find_if(history_.begin(), history_.end(), [&](const auto &old) {
          return old.sequence == h.baseline;
        });
    if (it == history_.end()) {
      ++stats_.baselineMisses;
      assemblies_.erase(h.sequence);
      recovery_ = true;
      return std::nullopt;
    }
    if ((it->reference - h.reference).norm2() != 0 || h.tick < it->tick) {
      assemblies_.erase(h.sequence);
      return fail();
    }
    f.entities = it->entities;
  }
  std::set<EntityId> updated;
  for (const auto &[i, bytes] : a.chunks) {
    (void)i;
    Reader body{bytes};
    NetFrame unused;
    unsigned ci, cc, n;
    if (!readHeader(body, unused, ci, cc, n)) {
      assemblies_.erase(h.sequence);
      return fail();
    }
    for (unsigned record = 0; record < n; ++record) {
      auto id = body.u(8), owner = body.u(1), tier = body.u(1),
           sampled = body.u(8), mask = body.u(2);
      if (!id || owner > 1 || tier > 3 || sampled > h.tick ||
          !updated.insert(id).second ||
          (!f.entities.contains(id) && mask != 65535)) {
        assemblies_.erase(h.sequence);
        return fail();
      }
      if (f.entities.size() >= maxPlayers && !f.entities.contains(id)) {
        assemblies_.erase(h.sequence);
        return fail();
      }
      auto &s = f.entities[id];
      if (f.entities.contains(id) && s.sampledTick > sampled) {
        assemblies_.erase(h.sequence);
        return fail();
      }
      s.owner = owner;
      s.tier = Tier(tier);
      s.sampledTick = sampled;
      for (unsigned field = 0; field < 16; ++field)
        if (mask & (1u << field)) {
          auto size = body.u(2);
          if (size > NetFields::capacities[field]) {
            assemblies_.erase(h.sequence);
            return fail();
          }
          s.fields[field] = body.take(size);
        }
      Aircraft expanded;
      if (!body.ok || !expandAircraft(id, s, h.reference, expanded)) {
        assemblies_.erase(h.sequence);
        return fail();
      }
    }
    if (!body.end()) {
      assemblies_.erase(h.sequence);
      return fail();
    }
  }
  // Reliable lifecycle is the existence authority. Snapshots may arrive first;
  // discard atomically and request refresh instead of constructing ghost
  // entities.
  unsigned owners = 0;
  for (auto it = f.entities.begin(); it != f.entities.end();) {
    if ((it->second.owner && it->second.sampledTick != f.tick) ||
        (expectedOwner && (it->first == expectedOwner) != it->second.owner)) {
      assemblies_.erase(h.sequence);
      return fail();
    }
    auto known = known_.find(it->first);
    if (known == known_.end()) {
      if (tombstones_.contains(it->first)) {
        it = f.entities.erase(it);
        continue;
      }
      ++stats_.baselineMisses;
      assemblies_.erase(h.sequence);
      recovery_ = true;
      return std::nullopt;
    }
    Aircraft expanded;
    if (!expandAircraft(it->first, it->second, f.reference, expanded)) {
      assemblies_.erase(h.sequence);
      return fail();
    }
    if (expanded.type != known->second.first.type ||
        expanded.life.generation != known->second.first.life.generation ||
        it->second.sampledTick < known->second.second) {
      ++stats_.baselineMisses;
      assemblies_.erase(h.sequence);
      recovery_ = true;
      return std::nullopt;
    }
    owners += it->second.owner;
    ++it;
  }
  if (owners != 1) {
    assemblies_.erase(h.sequence);
    return fail();
  }
  acknowledged_ = f.sequence;
  recovery_ = false;
  ++stats_.snapshots;
  if (f.baseline)
    ++stats_.deltas;
  else {
    ++stats_.keyframes;
    ++stats_.recoveries;
  }
  history_.push_back(f);
  historyBytes_ += frameMemory(history_.back());
  while (history_.size() > baselineLimit) {
    historyBytes_ -= frameMemory(history_.front());
    history_.pop_front();
  }
  for (auto it = assemblies_.begin(); it != assemblies_.end();)
    if (it->first <= acknowledged_)
      it = assemblies_.erase(it);
    else
      ++it;
  stats_.historyPeak = std::max(stats_.historyPeak, history_.size());
  stats_.decodeUs = micros(start);
  return f;
}
std::size_t ReplicationReceiver::memoryBytes() const {
  std::size_t n = sizeof(*this) + known_.size() * (sizeof(Aircraft) + 64) +
                  tombstones_.size() * 64;
  n += historyBytes_;
  return n + assemblyMemoryBytes();
}
std::size_t ReplicationReceiver::assemblyMemoryBytes() const {
  std::size_t n = 0;
  for (const auto &[id, a] : assemblies_) {
    (void)id;
    n += sizeof(a);
    for (const auto &[i, b] : a.chunks) {
      (void)i;
      n += b.capacity() + 64;
    }
  }
  return n;
}
} // namespace ofs::net
