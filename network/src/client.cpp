#include "ofs/net/client.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace ofs::net {
namespace {
Quat blend(Quat a, Quat b, double t) {
  if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0)
    b = {-b.w, -b.x, -b.y, -b.z};
  return Quat{lerp(a.w, b.w, t), lerp(a.x, b.x, t), lerp(a.y, b.y, t),
              lerp(a.z, b.z, t)}
      .normalized();
}
State between(const State &a, const State &b, double t) {
  State s = t < 1 ? a : b;
  s.pos_ned = a.pos_ned * (1 - t) + b.pos_ned * t;
  s.vel_ned = a.vel_ned * (1 - t) + b.vel_ned * t;
  s.att = blend(a.att, b.att, t);
  s.omega_body = a.omega_body * (1 - t) + b.omega_body * t;
  s.time = lerp(a.time, b.time, t);
  for (unsigned e = 0; e < 2; ++e) {
    s.n1[e] = lerp(a.n1[e], b.n1[e], t);
    s.afterburner[e] = lerp(a.afterburner[e], b.afterburner[e], t);
    s.inlet_spike[e] = lerp(a.inlet_spike[e], b.inlet_spike[e], t);
    s.nozzle_angle[e] = lerp(a.nozzle_angle[e], b.nozzle_angle[e], t);
    if (a.aero_memory_initialized && b.aero_memory_initialized) {
      s.alpha_lag[e] = std::remainder(
          a.alpha_lag[e] +
              t * std::remainder(b.alpha_lag[e] - a.alpha_lag[e], 2 * kPi),
          2 * kPi);
      s.separation[e] = lerp(a.separation[e], b.separation[e], t);
      s.vortex_state[e] = lerp(a.vortex_state[e], b.vortex_state[e], t);
    }
  }
  for (auto pair : {std::pair{&s.elevator, std::pair{a.elevator, b.elevator}},
                    {&s.aileron, {a.aileron, b.aileron}},
                    {&s.rudder, {a.rudder, b.rudder}},
                    {&s.flap, {a.flap, b.flap}},
                    {&s.spoiler, {a.spoiler, b.spoiler}},
                    {&s.canard, {a.canard, b.canard}},
                    {&s.elevon_l, {a.elevon_l, b.elevon_l}},
                    {&s.elevon_r, {a.elevon_r, b.elevon_r}}})
    *pair.first = lerp(pair.second.first, pair.second.second, t);
  return s;
}
} // namespace
void Prediction::initialize(Tick tick, const Aircraft &a, unsigned lead,
                            Weather weather) {
  type_ = a.type;
  sim_ = Simulator(aircraftDefinition(type_).flight);
  sim_.setWeather(weather);
  generation_ = a.life.generation;
  alive_ = a.life.alive();
  sim_.setState(a.state);
  sim_.setControls(a.controls);
  tick_ = tick;
  sequence_ = a.acknowledged;
  pending_.clear();
  history_.clear();
  stats_ = {};
  history_[tick_] = sim_.state();
  for (unsigned i = 0; i < (alive_ ? std::min(lead, 60u) : 0u); ++i) {
    sim_.step(tickSeconds);
    history_[++tick_] = sim_.state();
  }
}
Command Prediction::advance(const Controls &controls) {
  if (!validControls(controls))
    throw std::invalid_argument("invalid local controls");
  Command c{++sequence_, ++tick_, quantizeControls(controls)};
  pending_.push_back(c);
  sim_.setControls(c.controls);
  sim_.step(tickSeconds);
  history_[tick_] = sim_.state();
  while (pending_.size() > 512)
    pending_.pop_front();
  while (history_.size() > 512)
    history_.erase(history_.begin());
  stats_.maxPending = std::max(stats_.maxPending, pending_.size());
  return c;
}
void Prediction::reconcile(Tick tick, const Aircraft &a) {
  if (a.type != type_ || a.life.generation != generation_ ||
      a.life.alive() != alive_ || !a.life.alive()) {
    initialize(tick, a, a.life.alive() ? 18 : 0, sim_.weather());
    return;
  }
  auto h = history_.find(tick);
  stats_.error =
      h == history_.end() ? 0 : (h->second.pos_ned - a.state.pos_ned).norm();
  stats_.maxError = std::max(stats_.maxError, stats_.error);
  bool meaningful = h == history_.end() || stats_.error > .02;
  if (h != history_.end())
    meaningful = meaningful ||
                 (h->second.vel_ned - a.state.vel_ned).norm() > .01 ||
                 std::abs(h->second.att.w * a.state.att.w +
                          h->second.att.x * a.state.att.x +
                          h->second.att.y * a.state.att.y +
                          h->second.att.z * a.state.att.z) < .999999;
  if (h != history_.end()) {
    stats_.velocityCorrection = (h->second.vel_ned - a.state.vel_ned).norm();
    const auto &q = h->second.att;
    const auto &r = a.state.att;
    stats_.orientationCorrection =
        2 *
        std::acos(std::clamp(
            std::abs(q.w * r.w + q.x * r.x + q.y * r.y + q.z * r.z), 0., 1.));
    stats_.positionErrors.push_back(stats_.error);
    stats_.velocityErrors.push_back(stats_.velocityCorrection);
    stats_.orientationErrors.push_back(stats_.orientationCorrection);
    for (auto *errors : {&stats_.positionErrors, &stats_.velocityErrors,
                         &stats_.orientationErrors})
      while (errors->size() > 4096)
        errors->pop_front();
  }
  const State before = sim_.state();
  const Tick beforeTick = tick_;
  while (!pending_.empty() && (pending_.front().tick <= tick ||
                               pending_.front().sequence <= a.acknowledged))
    pending_.pop_front();
  Tick target = std::max(tick_, tick);
  if (target - tick > 512) {
    target = tick;
    pending_.clear();
    ++stats_.rebases;
  }
  sim_.setState(a.state);
  sim_.setControls(a.controls);
  history_.clear();
  history_[tick] = sim_.state();
  auto input = pending_.begin();
  for (Tick t = tick + 1; t <= target; ++t) {
    while (input != pending_.end() && input->tick <= t) {
      sim_.setControls(input->controls);
      ++input;
    }
    sim_.step(tickSeconds);
    ++stats_.replayCount;
    history_[t] = sim_.state();
  }
  tick_ = target;
  const double correction = (before.pos_ned - sim_.state().pos_ned).norm();
  // Advancing to a newer authoritative tick is elapsed motion, not divergence.
  const bool sameTime = target == beforeTick;
  if (sameTime)
    stats_.maxError = std::max(stats_.maxError, correction);
  if (meaningful ||
      (sameTime && (correction > .02 ||
                    (before.vel_ned - sim_.state().vel_ned).norm() > .01)))
    ++stats_.reconciliations;
}
void RemoteTrack::push(Tick tick, const Aircraft &a) {
  if (!history_.empty() &&
      a.life.generation != history_.back().aircraft.life.generation) {
    if (a.life.generation < history_.back().aircraft.life.generation)
      return;
    history_.clear();
  }
  if (!history_.empty() && tick <= history_.back().tick)
    return;
  history_.push_back({tick, a});
  while (history_.size() > 32)
    history_.pop_front();
}
State RemoteTrack::sample(double tick) const {
  return sampleAircraft(tick).state;
}
Aircraft RemoteTrack::sampleAircraft(double tick) const {
  const double target = interpolationDelay() - 12;
  const double elapsed = std::clamp(tick - lastSampleTick_, 0., 12.);
  lastSampleTick_ = tick;
  // Resolve one effective presentation tick for physics AND visual metadata.
  extraDelay_ +=
      std::clamp(target - extraDelay_, -elapsed * .25, elapsed * .75);
  tick -= extraDelay_;
  if (history_.empty())
    return {};
  if (tick <= double(history_.front().tick))
    return history_.front().aircraft;
  for (std::size_t i = 1; i < history_.size(); ++i)
    if (tick <= double(history_[i].tick)) {
      const auto &a = history_[i - 1];
      const auto &b = history_[i];
      const double t = (tick - double(a.tick)) / double(b.tick - a.tick);
      // Discrete life/type/flags use the preceding sample; at the endpoint
      // they switch with the matching physical sample. Actuators interpolate.
      Aircraft result = t < 1 ? a.aircraft : b.aircraft;
      result.state = between(a.aircraft.state, b.aircraft.state, t);
      auto &c = result.controls;
      const auto &ac = a.aircraft.controls;
      const auto &bc = b.aircraft.controls;
      c.gear01 = lerp(ac.gear01, bc.gear01, t);
      c.flap01 = lerp(ac.flap01, bc.flap01, t);
      c.spoiler01 = lerp(ac.spoiler01, bc.spoiler01, t);
      c.steering = lerp(ac.steering, bc.steering, t);
      for (unsigned e = 0; e < 2; ++e)
        c.throttle[e] = lerp(ac.throttle[e], bc.throttle[e], t);
      return result;
    }
  Aircraft result = history_.back().aircraft;
  auto &s = result.state;
  const double dt =
      std::clamp((tick - double(history_.back().tick)) * tickSeconds, 0.0, .05);
  s.pos_ned += s.vel_ned * dt;
  const double rate = s.omega_body.norm();
  if (rate > 1e-10) {
    double angle = rate * dt * .5;
    auto axis = s.omega_body / rate;
    s.att = (s.att * Quat{std::cos(angle), axis.x * std::sin(angle),
                          axis.y * std::sin(angle), axis.z * std::sin(angle)})
                .normalized();
  }
  s.time += dt;
  return result;
}

