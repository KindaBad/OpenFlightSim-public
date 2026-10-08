#include "ofs/net/server.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
namespace ofs::net {
Server::Server(const ServerConfig &config)
    : config_(config), world_(config.airborne, config.gun) {
  if (config.maxClients < 1 || config.maxClients > maxPlayers ||
      config.snapshotHz < 1 || config.snapshotHz > 60 || config.bots > 8 ||
      config.bots >= maxPlayers || config.maxClients + config.bots > maxPlayers)
    throw std::invalid_argument(
        "server limits: players plus bots <=64, bots 0..8, snapshot rate 1..60");
  world_.setMissileReload(config.missileReload);
  transport_.listen(config.bind, config.port);
  config_.lobbyName = lobbyName(config_.lobbyName);
  if (!config_.lobbyName.empty())
    discovery_ = std::make_unique<DiscoveryResponder>(config_.discoveryPort);
}
Server::~Server() { shutdown(); }
std::uint16_t Server::port() const { return transport_.port(); }
LobbyInfo Server::lobby() const {
  LobbyInfo info;
  info.name = config_.lobbyName;
  info.version = config_.version;
  info.port = port();
  info.protocol = protocolVersion;
  info.bots = std::uint8_t(world_.botCount());
  info.players = std::uint8_t(world_.players().size() - world_.botCount());
  info.maxPlayers = std::uint8_t(config_.maxClients);
  return info;
}
std::string Server::pilot(EntityId id) const {
  const auto player = world_.players().find(id);
  return player == world_.players().end() ? "Pilot " + std::to_string(id)
                                          : player->second.name;
}
void Server::notice(const std::string &text) {
  Message m;
  m.type = Type::Chat;
  m.tick = world_.tick();
  m.sequence = ++sequence_;
  m.text = text.substr(0, maxChatText);
  broadcast(m, true);
}
void Server::send(Connection c, const Message &m, bool reliable) {
  auto b = encode(m);
  if (transport_.send(c, b, reliable)) {
    ++stats_.sent;
    stats_.bytesOut += b.size();
    if (reliable)
      stats_.reliableBytes += b.size();
    stats_.protocolHeaderBytesOut += 24;
  } else {
    ++stats_.sendFailures;
    if (reliable && sessions_.contains(c))
      sessions_.at(c).reliableFailure = true;
  }
}
void Server::broadcast(const Message &m, bool reliable) {
  for (const auto &[c, s] : sessions_)
    if (s.entity)
      send(c, m, reliable);
}
void Server::remove(Connection c, const std::string &reason, bool reject) {
  auto it = sessions_.find(c);
  if (it == sessions_.end())
    return;
  const auto id = it->second.entity;
  const auto name = id ? pilot(id) : std::string{};
  if (reject) {
    Message m;
    m.type = Type::Reject;
    m.text = reason;
    m.tick = world_.tick();
    send(c, m, true);
  }
  transport_.close(c, reason, reject);
  sessions_.erase(it);
  if (id) {
    world_.leave(id);
    Message left;
    left.type = Type::Left;
    left.entity = id;
    left.tick = world_.tick();
    left.sequence = ++sequence_;
    broadcast(left, true);
    notice(name + " left the game");
  }
  ++stats_.disconnects;
}
void Server::shutdown() {
  while (!sessions_.empty())
    remove(sessions_.begin()->first, "server shutdown", true);
}
void Server::poll() {
  if (discovery_)
    discovery_->poll(lobby());
  for (const auto &e : transport_.poll()) {
    if (e.kind == TransportEvent::Kind::Connected) {
      if (sessions_.size() >= config_.maxClients) {
        transport_.close(e.connection, "server full");
        continue;
      }
      sessions_[e.connection].connected = world_.tick();
      continue;
    }
    if (e.kind == TransportEvent::Kind::Disconnected) {
      remove(e.connection, e.reason);
      continue;
    }
    auto it = sessions_.find(e.connection);
    if (it == sessions_.end())
      continue;
    auto &s = it->second;
    ++stats_.received;
    stats_.bytesIn += e.bytes.size();
    if (world_.tick() - s.window >= 120) {
      s.window = world_.tick();
      s.messages = s.commands = s.fires = s.weaponActions = s.chats = 0;
    }
    if (++s.messages > 240) {
      ++stats_.invalid;
      remove(e.connection, "message rate limit", true);
      continue;
    }
    if (e.bytes.size() >= 7 && e.bytes[6] == unsigned(Type::WeaponAction)) {
      WeaponMessage action;
      const bool valid = decodeWeapon(e.bytes, action) && s.entity &&
                         action.entity == s.entity && ++s.weaponActions <= 30 &&
                         world_.enqueueWeapon(s.entity, action.action);
      if (!valid) {
        ++stats_.invalid;
        if (++s.strikes >= 8)
          remove(e.connection, "invalid weapon action", true);
      }
      continue;
    }
    Message m;
    std::string reason;
    bool ok = decode(e.bytes, m, reason);
    if (!ok)
      ++stats_.decodeFailures;
    if (!ok && reason == "protocol version mismatch") {
      ++stats_.invalid;
      remove(e.connection, reason, true);
      continue;
    }
    if (ok && m.type == Type::Hello && !s.entity) {
      s.entity = world_.join(m.aircraftType, m.text);
      if (!s.entity) {
        remove(e.connection, "world full", true);
        continue;
      }
      while (world_.botCount() < config_.bots)
        if (!world_.joinBot()) break;
      // Stable peer phases spread publication work over the physics clock.
      s.publicationAccumulator =
          ((s.entity - 1) % 120) * config_.snapshotHz % 120;
      Message welcome;
      welcome.type = Type::Welcome;
      welcome.tick = world_.tick();
      welcome.snapshotHz = static_cast<std::uint16_t>(config_.snapshotHz);
      welcome.aircraft = world_.aircraft(s.entity);
      welcome.weather = world_.weather();
      send(e.connection, welcome, true);
      // The newcomer learns who is already here, then everyone learns of it.
      Message joined;
      joined.type = Type::Joined;
      joined.tick = world_.tick();
      for (const auto &[id, player] : world_.players()) {
        if (id == s.entity)
          continue;
        joined.entity = id;
        joined.text = player.name;
        joined.sequence = ++sequence_;
        send(e.connection, joined, true);
      }
      joined.entity = s.entity;
      joined.text = pilot(s.entity);
      joined.sequence = ++sequence_;
      broadcast(joined, true);
      notice(joined.text + " joined the game");
      continue;
    }
    if (ok && m.type == Type::Chat && s.entity) {
      // A talkative client is ignored, not struck: chat is never an attack on
      // the simulation.
      if (++s.chats > 4 || m.text.find_first_not_of(' ') == std::string::npos)
        continue;
      Message line;
      line.type = Type::Chat;
      line.tick = world_.tick();
      line.sequence = ++sequence_;
      line.entity = s.entity;
      line.text = m.text;
      broadcast(line, true);
      continue;
    }
    if (ok && m.type == Type::Ping && s.entity) {
      m.type = Type::Pong;
      m.tick = world_.tick();
      send(e.connection, m, false);
      continue;
    }
    if (ok && m.type == Type::SnapshotAck && s.entity) {
      if (!s.replication.acknowledge(m.baseline, m.recovery))
        ++stats_.invalid;
      continue;
    }
    if (ok && m.type == Type::Input && s.entity && m.entity == s.entity) {
      stats_.inputBytes += e.bytes.size();
      ok = s.replication.acknowledge(m.baseline, m.recovery);
      s.commands += static_cast<unsigned>(m.commands.size());
      ok = ok && s.commands <= 1920 &&
           world_.enqueue(s.entity, m.commands, m.generation);
    } else if (ok && m.type == Type::Fire && s.entity && m.entity == s.entity) {
      ok = ++s.fires <= 60 && world_.enqueueFire(s.entity, m.fire);
    } else
      ok = false;
    if (!ok) {
      ++stats_.invalid;
      if (++s.strikes >= 8)
        remove(e.connection,
               reason == "protocol version mismatch" ? reason
                                                     : "invalid client message",
               true);
    }
  }
  std::vector<Connection> expired;
  for (const auto &[c, s] : sessions_)
    if (s.reliableFailure || (!s.entity && world_.tick() - s.connected > 600))
      expired.push_back(c);
  for (auto c : expired)
    remove(c,
           sessions_.at(c).reliableFailure ? "reliable send buffer exhausted"
                                           : "hello timeout",
           true);
}
void Server::step() {
  world_.step();
  const auto weaponEvents = world_.missiles().takeEvents();
  for (auto &[connection, session] : sessions_)
    if (session.entity) {
      const auto &player = world_.players().at(session.entity);
      auto radar = radarProjection(player.weapons, player.life.generation);
      radar.loadouts = world_.loadoutsNear(session.entity);
      const auto packets = session.weapons.build(
          world_.tick(), session.entity, player.sim.state().pos_ned, radar,
          world_.missiles().missiles(), weaponEvents);
      for (const auto &packet : packets) {
        if (transport_.send(connection, packet.bytes, packet.reliable)) {
          ++stats_.sent;
          ++stats_.weaponMessages;
          stats_.bytesOut += packet.bytes.size();
          if (packet.reliable)
            stats_.reliableBytes += packet.bytes.size();
          if (packet.bytes[6] == unsigned(Type::RadarState))
            stats_.radarBytes += packet.bytes.size();
          else
            stats_.missileBytes += packet.bytes.size();
          stats_.maxWeaponPacket =
              std::max(stats_.maxWeaponPacket, packet.bytes.size());
          stats_.protocolHeaderBytesOut += 24;
        } else {
          ++stats_.sendFailures;
          if (packet.reliable)
            session.reliableFailure = true;
        }
      }
    }
  auto events = world_.combat().takeEvents();
  // An aircraft that was left by its pilot is announced as that, not as a crash.
  std::vector<EntityId> left;
  for (const auto &event : events) {
    if (event.kind == CombatKind::Ejected)
      left.push_back(event.target);
    if (event.kind != CombatKind::Destroyed)
      continue;
    const bool ejected =
        std::find(left.begin(), left.end(), event.target) != left.end();
    notice(event.owner != event.target
               ? pilot(event.owner) + (ejected ? " forced " : " shot down ") +
                     pilot(event.target) + (ejected ? " to eject" : "")
               : pilot(event.target) + (ejected ? " ejected" : " crashed"));
  }
  stats_.combatSerializationUs = 0;
  for (std::size_t offset = 0; offset < events.size();
       offset += maxCombatEvents) {
    Message m;
    m.type = Type::Combat;
    m.tick = world_.tick();
    m.sequence = ++sequence_;
    const auto end = std::min(events.size(), offset + maxCombatEvents);
    m.events.assign(events.begin() + offset, events.begin() + end);
    const auto start = std::chrono::steady_clock::now();
    const auto bytes = encode(m);
    stats_.combatSerializationUs +=
        std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - start)
            .count();
    for (auto &[c, s] : sessions_)
      if (s.entity) {
        Message relevant = m;
        std::erase_if(relevant.events, [&](const auto &event) {
          return event.owner != s.entity && event.target != s.entity &&
                 !s.replication.interested(event.owner) &&
                 !s.replication.interested(event.target);
        });
        if (relevant.events.empty())
          continue;
        const auto eventBytes = encode(relevant);
        if (transport_.send(c, eventBytes, true)) {
          ++stats_.sent;
          ++stats_.combatMessages;
          stats_.bytesOut += eventBytes.size();
          stats_.combatBytes += eventBytes.size();
          stats_.reliableBytes += eventBytes.size();
          stats_.protocolHeaderBytesOut += 24;
        } else {
          ++stats_.sendFailures;
          s.reliableFailure = true;
        }
      }
  }
  snapshotAccumulator_ += config_.snapshotHz;
  if (snapshotAccumulator_ >= 120) {
    snapshotAccumulator_ -= 120;
    ++stats_.snapshotCount; // Nominal publication clock, including an empty
                            // server.
  }
  bool publication = false;
  for (auto &[connection, session] : sessions_) {
    (void)connection;
    session.publicationDue = false;
    if (!session.entity)
      continue;
    session.publicationAccumulator += config_.snapshotHz;
    if (session.publicationAccumulator >= 120) {
      session.publicationAccumulator -= 120;
      session.publicationDue = true;
      publication = true;
    }
  }
  if (!publication)
    return;
  const auto start = std::chrono::steady_clock::now();
  const auto worldSnapshot = world_.snapshot();
  InterestGrid grid;
  grid.rebuild(worldSnapshot.aircrafts);
  ++stats_.publicationCount;
  stats_.interestUs = stats_.projectionUs = stats_.deltaEncodeUs =
      stats_.networkMemoryBytes = 0;
  stats_.interestUs = grid.indexUs();
  stats_.projectionUs = grid.projectionUs();
  stats_.transportUs = stats_.linkStatsUs = 0;
  stats_.wireOutboundBps = stats_.wireInboundBps = 0;
  stats_.replication.tierCounts = {};
  stats_.replication.baselineMisses = stats_.replication.recoveries =
      stats_.replication.rejectedAcks = 0;
  stats_.replication.staleAcks = stats_.replication.recoveryRequests =
      stats_.replication.recoveryCoalesced =
          stats_.replication.recoveryKeyframes =
              stats_.replication.recoveryRetries =
                  stats_.replication.recoveryCompleted = 0;
  for (auto &[connection, session] : sessions_)
    if (session.publicationDue) {
      const auto linkStart = std::chrono::steady_clock::now();
      const auto link = session.link = transport_.stats(connection);
      stats_.linkStatsUs += std::chrono::duration<double, std::micro>(
                                std::chrono::steady_clock::now() - linkStart)
                                .count();
      if (link.pendingBytes > 131072) {
        ++stats_.sendFailures;
        continue;
      }
      auto update = session.replication.build(world_.tick(), session.entity,
                                              worldSnapshot.aircrafts, grid,
                                              world_.weather());
      for (const auto &event : update.lifecycle)
        send(connection, event, true);
      std::size_t size = 0;
      const auto sendStart = std::chrono::steady_clock::now();
      const auto accepted = transport_.sendBatch(connection, update.packets);
      stats_.transportUs += std::chrono::duration<double, std::micro>(
                                std::chrono::steady_clock::now() - sendStart)
                                .count();
      for (std::size_t i = 0; i < update.packets.size(); ++i) {
        const auto &packet = update.packets[i];
        size += packet.size();
        if (i < accepted.size() && accepted[i]) {
          ++stats_.sent;
          stats_.bytesOut += packet.size();
          stats_.snapshotBytes += packet.size();
          stats_.protocolHeaderBytesOut += 99;
        } else
          ++stats_.sendFailures;
      }
      const auto &r = session.replication.stats();
      stats_.interestUs += r.queryUs;
      stats_.projectionUs += r.projectionUs;
      stats_.deltaEncodeUs += r.deltaEncodeUs;
      stats_.replication.record(size);
      stats_.replication.chunks += update.packets.size();
      stats_.replication.bytes += size;
      stats_.replication.entities +=
          r.tierCounts[0] + r.tierCounts[1] + r.tierCounts[2] + r.tierCounts[3];
      stats_.replication.maxChunk =
          std::max(stats_.replication.maxChunk, r.maxChunk);
      stats_.replication.snapshots += 1;
      if (!update.packets.empty()) {
        // Baseline sequence is the 8 bytes immediately after the common header.
        bool keyframe = true;
        for (unsigned i = 24; i < 32; ++i)
          keyframe = keyframe && update.packets.front()[i] == 0;
        if (keyframe)
          ++stats_.replication.keyframes;
        else
          ++stats_.replication.deltas;
      }
    }
  std::size_t pendingTransportBytes = 0;
  for (const auto &[connection, session] : sessions_)
    if (session.entity) {
      (void)connection;
      const auto &r = session.replication.stats();
      pendingTransportBytes += std::max(0, session.link.pendingBytes);
      stats_.wireOutboundBps += session.link.bytesOutPerSecond;
      stats_.wireInboundBps += session.link.bytesInPerSecond;
      stats_.networkMemoryBytes += session.replication.memoryBytes();
      stats_.replication.baselineMisses += r.baselineMisses;
      stats_.replication.recoveries += r.recoveries;
      stats_.replication.rejectedAcks += r.rejectedAcks;
      stats_.replication.staleAcks += r.staleAcks;
      stats_.replication.recoveryRequests += r.recoveryRequests;
      stats_.replication.recoveryCoalesced += r.recoveryCoalesced;
      stats_.replication.recoveryKeyframes += r.recoveryKeyframes;
      stats_.replication.recoveryRetries += r.recoveryRetries;
      stats_.replication.recoveryCompleted += r.recoveryCompleted;
      for (unsigned i = 0; i < 4; ++i)
        stats_.replication.tierCounts[i] += r.tierCounts[i];
    }
  stats_.pendingTransportBytesPeak =
      std::max(stats_.pendingTransportBytesPeak, pendingTransportBytes);
  const double seconds = double(world_.tick()) * tickSeconds;
  stats_.outboundBps = double(stats_.bytesOut) / seconds;
  stats_.inboundBps = double(stats_.bytesIn) / seconds;
  stats_.snapshotUs = std::chrono::duration<double, std::micro>(
                          std::chrono::steady_clock::now() - start)
                          .count();
}
} // namespace ofs::net
