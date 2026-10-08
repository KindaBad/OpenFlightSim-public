#pragma once
#include "ofs/net/replication.hpp"
#include "ofs/net/transport.hpp"
#include "ofs/net/weapons_protocol.hpp"
#include "ofs/net/world.hpp"
#include <deque>
namespace ofs::net {
struct PredictionStats {
  std::uint64_t reconciliations{}, rebases{};
  double error{}, maxError{}, orientationCorrection{}, velocityCorrection{};
  std::uint64_t replayCount{};
  std::deque<double> positionErrors, orientationErrors, velocityErrors;
  std::size_t maxPending{};
};
class Prediction {
public:
  void initialize(Tick, const Aircraft &, unsigned lead = 18,
                  Weather weather = {});
  Command advance(const Controls &);
  void reconcile(Tick, const Aircraft &);
  Simulator &simulator() { return sim_; }
  const Simulator &simulator() const { return sim_; }
  Tick tick() const { return tick_; }
  const std::deque<Command> &pending() const { return pending_; }
  const PredictionStats &stats() const { return stats_; }

private:
  Simulator sim_;
  Tick tick_{};
  std::uint64_t sequence_{};
  std::deque<Command> pending_;
  std::map<Tick, State> history_;
  PredictionStats stats_;
  std::uint32_t generation_{};
  bool alive_{true};
  AircraftType type_{AircraftType::A320};
};
struct RemoteSample {
  Tick tick{};
  Aircraft aircraft;
};
class RemoteTrack {
public:
  void push(Tick, const Aircraft &);
  State sample(double tick) const;
  Aircraft sampleAircraft(double tick) const;
  std::size_t size() const { return history_.size(); }
  std::uint32_t generation() const {
    return history_.empty() ? 0 : history_.back().aircraft.life.generation;
  }
  bool alive() const {
    return !history_.empty() && history_.back().aircraft.life.alive();
  }
  Tier tier() const { return tier_; }
  void setTier(Tier tier) { tier_ = tier; }
  double interpolationDelay() const {
    return tier_ == Tier::Far ? 60 : tier_ == Tier::Medium ? 24 : 12;
  }
  double newestTick() const {
    return history_.empty() ? 0 : double(history_.back().tick);
  }

private:
  std::deque<RemoteSample> history_;
  Tier tier_{Tier::Near};
  mutable double extraDelay_{}, lastSampleTick_{};
};
struct ClientStats {
  std::uint64_t received{}, sent{}, bytesIn{}, bytesOut{}, snapshots{},
      inputMessages{}, invalid{}, sendFailures{}, joined{}, left{};
  double snapshotHz{}, inputHz{};
  Tick serverTick{};
  int pingMs{-1};
  std::uint64_t shots{}, hitsReceived{}, destructions{}, respawns{},
      predictionResets{};
  std::size_t peakVisuals{};
  double wireIn{}, wireOut{}, renderTick{};
  std::size_t historySamples{};
  ReplicationStats replication;
  std::size_t networkMemoryBytes{};
  int unfragmentedPayload{};
};
// One line of chat. `from` is zero for a notice from the server itself.
struct ChatLine {
  EntityId from{};
  std::string name, text;
};
struct VisualRound {
  CombatEvent event;
};
struct VisualEffect {
  CombatEvent event;
};
struct VisualLine {
  Vec3 start, end;
  std::uint32_t color{};
};
class Client {
public:
  explicit Client(std::string name = "pilot",
                  AircraftType type = AircraftType::A320);
  ~Client();
  void connect(const std::string &address, std::uint16_t port);
  void disconnect();
  void poll(double elapsed);
  void predict(const Controls &);
  void setFiring(bool held);
  void weaponAction(WeaponActionKind, unsigned station = 0);
  const RadarNetState &radar() const { return weapons_.radar; }
  const std::map<std::uint64_t, MissileNetState> &missiles() const {
    return weapons_.missiles();
  }
  std::vector<MissileNetState>
  missilePresentation(std::vector<double> *sampled = nullptr) const {
    return weapons_.sample(renderTime_, sampled);
  }
  std::vector<MissileEvent> takeMissileDetonations() {
    return weapons_.takeDetonations();
  }
  std::vector<MissileEvent> takeMissileTerminations() {
    return weapons_.takeMissileTerminations();
  }
  // Sends a line to everyone in the game; over-long text is cut to fit.
  void chat(std::string text);
  std::vector<ChatLine> takeChat();
  // The pilot flying an aircraft, or an empty name before the server said.
  const std::string &pilot(EntityId) const;
  const std::string &name() const { return name_; }
  const Life &life() const { return life_; }
  const CombatEvent &latestHit() const { return latestHit_; }
  std::size_t visualCount() const { return visualRounds_.size(); }
  std::vector<VisualLine> combatLines() const;
  std::vector<CombatEvent> takeVisualEvents();
  AircraftType aircraftType() const { return type_; }
  bool ready() const { return entity_ != 0; }
  EntityId entity() const { return entity_; }
  const std::string &status() const { return status_; }
  const ClientStats &stats() const { return stats_; }
  Prediction &prediction() { return prediction_; }
  const Prediction &prediction() const { return prediction_; }
  const std::map<EntityId, RemoteTrack> &remotes() const { return remotes_; }
  std::vector<State> remoteStates() const;
  State displayState() const;
  // Test-only access still sends through the real GNS connection.
  bool sendTestPacket(std::span<const std::uint8_t> bytes);

private:
  ReplicationReceiver replication_;
  WeaponReplicationReceiver weapons_;
  std::uint64_t weaponSequence_{};
  Transport transport_;
  Connection connection_{};
  std::string name_, status_{"offline"};
  EntityId entity_{};
  Prediction prediction_;
  std::map<EntityId, RemoteTrack> remotes_;
  std::map<EntityId, std::string> pilots_;
  std::vector<ChatLine> chat_;
  std::map<EntityId, Tick> tombstones_;
  ClientStats stats_;
  double serverTime_{}, renderTime_{}, pingTimer_{}, metricsTime_{};
  std::uint64_t lastSnapshotSequence_{}, lastSnapshots_{}, lastInputs_{};
  unsigned sendDivider_{}, fireDivider_{};
  bool firing_{};
  std::uint64_t fireSequence_{}, lastCombatSequence_{}, lastEvent_{};
  Life life_;
  AircraftType type_;
  CombatEvent latestHit_;
  std::vector<VisualRound> visualRounds_;
  std::vector<VisualEffect> visualEffects_;
  std::vector<CombatEvent> pendingVisualEvents_;
  Tick lastRecoveryTick_{};
  bool recoverySent_{};
  void requestRecovery();
  void sendFire();
  Vec3 correction_;
  Quat attitudeCorrection_;
  void send(const Message &, bool reliable);
};
} // namespace ofs::net