Client::Client(std::string name, AircraftType type)
    : name_(std::move(name)), type_(type) {
  if (!validAircraftType(type_))
    throw std::invalid_argument("invalid client aircraft type");
  if (name_.empty() || name_.size() > 64 ||
      std::any_of(name_.begin(), name_.end(),
                  [](unsigned char c) { return c < 32 || c > 126; }))
    throw std::invalid_argument("name must be 1..64 printable ASCII bytes");
}
Client::~Client() { disconnect(); }
void Client::connect(const std::string &address, std::uint16_t port) {
  disconnect();
  stats_ = {};
  replication_ = ReplicationReceiver{};
  remotes_.clear();
  tombstones_.clear();
  lastSnapshotSequence_ = lastSnapshots_ = lastInputs_ = 0;
  serverTime_ = renderTime_ = pingTimer_ = metricsTime_ = 0;
  sendDivider_ = fireDivider_ = 0;
  lastRecoveryTick_ = 0;
  recoverySent_ = false;
  fireSequence_ = lastCombatSequence_ = lastEvent_ = 0;
  weaponSequence_ = 0;
  weapons_.reset();
  firing_ = false;
  life_ = {};
  latestHit_ = {};
  visualRounds_.clear();
  visualEffects_.clear();
  pendingVisualEvents_.clear();
  pendingVisualEvents_.reserve(256);
  visualRounds_.reserve(Combat::capacity);
  visualEffects_.reserve(256);
  correction_ = {};
  attitudeCorrection_ = {};
  connection_ = transport_.connect(address, port);
  status_ = "connecting";
}
void Client::disconnect() {
  if (connection_)
    transport_.close(connection_, "client leaving");
  connection_ = 0;
  entity_ = 0;
  remotes_.clear();
  weapons_.reset();
  weaponSequence_ = 0;
  replication_ = ReplicationReceiver{};
  tombstones_.clear();
  stats_.networkMemoryBytes = 0;
  stats_.historySamples = 0;
  visualRounds_.clear();
  visualEffects_.clear();
  status_ = "offline";
}
void Client::send(const Message &m, bool reliable) {
  auto b = encode(m);
  if (transport_.send(connection_, b, reliable)) {
    ++stats_.sent;
    stats_.bytesOut += b.size();
    if (m.type == Type::Input)
      ++stats_.inputMessages;
  } else
    ++stats_.sendFailures;
}
bool Client::sendTestPacket(std::span<const std::uint8_t> b) {
  return transport_.send(connection_, b, true);
}
void Client::requestRecovery() {
  const Tick now =
      static_cast<Tick>(std::clamp(serverTime_, 0., double(maxTimelineTick)));
  if (recoverySent_ && now < lastRecoveryTick_ + 12)
    return;
  Message ack;
  ack.type = Type::SnapshotAck;
  ack.baseline = replication_.acknowledged();
  ack.recovery = true;
  send(ack, false);
  lastRecoveryTick_ = now;
  recoverySent_ = true;
}
void Client::poll(double elapsed) {
  if (!std::isfinite(elapsed))
    throw std::invalid_argument("Nonfinite client elapsed time");
  const double dt = std::clamp(elapsed, 0.0, .25);
  if (ready()) {
    // Smoothing remains bounded, but presentation clocks must retain all real
    // elapsed time. Discarding a long frame permanently lags the render clock
    // behind the bounded snapshot history and makes eviction look like a jump.
    const double clockElapsed = std::max(0., elapsed);
    serverTime_ =
        std::min(serverTime_ + clockElapsed * 120, double(maxTimelineTick));
    renderTime_ =
        std::min(renderTime_ + clockElapsed * 120, double(maxTimelineTick));
    correction_ = correction_ * std::exp(-dt / .12);
    attitudeCorrection_ = blend({}, attitudeCorrection_, std::exp(-dt / .12));
  }
  if (ready() && replication_.expire(static_cast<Tick>(
                     std::clamp(serverTime_, 0., double(maxTimelineTick))))) {
    stats_.replication = replication_.stats();
    stats_.networkMemoryBytes = replication_.memoryBytes();
    requestRecovery();
  }
  weapons_.expire(static_cast<Tick>(std::max(0., serverTime_)));
  for (const auto &e : transport_.poll()) {
    if (e.connection != connection_)
      continue;
    if (e.kind == TransportEvent::Kind::Connected) {
      status_ = "handshaking";
      Message hello;
      hello.type = Type::Hello;
      hello.text = name_;
      hello.aircraftType = type_;
      send(hello, true);
      continue;
    }
    if (e.kind == TransportEvent::Kind::Disconnected) {
      status_ = e.reason.empty() ? "disconnected" : e.reason;
      connection_ = 0;
      entity_ = 0;
      remotes_.clear();
      weapons_.reset();
      replication_ = ReplicationReceiver{};
      tombstones_.clear();
      stats_.networkMemoryBytes = 0;
      stats_.historySamples = 0;
      continue;
    }
    ++stats_.received;
    stats_.bytesIn += e.bytes.size();
    if (e.bytes.size() >= 7 && e.bytes[6] >= unsigned(Type::RadarState) &&
        e.bytes[6] <= unsigned(Type::MissileRemove)) {
      WeaponMessage message;
      if (!decodeWeapon(e.bytes, message) ||
          !weapons_.receive(message,
                            static_cast<Tick>(std::max(0., serverTime_))))
        ++stats_.invalid;
      continue;
    }
    Message m;
    std::string reason;
    if (e.bytes.size() >= 7 && e.bytes[6] == unsigned(Type::SnapshotChunk)) {
      auto frame =
          replication_.receive(e.bytes,
                               static_cast<Tick>(std::clamp(
                                   serverTime_, 0., double(maxTimelineTick))),
                               entity_);
      stats_.replication = replication_.stats();
      stats_.networkMemoryBytes = replication_.memoryBytes();
      if (!frame) {
        if (replication_.needsRecovery())
          requestRecovery();
        continue;
      }
      m.type = Type::Snapshot;
      m.sequence = frame->sequence;
      m.tick = frame->tick;
      m.weather = frame->weather;
      for (const auto &[id, state] : frame->entities) {
        Aircraft aircraft;
        if (!expandAircraft(id, state, frame->reference, aircraft)) {
          ++stats_.invalid;
          continue;
        }
        m.aircrafts.push_back(aircraft);
        if (id != entity_)
          remotes_[id].setTier(state.tier);
      }
      Message ack;
      ack.type = Type::SnapshotAck;
      ack.baseline = replication_.acknowledged();
      send(ack, false);
    } else if (!decode(e.bytes, m, reason) || m.type == Type::Snapshot) {
      ++stats_.invalid;
      continue;
    }
    if (m.type == Type::Reject) {
      status_ = m.text;
      transport_.close(connection_, m.text);
      connection_ = 0;
      entity_ = 0;
      remotes_.clear();
      weapons_.reset();
      replication_ = ReplicationReceiver{};
      tombstones_.clear();
      stats_.networkMemoryBytes = 0;
      stats_.historySamples = 0;
      continue;
    }
    if (m.type == Type::Welcome && !ready()) {
      entity_ = m.aircraft.id;
      replication_.spawn(m.aircraft, m.tick);
      life_ = m.aircraft.life;
      type_ = m.aircraft.type;
      prediction_.initialize(m.tick, m.aircraft, 18, m.weather);
      serverTime_ = double(m.tick);
      renderTime_ = serverTime_ - 12;
      stats_.serverTick = m.tick;
      status_ = "connected";
      continue;
    }
    if (!ready())
      continue;
    if (m.type == Type::Spawn) {
      if (!replication_.spawn(m.aircraft, m.tick)) {
        ++stats_.invalid;
        requestRecovery();
        continue;
      }
      if (m.aircraft.id != entity_) {
        tombstones_.erase(m.aircraft.id);
        remotes_[m.aircraft.id].push(m.tick, m.aircraft);
      } else {
        const bool reset = m.aircraft.life.generation != life_.generation ||
                           m.aircraft.life.alive() != life_.alive();
        life_ = m.aircraft.life;
        prediction_.reconcile(m.tick, m.aircraft);
        if (reset) {
          ++stats_.predictionResets;
          correction_ = {};
          attitudeCorrection_ = {};
        }
      }
      continue;
    }
    if (m.type == Type::Despawn) {
      replication_.despawn(m.entity, m.tick);
      remotes_.erase(m.entity);
      tombstones_[m.entity] = m.tick;
      while (tombstones_.size() > maxPlayers * 2)
        tombstones_.erase(tombstones_.begin());
      continue;
    }
    if (m.type == Type::Joined) {
      ++stats_.joined;
      continue;
    }
    if (m.type == Type::Left) {
      ++stats_.left;
      replication_.despawn(m.entity, m.tick);
      remotes_.erase(m.entity);
      tombstones_[m.entity] = m.tick;
      while (tombstones_.size() > maxPlayers * 2)
        tombstones_.erase(tombstones_.begin());
      continue;
    }
    if (m.type == Type::Combat && m.sequence > lastCombatSequence_) {
      lastCombatSequence_ = m.sequence;
      for (const auto &event : m.events) {
        if (event.id <= lastEvent_)
          continue;
        lastEvent_ = event.id;
        const auto affected =
            event.kind == CombatKind::Shot ? event.owner : event.target;
        const auto knownGeneration =
            affected == entity_ ? life_.generation
                                : (remotes_.contains(affected)
                                       ? remotes_.at(affected).generation()
                                       : 0);
        const bool staleLife = event.generation < knownGeneration;
        if (!staleLife && pendingVisualEvents_.size() < 256)
          pendingVisualEvents_.push_back(event);
        if (event.kind == CombatKind::Shot) {
          ++stats_.shots;
          if (!staleLife && visualRounds_.size() < Combat::capacity)
            visualRounds_.push_back({event});
        } else {
          if (event.kind == CombatKind::Hit) {
            latestHit_ = event;
            if (event.target == entity_)
              ++stats_.hitsReceived;
            std::erase_if(visualRounds_, [&](const VisualRound &v) {
              return v.event.projectile == event.projectile;
            });
          }
          if (event.kind == CombatKind::Destroyed) {
            ++stats_.destructions;
            std::erase_if(visualRounds_, [&](const VisualRound &v) {
              return v.event.owner == event.target &&
                     v.event.generation <= event.generation;
            });
          }
          if (event.kind == CombatKind::Respawn) {
            ++stats_.respawns;
            std::erase_if(visualRounds_, [&](const VisualRound &v) {
              return v.event.owner == event.target &&
                     v.event.generation < event.generation;
            });
          }
          if (!staleLife && event.kind != CombatKind::Respawn &&
              visualEffects_.size() < 256)
            visualEffects_.push_back({event});
        }
      }
      stats_.peakVisuals = std::max(stats_.peakVisuals, visualRounds_.size());
      continue;
    }
    if (m.type == Type::Snapshot && m.sequence > lastSnapshotSequence_ &&
        m.tick >= stats_.serverTick) {
      lastSnapshotSequence_ = m.sequence;
      stats_.serverTick = m.tick;
      ++stats_.snapshots;
      const double halfPing = std::max(0, stats_.pingMs) * .001 * 60;
      const double estimate = double(m.tick) + halfPing;
      serverTime_ += std::clamp((estimate - serverTime_) * .2, -2.0, 2.0);
      // Slew render clock gradually; no packet-arrival-driven render jumps.
      renderTime_ += std::clamp((serverTime_ - 12 - renderTime_) * .1, -.5, .5);
      prediction_.simulator().setWeather(m.weather);
      const State oldDisplay = displayState();
      bool foundLocal = false;
      for (const auto &a : m.aircrafts) {
        if (a.id == entity_) {
          foundLocal = true;
          if (a.life.generation < life_.generation)
            continue;
          const bool reset = a.life.generation != life_.generation ||
                             a.life.alive() != life_.alive();
          life_ = a.life;
          prediction_.reconcile(m.tick, a);
          if (reset) {
            ++stats_.predictionResets;
            correction_ = {};
            attitudeCorrection_ = {};
            std::erase_if(visualRounds_, [&](const VisualRound &v) {
              return v.event.owner == entity_ &&
                     (v.event.generation < life_.generation || !life_.alive());
            });
            continue;
          }
          auto offset =
              oldDisplay.pos_ned - prediction_.simulator().state().pos_ned;
          correction_ = offset.norm() < 50 ? offset : Vec3{};
          attitudeCorrection_ =
              (oldDisplay.att * prediction_.simulator().state().att.conj())
                  .normalized();
        } else if (!tombstones_.contains(a.id) || m.tick > tombstones_.at(a.id))
          remotes_[a.id].push(
              static_cast<Tick>(std::llround(a.state.time / tickSeconds)), a);
      }
      std::vector<EntityId> absent;
      for (const auto &[id, track] : remotes_) {
        (void)track;
        if (std::none_of(m.aircrafts.begin(), m.aircrafts.end(),
                         [&](const Aircraft &a) { return a.id == id; }))
          absent.push_back(id);
      }
      for (auto id : absent)
        remotes_.erase(id);
      for (auto it = tombstones_.begin(); it != tombstones_.end();)
        if (it->second < m.tick)
          it = tombstones_.erase(it);
        else
          ++it;
      if (!foundLocal) {
        status_ = "local entity removed";
        disconnect();
      }
    }
  }
  std::erase_if(visualRounds_, [&](const VisualRound &v) {
    return (serverTime_ - double(v.event.tick)) * tickSeconds >=
           v.event.lifetime;
  });
  std::erase_if(visualEffects_, [&](const VisualEffect &v) {
    return (serverTime_ - double(v.event.tick)) * tickSeconds > 1.5;
  });
  if (connection_) {
    auto s = transport_.stats(connection_);
    stats_.pingMs = s.pingMs;
    stats_.unfragmentedPayload = s.unfragmentedPayload;
    stats_.wireIn = s.bytesInPerSecond;
    stats_.wireOut = s.bytesOutPerSecond;
  }
  stats_.renderTick = renderTime_;
  stats_.historySamples = 0;
  for (const auto &[id, t] : remotes_) {
    (void)id;
    stats_.historySamples += t.size();
  }
  metricsTime_ += dt;
  pingTimer_ += dt;
  if (ready() && pingTimer_ >= 1) {
    pingTimer_ = 0;
    Message m;
    m.type = Type::Ping;
    m.tick = prediction_.tick();
    m.sequence = stats_.sent + 1;
    send(m, false);
  }
  if (metricsTime_ >= 1) {
    stats_.snapshotHz =
        double(stats_.snapshots - lastSnapshots_) / metricsTime_;
    stats_.inputHz = double(stats_.inputMessages - lastInputs_) / metricsTime_;
    lastSnapshots_ = stats_.snapshots;
    lastInputs_ = stats_.inputMessages;
    metricsTime_ = 0;
  }
}
void Client::predict(const Controls &c) {
  if (!ready() || !life_.alive())
    return;
  if (++fireDivider_ % 12 == 0)
    sendFire();
  // Keep a bounded lead, allowing for measured one-way latency plus jitter.
  const double lead =
      std::clamp(double(std::max(0, stats_.pingMs)) * .06 + 8.0, 18.0, 60.0);
  const Tick target = static_cast<Tick>(
      std::clamp(serverTime_ + lead, 0., double(maxTimelineTick)));
  if (prediction_.tick() > target + 2)
    return;
  unsigned steps = 0;
  do {
    prediction_.advance(c);
    ++steps;
  } while (prediction_.tick() < target && steps < 8);
  if (++sendDivider_ % 2 == 0 && !prediction_.pending().empty()) {
    Message m;
    m.type = Type::Input;
    m.entity = entity_;
    m.generation = life_.generation;
    m.tick = prediction_.tick();
    m.sequence = prediction_.pending().back().sequence;
    const auto &p = prediction_.pending();
    m.baseline = replication_.acknowledged();
    m.recovery = replication_.needsRecovery();
    constexpr std::size_t normalRedundancy = 4;
    const auto first =
        p.size() > normalRedundancy ? p.size() - normalRedundancy : 0;
    for (std::size_t i = first; i < p.size(); ++i)
      m.commands.push_back(p[i]);
    send(m, false);
  }
}
void Client::weaponAction(WeaponActionKind kind, unsigned station) {
  if (!ready() || !life_.alive() || station > 7)
    return;
  WeaponMessage m;
  m.type = Type::WeaponAction;
  m.entity = entity_;
  m.tick = stats_.serverTick;
  m.sequence = ++weaponSequence_;
  m.action = {m.sequence, m.tick, life_.generation, kind,
              std::uint8_t(station)};
  const auto bytes = encodeWeapon(m);
  if (transport_.send(connection_, bytes, true)) {
    ++stats_.sent;
    stats_.bytesOut += bytes.size();
  } else
    ++stats_.sendFailures;
}
void Client::sendFire() {
  if (!ready() || !life_.alive() || !aircraftDefinition(type_).gun)
    return;
  Message m;
  m.type = Type::Fire;
  m.entity = entity_;
  m.fire = {++fireSequence_, prediction_.tick(), life_.generation, 0, firing_};
  m.tick = m.fire.tick;
  m.sequence = m.fire.sequence;
  send(m, false);
}
void Client::setFiring(bool held) {
  held = held && aircraftDefinition(type_).gun.has_value();
  if (firing_ != held) {
    firing_ = held;
    sendFire();
  }
}
std::vector<VisualLine> Client::combatLines() const {
  std::vector<VisualLine> lines;
  lines.reserve(visualRounds_.size() + visualEffects_.size() * 3);
  for (const auto &v : visualRounds_) {
    const auto &e = v.event;
    const double t = std::clamp((serverTime_ - double(e.tick)) * tickSeconds,
                                0.0, e.lifetime);
    const auto position =
        e.position + e.velocity * t + Vec3{0, 0, .5 * kG0 * t * t};
    lines.push_back(
        {position - (e.velocity + Vec3{0, 0, kG0 * t}).normalized() * 12,
         position, 0xff60eaff});
    if (t < .08) {
      lines.push_back(
          {e.position - Vec3{0, 2, 0}, e.position + Vec3{0, 2, 0}, 0xff60cfff});
      lines.push_back(
          {e.position - Vec3{0, 0, 2}, e.position + Vec3{0, 0, 2}, 0xff60cfff});
    }
  }
  for (const auto &v : visualEffects_) {
    const auto &e = v.event;
    const double t =
        std::max(0.0, (serverTime_ - double(e.tick)) * tickSeconds);
    const double radius =
        e.kind == CombatKind::Destroyed ? 3 + t * 20 : 1 + t * 3;
    const std::uint32_t color =
        e.kind == CombatKind::Destroyed ? 0xff2070ff : 0xffeeeeff;
    for (const auto axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}})
      lines.push_back(
          {e.position - axis * radius, e.position + axis * radius, color});
  }
  return lines;
}
std::vector<CombatEvent> Client::takeVisualEvents() {
  auto events = pendingVisualEvents_;
  pendingVisualEvents_.clear();
  return events;
}
std::vector<State> Client::remoteStates() const {
  std::vector<State> states;
  for (const auto &[id, t] : remotes_) {
    (void)id;
    if (t.alive())
      states.push_back(t.sample(renderTime_));
  }
  return states;
}
State Client::displayState() const {
  State s = prediction_.simulator().state();
  s.pos_ned += correction_;
  s.att = (attitudeCorrection_ * s.att).normalized();
  return s;
}
} // namespace ofs::net
