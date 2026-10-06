#include "ofs/net/protocol.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace ofs::net {
namespace {
static_assert(sizeof(double) == 8 && sizeof(float) == 4 &&
              std::numeric_limits<double>::is_iec559);
struct Writer {
  std::vector<std::uint8_t> bytes;
  void integer(std::uint64_t n, unsigned size) {
    for (unsigned i = 0; i < size; ++i)
      bytes.push_back(static_cast<std::uint8_t>(n >> ((size - 1 - i) * 8)));
  }
  void real(double n) { integer(std::bit_cast<std::uint64_t>(n), 8); }
  void small(double n) {
    integer(std::bit_cast<std::uint32_t>(static_cast<float>(n)), 4);
  }
  void string(const std::string &s) {
    if (s.size() > 64)
      throw std::invalid_argument("text exceeds 64 bytes");
    integer(s.size(), 1);
    bytes.insert(bytes.end(), s.begin(), s.end());
  }
};
struct Reader {
  std::span<const std::uint8_t> bytes;
  std::size_t pos{};
  bool ok{true};
  std::uint64_t integer(unsigned size) {
    if (size > bytes.size() - pos) {
      ok = false;
      return 0;
    }
    std::uint64_t n = 0;
    for (unsigned i = 0; i < size; ++i)
      n = (n << 8) | bytes[pos++];
    return n;
  }
  double real() { return std::bit_cast<double>(integer(8)); }
  double small() {
    return std::bit_cast<float>(static_cast<std::uint32_t>(integer(4)));
  }
  std::string string() {
    auto n = integer(1);
    if (n > 64 || n > bytes.size() - pos) {
      ok = false;
      return {};
    }
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
      auto c = bytes[pos++];
      if (c < 32 || c > 126)
        ok = false;
      s.push_back(static_cast<char>(c));
    }
    return s;
  }
};
std::array<double, 11> values(const Controls &c) {
  return {c.elevator_stick, c.aileron_stick, c.rudder_pedal, c.flap01,
          c.spoiler01,      c.gear01,        c.throttle[0],  c.throttle[1],
          c.brake01,        c.steering,      c.elevator_trim};
}
void controls(Writer &w, const Controls &c) {
  for (double v : values(c))
    w.small(v);
  w.integer(c.maneuver_mode, 1);
}
Controls controls(Reader &r) {
  Controls c;
  c.elevator_stick = r.small();
  c.aileron_stick = r.small();
  c.rudder_pedal = r.small();
  c.flap01 = r.small();
  c.spoiler01 = r.small();
  c.gear01 = r.small();
  c.throttle[0] = r.small();
  c.throttle[1] = r.small();
  c.brake01 = r.small();
  c.steering = r.small();
  c.elevator_trim = r.small();
  const auto mode = r.integer(1);
  if (mode > 1) r.ok = false;
  c.maneuver_mode = mode == 1;
  return c;
}

