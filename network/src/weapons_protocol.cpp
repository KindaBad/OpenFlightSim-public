#include "ofs/net/weapons_protocol.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace ofs::net {
namespace {
struct Writer {
  std::vector<std::uint8_t> b;
  void u(std::uint64_t n, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i)
      b.push_back(std::uint8_t(n >> ((bytes - i - 1) * 8)));
  }
  void q(double n, double scale, unsigned bytes) {
    const double bound = std::ldexp(1., bytes * 8 - 1) - 1;
    if (!std::isfinite(n) || std::abs(n * scale) > bound)
      throw std::invalid_argument("weapons quantization bounds");
    u(std::uint64_t(std::int64_t(std::llround(n * scale))), bytes);
  }
  void vec(Vec3 v, double scale, unsigned bytes) {
    q(v.x, scale, bytes);
    q(v.y, scale, bytes);
    q(v.z, scale, bytes);
  }
  void ref(EntityRef e) {
    u(e.id, 8);
    u(e.generation, 4);
  }
};
struct Reader {
  std::span<const std::uint8_t> b;
  std::size_t p{};
  bool ok{true};
  std::uint64_t u(unsigned bytes) {
    if (bytes > b.size() - p) {
      ok = false;
      return 0;
    }
    std::uint64_t n = 0;
    for (unsigned i = 0; i < bytes; ++i)
      n = (n << 8) | b[p++];
    return n;
  }
  double q(double scale, unsigned bytes) {
    const auto n = u(bytes);
    const auto sign = std::uint64_t{1} << (bytes * 8 - 1);
    if (n == sign)
      ok = false;
    const std::int64_t v =
        n & sign
            ? std::int64_t(n) - std::int64_t(std::uint64_t{1} << (bytes * 8))
            : std::int64_t(n);
    return double(v) / scale;
  }
  Vec3 vec(double scale, unsigned bytes) {
    const double x = q(scale, bytes), y = q(scale, bytes), z = q(scale, bytes);
    return {x, y, z};
  }
  EntityRef ref() {
    const auto id = u(8);
    const auto generation = u(4);
    if (!id && generation)
      ok = false;
    return {id, std::uint32_t(generation)};
  }
};
void missile(Writer &w, const MissileNetState &m) {
  w.u(m.id, 8);
  w.ref(m.owner);
  w.ref(m.target);
  w.u(unsigned(m.type), 1);
  w.vec(m.position, 8, 4);
  w.vec(m.velocity, 4, 2);
  for (double v : {m.attitude.w, m.attitude.x, m.attitude.y, m.attitude.z})
    w.q(v, 32767, 2);
  w.u(unsigned(m.motor), 1);
  w.u(unsigned(m.seeker), 1);
  w.u(std::uint16_t(clamp(m.age * 100, 0, 7500)), 2);
}
MissileNetState missile(Reader &r) {
  MissileNetState m;
  m.id = r.u(8);
  m.owner = r.ref();
  m.target = r.ref();
  m.type = WeaponType(r.u(1));
  m.position = r.vec(8, 4);
  m.velocity = r.vec(4, 2);
  const double w = r.q(32767, 2), x = r.q(32767, 2), y = r.q(32767, 2),
               z = r.q(32767, 2);
  const double norm = w * w + x * x + y * y + z * z;
  if (norm < .99 || norm > 1.01)
    r.ok = false;
  m.attitude = Quat{w, x, y, z}.normalized();
  m.motor = weapons::MotorPhase(r.u(1));
  m.seeker = weapons::SeekerPhase(r.u(1));
  m.age = double(r.u(2)) / 100;
  if (!m.id || !m.owner.id || unsigned(m.type) < 1 || unsigned(m.type) > 2 ||
      unsigned(m.motor) > 3 ||
      unsigned(m.seeker) > unsigned(weapons::SeekerPhase::Decoyed) ||
      m.age > 75)
    r.ok = false;
  return m;
}
} // namespace
std::vector<std::uint8_t> encodeWeapon(const WeaponMessage &m) {
  Writer w;
  w.u(0x4f46534e, 4);
  w.u(protocolVersion, 2);
  w.u(unsigned(m.type), 1);
  w.u(0, 1);
  w.u(m.tick, 8);
  w.u(m.sequence, 8);
  switch (m.type) {
  case Type::WeaponAction:
    w.u(m.entity, 8);
    w.u(m.action.generation, 4);
    w.u(unsigned(m.action.kind), 1);
    w.u(m.action.station, 1);
    break;
  case Type::RadarState: {
    const auto &n = m.radar;
    if (n.tracks.size() > 16 || n.stations.size() > 8 ||
        n.loadouts.size() > maxLoadouts)
      throw std::length_error("radar bounds");
    w.u(n.generation, 4);
    w.u(unsigned(n.mode), 1);
    w.ref(n.selected);
    w.ref(n.locked);
    w.u(unsigned(n.weapon), 1);
    w.u(n.seekerReady, 1);
    w.ref(n.seekerTarget);
    w.u(std::uint8_t(clamp(n.lockProgress * 255, 0, 255)), 1);
    w.u(n.stations.size(), 1);
    for (auto type : n.stations)
      w.u(unsigned(type), 1);
    for (double value : {n.envelope.minimum, n.envelope.kinematicRange,
                         n.envelope.targetRange})
      w.u(std::uint32_t(clamp(value, 0, 1000000)), 4);
    w.q(n.envelope.closure, 1, 2);
    w.u(n.envelope.inside, 1);
    w.u(n.tracks.size(), 1);
    for (const auto &t : n.tracks) {
      w.ref(t.entity);
      w.vec(t.position, 2, 4);
      w.vec(t.velocity, 2, 2);
      w.u(std::uint8_t(clamp(t.quality * 255, 0, 255)), 1);
      w.u(std::uint8_t(clamp(
              (double(m.tick) * tickSeconds - t.lastDetection) * 50, 0, 255)),
          1);
    }
    w.u(n.loadouts.size(), 1);
    for (const auto &loadout : n.loadouts) {
      w.ref(loadout.entity);
      w.u(loadout.mounted, 1);
    }
    w.u(n.flares, 1);
    w.u(n.chaff, 1);
    break;
  }
  case Type::MissileSpawn:
  case Type::MissileState:
    if (m.missiles.empty() || m.missiles.size() > maxMissileRecords)
      throw std::length_error("missile batch");
    w.u(m.missiles.size(), 1);
    for (const auto &n : m.missiles)
      missile(w, n);
    break;
  case Type::MissileRemove:
    if (m.removals.empty() || m.removals.size() > maxMissileRecords)
      throw std::length_error("missile remove batch");
    w.u(m.removals.size(), 1);
    for (const auto &e : m.removals) {
      w.u(e.missile.id, 8);
      w.u(e.detonation, 1);
      w.vec(e.missile.position, 8, 4);
    }
    break;
  default:
    throw std::invalid_argument("weapon message type");
  }
  if (!withinApplicationBudget(w.b.size()))
    throw std::length_error("weapon packet budget");
  return w.b;
}
bool decodeWeapon(std::span<const std::uint8_t> bytes, WeaponMessage &out) {
  if (bytes.size() < 24 || !withinApplicationBudget(bytes.size()))
    return false;
  Reader r{bytes};
  if (r.u(4) != 0x4f46534e || r.u(2) != protocolVersion)
    return false;
  WeaponMessage m;
  m.type = Type(r.u(1));
  if (r.u(1))
    return false;
  m.tick = r.u(8);
  m.sequence = r.u(8);
  if (m.tick > maxTimelineTick || !m.sequence)
    return false;
  switch (m.type) {
  case Type::WeaponAction:
    m.entity = r.u(8);
    m.action = {m.sequence, m.tick, std::uint32_t(r.u(4)),
                WeaponActionKind(r.u(1)), std::uint8_t(r.u(1))};
    if (!m.entity ||
        unsigned(m.action.kind) > unsigned(WeaponActionKind::Chaff) ||
        m.action.station > 7)
      r.ok = false;
    break;
  case Type::RadarState: {
    auto &n = m.radar;
    n.generation = r.u(4);
    n.mode = weapons::RadarMode(r.u(1));
    n.selected = r.ref();
    n.locked = r.ref();
    n.weapon = WeaponType(r.u(1));
    const auto ready = r.u(1);
    n.seekerReady = ready;
    n.seekerTarget = r.ref();
    n.lockProgress = double(r.u(1)) / 255;
    const auto stations = r.u(1);
    if (stations > 8)
      return false;
    for (unsigned i = 0; i < stations; ++i) {
      const auto type = WeaponType(r.u(1));
      if (unsigned(type) > 2)
        r.ok = false;
      n.stations.push_back(type);
    }
    n.envelope.minimum = r.u(4);
    n.envelope.kinematicRange = r.u(4);
    n.envelope.targetRange = r.u(4);
    n.envelope.closure = r.q(1, 2);
    const auto inside = r.u(1);
    n.envelope.inside = inside;
    if (unsigned(n.mode) > 2 || unsigned(n.weapon) > 2 || ready > 1 ||
        inside > 1 || n.envelope.kinematicRange > 1000000 ||
        n.envelope.targetRange > 1000000 || n.envelope.minimum > 1000000)
      r.ok = false;
    const auto count = r.u(1);
    if (count > 16)
      return false;
    for (unsigned i = 0; i < count; ++i) {
      weapons::Track t;
      t.entity = r.ref();
      t.position = r.vec(2, 4);
      t.velocity = r.vec(2, 2);
      t.quality = double(r.u(1)) / 255;
      t.lastDetection = double(m.tick) * tickSeconds - double(r.u(1)) / 50;
      t.age = double(m.tick) * tickSeconds;
      if (!t.entity.id ||
          std::any_of(n.tracks.begin(), n.tracks.end(), [&](const auto &other) {
            return other.entity == t.entity;
          }))
        r.ok = false;
      n.tracks.push_back(t);
    }
    const auto loadouts = r.u(1);
    if (loadouts > maxLoadouts)
      return false;
    for (unsigned i = 0; i < loadouts; ++i) {
      Loadout loadout;
      loadout.entity = r.ref();
      loadout.mounted = std::uint8_t(r.u(1));
      if (!loadout.entity.id ||
          std::any_of(n.loadouts.begin(), n.loadouts.end(),
                      [&](const auto &other) {
                        return other.entity.id == loadout.entity.id;
                      }))
        r.ok = false;
      n.loadouts.push_back(loadout);
    }
    n.flares = std::uint8_t(r.u(1));
    n.chaff = std::uint8_t(r.u(1));
    break;
  }
  case Type::MissileSpawn:
  case Type::MissileState: {
    const auto count = r.u(1);
    if (!count || count > maxMissileRecords)
      return false;
    for (unsigned i = 0; i < count; ++i) {
      auto n = missile(r);
      if (std::any_of(m.missiles.begin(), m.missiles.end(),
                      [&](const auto &other) { return n.id == other.id; }))
        r.ok = false;
      m.missiles.push_back(n);
    }
    break;
  }
  case Type::MissileRemove: {
    const auto count = r.u(1);
    if (!count || count > maxMissileRecords)
      return false;
    for (unsigned i = 0; i < count; ++i) {
      MissileEvent e;
      e.tick = m.tick;
      e.missile.id = r.u(8);
      const auto detonate = r.u(1);
      e.detonation = detonate;
      e.missile.position = r.vec(8, 4);
      if (!e.missile.id || detonate > 1 ||
          std::any_of(m.removals.begin(), m.removals.end(),
                      [&](const auto &other) {
                        return e.missile.id == other.missile.id;
                      }))
        r.ok = false;
      m.removals.push_back(e);
    }
    break;
  }
  default:
    return false;
  }
  if (!r.ok || r.p != bytes.size())
    return false;
  out = std::move(m);
  return true;
}
std::vector<WeaponReplicationSender::Packet> WeaponReplicationSender::build(
    Tick tick, EntityId viewer, Vec3 position, const RadarNetState &radar,
    std::span<const Missile> missiles, std::span<const MissileEvent> events) {
  std::vector<Packet> result;
  auto send = [&](WeaponMessage &m, bool reliable) {
    m.tick = tick;
    m.sequence = ++sequence_;
    result.push_back({encodeWeapon(m), reliable});
  };
  if (tick % 12 == 0 && !radar.stations.empty()) {
    WeaponMessage m;
    m.type = Type::RadarState;
    m.radar = radar;
    send(m, false);
  }
  std::map<std::uint64_t, MissileNetState> current;
  for (const auto &m : missiles)
    if (missileInterest(m, viewer, position))
      current.emplace(m.id, projectMissile(m, viewer));
  WeaponMessage removed;
  removed.type = Type::MissileRemove;
  for (const auto &[id, n] : known_)
    if (!current.contains(id)) {
      auto e = std::find_if(events.begin(), events.end(),
                            [&](const auto &e) { return e.missile.id == id; });
      removed.removals.push_back(
          e == events.end() ? MissileEvent{n, tick, false} : *e);
      if (removed.removals.size() == maxMissileRecords) {
        send(removed, true);
        removed.removals.clear();
      }
    }
  if (!removed.removals.empty())
    send(removed, true);
  WeaponMessage spawned, states;
  spawned.type = Type::MissileSpawn;
  states.type = Type::MissileState;
  for (const auto &[id, n] : current) {
    if (!known_.contains(id)) {
      spawned.missiles.push_back(n);
      if (spawned.missiles.size() == maxMissileRecords) {
        send(spawned, true);
        spawned.missiles.clear();
      }
    } else if (tick % 6 == 0) {
      states.missiles.push_back(n);
      if (states.missiles.size() == maxMissileRecords) {
        send(states, false);
        states.missiles.clear();
      }
    }
  }
  if (!spawned.missiles.empty())
    send(spawned, true);
  if (!states.missiles.empty())
    send(states, false);
  known_ = std::move(current);
  return result;
}
bool WeaponReplicationReceiver::receive(const WeaponMessage &m, Tick now) {
  if (m.tick > now + 120 || (m.tick < now && now - m.tick > 240))
    return false;
  if (m.type == Type::RadarState) {
    if (m.tick < radarTick_ || m.radar.generation < radar.generation)
      return true;
    radar = m.radar;
    radarTick_ = m.tick;
    return true;
  }
  if (m.type == Type::MissileSpawn || m.type == Type::MissileState) {
    for (const auto &n : m.missiles) {
      if (retired_.contains(n.id) && retired_.at(n.id) >= m.tick)
        continue;
      if (sampled_.contains(n.id) && sampled_.at(n.id) >= m.tick)
        continue;
      if (missiles_.size() >= MissileCombat::capacity &&
          !missiles_.contains(n.id))
        continue;
      // A state can recover a watchdog-expired visualization. A reliable
      // retirement still rejects pre-retirement state and reordered launches.
      missiles_[n.id] = n;
      sampled_[n.id] = m.tick;
      auto &history = history_[n.id];
      history.emplace_back(m.tick, n);
      while (history.size() > 4)
        history.pop_front();
    }
    return true;
  }
  if (m.type == Type::MissileRemove) {
    for (const auto &e : m.removals) {
      if (retired_.contains(e.missile.id) &&
          retired_.at(e.missile.id) >= m.tick)
        continue;
      retired_[e.missile.id] = m.tick;
      if (!sampled_.contains(e.missile.id) ||
          sampled_.at(e.missile.id) <= m.tick) {
        missiles_.erase(e.missile.id);
        sampled_.erase(e.missile.id);
        history_.erase(e.missile.id);
      }
      if (terminations_.size() < MissileCombat::capacity)
        terminations_.push_back(e);
      if (e.detonation && detonations_.size() < MissileCombat::capacity)
        detonations_.push_back(e);
    }
    while (retired_.size() > MissileCombat::capacity * 2) {
      const auto oldest = std::min_element(
          retired_.begin(), retired_.end(),
          [](const auto &a, const auto &b) { return a.second < b.second; });
      retired_.erase(oldest);
    }
    return true;
  }
  return false;
}
void WeaponReplicationReceiver::expire(Tick now) {
  for (auto it = sampled_.begin(); it != sampled_.end();)
    if (now > it->second + 240) {
      missiles_.erase(it->first);
      history_.erase(it->first);
      it = sampled_.erase(it);
    } else
      ++it;
  if (now > radarTick_ + 240) {
    radar.tracks.clear();
    radar.selected = radar.locked = {};
    radar.seekerReady = false;
  }
}
void WeaponReplicationReceiver::reset() {
  radar = {};
  radarTick_ = 0;
  missiles_.clear();
  sampled_.clear();
  retired_.clear();
  detonations_.clear();
  terminations_.clear();
  history_.clear();
}
std::vector<MissileEvent> WeaponReplicationReceiver::takeDetonations() {
  auto e = std::move(detonations_);
  detonations_.clear();
  return e;
}
std::vector<MissileEvent> WeaponReplicationReceiver::takeMissileTerminations() {
  auto e = std::move(terminations_);
  terminations_.clear();
  return e;
}
} // namespace ofs::net