void compactControls(Writer &w, const Controls &c) {
  const auto v = values(c);
  for (unsigned i = 0; i < v.size(); ++i) {
    const bool signedAxis = i < 3 || i >= 9;
    // Reserved signed code rejects invalid local/test input without NaN casts.
    if (!std::isfinite(v[i]) || v[i] > 1 || v[i] < (signedAxis ? -1 : 0)) {
      w.integer(signedAxis ? 0x8000 : 0xffff, 2);
      continue;
    }
    const auto q = std::llround(v[i] * (signedAxis ? 32767 : 65534));
    w.integer(static_cast<std::uint16_t>(q), 2);
  }
  w.integer(c.maneuver_mode, 1);
}
Controls compactControls(Reader &r) {
  std::array<double, 11> v{};
  for (unsigned i = 0; i < v.size(); ++i) {
    const auto q = static_cast<std::uint16_t>(r.integer(2));
    if (i < 3 || i >= 9) {
      if (q == 0x8000)
        r.ok = false;
      v[i] = double(q > 32767 ? int(q) - 65536 : int(q)) / 32767;
    } else {
      if (q == 65535)
        r.ok = false;
      v[i] = double(q) / 65534;
    }
  }
  Controls c;
  c.elevator_stick = v[0];
  c.aileron_stick = v[1];
  c.rudder_pedal = v[2];
  c.flap01 = v[3];
  c.spoiler01 = v[4];
  c.gear01 = v[5];
  c.throttle[0] = v[6];
  c.throttle[1] = v[7];
  c.brake01 = v[8];
  c.steering = v[9];
  c.elevator_trim = v[10];
  const auto mode = r.integer(1);
  if (mode > 1) r.ok = false;
  c.maneuver_mode = mode == 1;
  return c;
}
void vec(Writer &w, const Vec3 &v) {
  w.real(v.x);
  w.real(v.y);
  w.real(v.z);
}
Vec3 vec(Reader &r) {
  double x = r.real(), y = r.real(), z = r.real();
  return {x, y, z};
}
void weather(Writer &w, const Weather &value) {
  vec(w, value.wind_ned);
  w.real(value.turbulence01);
  w.real(value.temp_offset_c);
}
Weather weather(Reader &r) {
  Weather value;
  value.wind_ned = vec(r);
  value.turbulence01 = r.real();
  value.temp_offset_c = r.real();
  for (double v : {value.wind_ned.x, value.wind_ned.y, value.wind_ned.z})
    if (!std::isfinite(v) || std::abs(v) > 150)
      r.ok = false;
  if (!std::isfinite(value.turbulence01) || value.turbulence01 < 0 ||
      value.turbulence01 > 1 || !std::isfinite(value.temp_offset_c) ||
      std::abs(value.temp_offset_c) > 100)
    r.ok = false;
  return value;
}
void aircraft(Writer &w, const Aircraft &a) {
  w.integer(a.id, 8);
  w.integer(a.acknowledged, 8);
  const auto &s = a.state;
  vec(w, s.pos_ned);
  vec(w, s.vel_ned);
  w.real(s.att.w);
  w.real(s.att.x);
  w.real(s.att.y);
  w.real(s.att.z);
  vec(w, s.omega_body);
  w.real(s.n1[0]);
  w.real(s.n1[1]);
  w.real(s.time);
  controls(w, a.controls);
  w.small(a.life.health);
  w.integer(a.life.ammo, 2);
  w.integer(a.life.generation, 4);
  w.integer(a.life.readyTick, 8);
  w.integer(a.life.respawnTick, 8);
  w.integer(a.life.kills, 4);
  w.integer(a.life.deaths, 4);
  w.integer(static_cast<unsigned>(a.type), 1);
  w.small(s.afterburner[0]);
  w.small(s.afterburner[1]);
  w.real(s.inlet_spike[0]);
  w.real(s.inlet_spike[1]);
  w.real(s.nozzle_angle[0]);
  w.real(s.nozzle_angle[1]);
  w.integer(s.aero_memory_initialized, 1);
  for (double v : s.alpha_lag)
    w.real(v);
  for (double v : s.separation)
    w.real(v);
  for (double v : s.vortex_state)
    w.real(v);
  w.real(s.trim_reference);
  w.real(s.fuel_mass);
  w.real(s.payload_mass);
  vec(w, s.payload_offset);
  for (double v : s.engine_health)
    w.real(v);
  for (double v : s.surface_health)
    w.real(v);
  for (double v : s.surface_drag)
    w.real(v);
  w.integer(s.actuators_initialized, 1);
  w.integer(s.fcs_enabled, 1);
  for (double v :
       {s.pilot_pitch, s.pilot_roll, s.pilot_yaw, s.elevator, s.aileron,
        s.rudder, s.flap, s.spoiler, s.canard, s.elevon_l, s.elevon_r})
    w.real(v);
  vec(w, s.payload_inertia_correction);
  vec(w, s.payload_products_correction);
}
Aircraft aircraft(Reader &r) {
  Aircraft a;
  a.id = r.integer(8);
  a.acknowledged = r.integer(8);
  auto &s = a.state;
  s.pos_ned = vec(r);
  s.vel_ned = vec(r);
  s.att.w = r.real();
  s.att.x = r.real();
  s.att.y = r.real();
  s.att.z = r.real();
  s.omega_body = vec(r);
  s.n1[0] = r.real();
  s.n1[1] = r.real();
  s.time = r.real();
  a.controls = controls(r);
  a.life.health = r.small();
  a.life.ammo = r.integer(2);
  a.life.generation = r.integer(4);
  a.life.readyTick = r.integer(8);
  a.life.respawnTick = r.integer(8);
  a.life.kills = r.integer(4);
  a.life.deaths = r.integer(4);
  a.type = static_cast<AircraftType>(r.integer(1));
  s.afterburner[0] = r.small();
  s.afterburner[1] = r.small();
  s.inlet_spike[0] = r.real();
  s.inlet_spike[1] = r.real();
  s.nozzle_angle[0] = r.real();
  s.nozzle_angle[1] = r.real();
  const auto aeroInitialized = r.integer(1);
  if (aeroInitialized > 1)
    r.ok = false;
  s.aero_memory_initialized = aeroInitialized;
  for (auto &v : s.alpha_lag)
    v = r.real();
  for (auto &v : s.separation)
    v = r.real();
  for (auto &v : s.vortex_state)
    v = r.real();
  s.trim_reference = r.real();
  s.fuel_mass = r.real();
  s.payload_mass = r.real();
  s.payload_offset = vec(r);
  for (auto &v : s.engine_health)
    v = r.real();
  for (auto &v : s.surface_health)
    v = r.real();
  for (auto &v : s.surface_drag)
    v = r.real();
  auto initialized = r.integer(1), enabled = r.integer(1);
  if (initialized > 1 || enabled > 1)
    r.ok = false;
  s.actuators_initialized = initialized;
  s.fcs_enabled = enabled;
  for (double *v :
       {&s.pilot_pitch, &s.pilot_roll, &s.pilot_yaw, &s.elevator, &s.aileron,
        &s.rudder, &s.flap, &s.spoiler, &s.canard, &s.elevon_l, &s.elevon_r})
    *v = r.real();

  if (!validAircraftType(a.type) ||
      (!aircraftDefinition(a.type).gun && a.life.ammo != 0))
    r.ok = false;
  if (!std::isfinite(a.life.health) || a.life.health < 0 ||
      a.life.health > 100 || a.life.ammo > 600 ||
      (a.life.alive() && a.life.respawnTick))
    r.ok = false;
  if (validAircraftType(a.type))
    for (unsigned e = 0; e < 2; ++e)
      if (std::abs(s.nozzle_angle[e]) >
          aircraftDefinition(a.type).flight.engines[e].vector_limit + 1e-12)
        r.ok = false;
  s.payload_inertia_correction = vec(r);
  s.payload_products_correction = vec(r);
  if (!a.id || !finiteState(s) || !validControls(a.controls))
    r.ok = false;
  return a;
}
} // namespace
bool sameControls(const Controls &a, const Controls &b) {
  return values(a) == values(b) && a.maneuver_mode == b.maneuver_mode;
}
bool validControls(const Controls &c) {
  const auto v = values(c);
  for (std::size_t i = 0; i < v.size(); ++i)
    if (!std::isfinite(v[i]) || v[i] > (1.0) ||
        v[i] < ((i < 3 || i >= 9) ? -1.0 : 0.0))
      return false;
  return true;
}
bool finiteState(const State &s) {
  for (double v : {s.pos_ned.x, s.pos_ned.y, s.pos_ned.z, s.vel_ned.x,
                   s.vel_ned.y, s.vel_ned.z, s.att.w, s.att.x, s.att.y, s.att.z,
                   s.omega_body.x, s.omega_body.y, s.omega_body.z, s.n1[0],
                   s.n1[1], s.afterburner[0], s.afterburner[1], s.time})
    if (!std::isfinite(v))
      return false;
  for (double v : {s.fuel_mass, s.payload_mass, s.payload_offset.x,
                   s.payload_offset.y, s.payload_offset.z})
    if (!std::isfinite(v))
      return false;
  if (s.fuel_mass < -1 || s.payload_mass < -1 || s.fuel_mass > 1e6 ||
      s.payload_mass > 1e6 || s.payload_offset.norm() > 20)
    return false;
  for (auto v : s.engine_health)
    if (!std::isfinite(v) || v < 0 || v > 1)
      return false;
  for (unsigned i = 0; i < 2; ++i) {
    if (!std::isfinite(s.alpha_lag[i]) || std::abs(s.alpha_lag[i]) > kPi ||
        !std::isfinite(s.separation[i]) || s.separation[i] < 0 ||
        s.separation[i] > 1 || !std::isfinite(s.vortex_state[i]) ||
        s.vortex_state[i] < 0 || s.vortex_state[i] > 1)
      return false;
  }
  for (auto v : s.nozzle_angle)
    if (!std::isfinite(v) || std::abs(v) > 30 * kDeg2Rad)
      return false;
  for (auto v : s.inlet_spike)
    if (!std::isfinite(v) || v < 0 || v > 1)
      return false;
  for (auto v : s.surface_health)
    if (!std::isfinite(v) || v < 0 || v > 1)
      return false;
  for (auto v : s.surface_drag)
    if (!std::isfinite(v) || v < 0 || v > 10)
      return false;
  for (double v :
       {s.trim_reference, s.pilot_pitch, s.pilot_roll, s.pilot_yaw, s.elevator,
        s.aileron, s.rudder, s.canard, s.elevon_l, s.elevon_r})
    if (!std::isfinite(v) || std::abs(v) > 1)
      return false;
  if (!std::isfinite(s.flap) || !std::isfinite(s.spoiler) || s.flap < 0 ||
      s.flap > 1 || s.spoiler < 0 || s.spoiler > 1)
    return false;
  for (double v :
       {s.payload_inertia_correction.x, s.payload_inertia_correction.y,
        s.payload_inertia_correction.z, s.payload_products_correction.x,
        s.payload_products_correction.y, s.payload_products_correction.z})
    if (!std::isfinite(v) || std::abs(v) > 1e7)
      return false;
  double q = s.att.w * s.att.w + s.att.x * s.att.x + s.att.y * s.att.y +
             s.att.z * s.att.z;
  return q > .99 && q < 1.01 && s.n1[0] >= 0 && s.n1[0] <= 1 && s.n1[1] >= 0 &&
         s.n1[1] <= 1 && s.afterburner[0] >= 0 && s.afterburner[0] <= 1 &&
         s.afterburner[1] >= 0 && s.afterburner[1] <= 1 && s.time >= 0;
}
Controls quantizeControls(const Controls &c) {
  Writer w;
  compactControls(w, c);
  Reader r{w.bytes};
  return compactControls(r);
}
std::vector<std::uint8_t> encodeAircraft(const Aircraft &a) {
  Writer w;
  aircraft(w, a);
  return w.bytes;
}
bool decodeAircraft(std::span<const std::uint8_t> bytes, Aircraft &out) {
  Reader r{bytes};
  auto a = aircraft(r);
  if (!r.ok || r.pos != bytes.size())
    return false;
  out = std::move(a);
  return true;
}
std::vector<std::uint8_t> encode(const Message &m) {
  Writer w;
  w.integer(0x4f46534e, 4);
  w.integer(protocolVersion, 2);
  w.integer(static_cast<unsigned>(m.type), 1);
  w.integer(0, 1);
  const bool input = m.type == Type::Input && !m.commands.empty();
  const Tick packetTick = input ? m.commands.back().tick : m.tick;
  const auto packetSequence = input ? m.commands.back().sequence : m.sequence;
  w.integer(packetTick, 8);
  w.integer(packetSequence, 8);
  switch (m.type) {
  case Type::Hello:
    w.string(m.text);
    w.integer(static_cast<unsigned>(m.aircraftType), 1);
    break;
  case Type::Reject:
    w.string(m.text);
    break;
  case Type::Welcome:
    w.integer(120, 2);
    w.integer(m.snapshotHz, 2);
    aircraft(w, m.aircraft);
    weather(w, m.weather);
    break;
  case Type::Spawn:
    aircraft(w, m.aircraft);
    break;
  case Type::Despawn:
  case Type::Joined:
  case Type::Left:
    w.integer(m.entity, 8);
    break;
  case Type::Input:
    if (m.commands.empty() || m.commands.size() > maxBatch)
      throw std::invalid_argument("input batch size");
    w.integer(m.entity, 8);
    w.integer(m.generation, 4);
    w.integer(m.baseline, 8);
    w.integer(m.recovery, 1);
    w.integer(m.commands.size(), 1);
    for (const auto &c : m.commands) {
      if (packetSequence < c.sequence || packetTick < c.tick ||
          packetSequence - c.sequence > 65535 || packetTick - c.tick > 65535)
        throw std::invalid_argument("input offset");
      w.integer(packetSequence - c.sequence, 2);
      w.integer(packetTick - c.tick, 2);
      compactControls(w, c.controls);
    }
    break;
  case Type::Snapshot:
    if (m.aircrafts.size() > maxPlayers)
      throw std::invalid_argument("snapshot size");
    weather(w, m.weather);
    w.integer(m.aircrafts.size(), 2);
    for (const auto &a : m.aircrafts)
      aircraft(w, a);
    break;
  case Type::Fire:
    w.integer(m.entity, 8);
    w.integer(m.fire.sequence, 8);
    w.integer(m.fire.tick, 8);
    w.integer(m.fire.generation, 4);
    w.integer(m.fire.weapon, 1);
    w.integer(m.fire.held, 1);
    break;
  case Type::Combat:
    if (m.events.empty() || m.events.size() > maxCombatEvents)
      throw std::invalid_argument("combat batch size");
    w.integer(m.events.size(), 1);
    for (const auto &e : m.events) {
      w.integer(e.id, 8);
      w.integer(e.tick, 8);
      w.integer(unsigned(e.kind), 1);
      w.integer(e.projectile, 8);
      w.integer(e.owner, 8);
      w.integer(e.target, 8);
      w.integer(e.generation, 4);
      w.integer(unsigned(e.region), 1);
      w.small(e.health);
      w.small(e.lifetime);
      vec(w, e.position);
      vec(w, e.velocity);
    }
    break;
  case Type::SnapshotAck:
    w.integer(m.baseline, 8);
    w.integer(m.recovery, 1);
    break;
  case Type::Ping:
  case Type::Pong:
    break;
  default:
    throw std::invalid_argument("message type");
  }
  if (w.bytes.size() > maxPacket)
    throw std::invalid_argument("packet size");
  if (m.type != Type::Snapshot && !withinApplicationBudget(w.bytes.size()))
    throw std::length_error("message exceeds application payload budget");
  return w.bytes;
}
bool decode(std::span<const std::uint8_t> bytes, Message &output,
            std::string &reason) {
  reason = "malformed packet";
  if (bytes.size() < 24 || bytes.size() > maxPacket)
    return false;
  Reader r{bytes};
  if (r.integer(4) != 0x4f46534e)
    return false;
  if (r.integer(2) != protocolVersion) {
    reason = "protocol version mismatch";
    return false;
  }
  Message m;
  m.type = static_cast<Type>(r.integer(1));
  if (m.type != Type::Snapshot && !withinApplicationBudget(bytes.size()))
    return false;
  if (r.integer(1) != 0)
    return false;
  m.tick = r.integer(8);
  m.sequence = r.integer(8);
  if (m.tick > maxTimelineTick)
    return false;
  switch (m.type) {
  case Type::Hello:
    m.text = r.string();
    m.aircraftType = static_cast<AircraftType>(r.integer(1));
    if (m.text.empty() || !validAircraftType(m.aircraftType))
      r.ok = false;
    break;
  case Type::Reject:
    m.text = r.string();
    if (m.text.empty())
      r.ok = false;
    break;
  case Type::Welcome:
    if (r.integer(2) != 120)
      r.ok = false;
    m.snapshotHz = static_cast<std::uint16_t>(r.integer(2));
    if (m.snapshotHz < 1 || m.snapshotHz > 60)
      r.ok = false;
    m.aircraft = aircraft(r);
    m.weather = weather(r);
    break;
  case Type::Spawn:
    m.aircraft = aircraft(r);
    break;
  case Type::Despawn:
  case Type::Joined:
  case Type::Left:
    m.entity = r.integer(8);
    if (!m.entity)
      r.ok = false;
    break;
  case Type::Input: {
    m.entity = r.integer(8);
    m.generation = r.integer(4);
    m.baseline = r.integer(8);
    const auto recover = r.integer(1);
    if (recover > 1)
      r.ok = false;
    m.recovery = recover;
    auto n = r.integer(1);
    if (!m.entity || n == 0 || n > maxBatch)
      return false;
    for (std::size_t i = 0; i < n; ++i) {
      Command c;
      const auto ds = r.integer(2), dt = r.integer(2);
      if (ds > m.sequence || dt > m.tick)
        r.ok = false;
      c.sequence = ds <= m.sequence ? m.sequence - ds : 0;
      c.tick = dt <= m.tick ? m.tick - dt : 0;
      c.controls = compactControls(r);
      if (!c.sequence || !validControls(c.controls) ||
          (i && (c.sequence <= m.commands.back().sequence ||
                 c.tick <= m.commands.back().tick)))
        r.ok = false;
      m.commands.push_back(c);
    }
    break;
  }
  case Type::Snapshot: {
    m.weather = weather(r);
    auto n = r.integer(2);
    if (n > maxPlayers)
      return false;
    for (std::size_t i = 0; i < n; ++i) {
      auto a = aircraft(r);
      for (const auto &previous : m.aircrafts)
        if (a.id == previous.id)
          r.ok = false;
      m.aircrafts.push_back(a);
    }
    break;
  }
  case Type::Fire: {
    m.entity = r.integer(8);
    m.fire.sequence = r.integer(8);
    m.fire.tick = r.integer(8);
    m.fire.generation = r.integer(4);
    m.fire.weapon = r.integer(1);
    auto held = r.integer(1);
    m.fire.held = held == 1;
    if (!m.entity || !m.fire.sequence || m.fire.weapon != 0 || held > 1 ||
        m.tick != m.fire.tick || m.sequence != m.fire.sequence)
      r.ok = false;
    break;
  }
  case Type::Combat: {
    auto n = r.integer(1);
    if (!n || n > maxCombatEvents)
      return false;
    for (std::size_t i = 0; i < n; ++i) {
      CombatEvent e;
      e.id = r.integer(8);
      e.tick = r.integer(8);
      e.kind = static_cast<CombatKind>(r.integer(1));
      e.projectile = r.integer(8);
      e.owner = r.integer(8);
      e.target = r.integer(8);
      e.generation = r.integer(4);
      e.region = static_cast<HitRegion>(r.integer(1));
      e.health = r.small();
      e.lifetime = r.small();
      e.position = vec(r);
      e.velocity = vec(r);
      if (!e.id || !e.owner || e.tick > m.tick || unsigned(e.kind) < 1 ||
          unsigned(e.kind) > 4 || unsigned(e.region) > 3 ||
          !std::isfinite(e.health) || e.health < 0 || e.health > 100 ||
          !std::isfinite(e.lifetime) || e.lifetime < 0 || e.lifetime > 10 ||
          !std::isfinite(e.position.norm2()) ||
          !std::isfinite(e.velocity.norm2()) || e.velocity.norm() > 10000 ||
          (e.kind == CombatKind::Shot && (!e.projectile || e.lifetime <= 0)) ||
          (e.kind != CombatKind::Shot && !e.target) ||
          (i && e.id <= m.events.back().id))
        r.ok = false;
      m.events.push_back(e);
    }
    break;
  }
  case Type::SnapshotAck: {
    m.baseline = r.integer(8);
    const auto recover = r.integer(1);
    if (recover > 1)
      r.ok = false;
    m.recovery = recover;
    break;
  }
  case Type::Ping:
  case Type::Pong:
    break;
  default:
    return false;
  }
  if (!r.ok || r.pos != bytes.size())
    return false;
  output = std::move(m);
  reason.clear();
  return true;
}
} // namespace ofs::net