namespace ofs::net {
std::vector<MissileNetState>
WeaponReplicationReceiver::sample(double tick,
                                  std::vector<double> *sampled) const {
  std::vector<MissileNetState> states;
  states.reserve(history_.size());
  if (sampled)
    sampled->clear();
  for (const auto &[id, history] : history_) {
    (void)id;
    if (history.empty())
      continue;
    auto result = history.front().second;
    // The moment the returned state belongs to, in server ticks.
    double moment = std::max(tick, double(history.front().first));
    if (tick >= double(history.back().first)) {
      result = history.back().second;
      const double ahead = std::clamp(
          (tick - double(history.back().first)) * tickSeconds, 0., .05);
      result.position += result.velocity * ahead;
      result.age += ahead;
      moment = double(history.back().first) + ahead / tickSeconds;
    } else
      for (unsigned i = 1; i < history.size(); ++i)
        if (tick <= double(history[i].first)) {
          const auto &a = history[i - 1];
          const auto &b = history[i];
          const double f = std::clamp(
              (tick - double(a.first)) / double(b.first - a.first), 0., 1.);
          result = a.second;
          result.position =
              a.second.position + (b.second.position - a.second.position) * f;
          result.velocity =
              a.second.velocity + (b.second.velocity - a.second.velocity) * f;
          const auto &q = a.second.attitude;
          auto r = b.second.attitude;
          if (q.w * r.w + q.x * r.x + q.y * r.y + q.z * r.z < 0)
            r = {-r.w, -r.x, -r.y, -r.z};
          result.attitude = Quat{lerp(q.w, r.w, f), lerp(q.x, r.x, f),
                                 lerp(q.y, r.y, f), lerp(q.z, r.z, f)}
                                .normalized();
          result.age = lerp(a.second.age, b.second.age, f);
          if (f >= 1)
            result = b.second;
          break;
        }
    states.push_back(result);
    if (sampled)
      sampled->push_back(moment);
  }
  return states;
}
} // namespace ofs::net
