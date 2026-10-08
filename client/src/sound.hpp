#pragma once
// Sound.
//
// Nothing here is recorded: every sound is synthesised while it is heard, from
// filtered noise and a handful of oscillators, so the game ships no audio
// files. The work is split in two.
//
// SoundMixer is the instrument. It runs on the audio thread, knows nothing of
// the game, and turns a SoundScene (what is sounding continuously) and a queue
// of SoundEvents (things that happen once) into stereo samples.
//
// SoundDirector is the player of it. Once a frame it is told what the pilot
// can see, works out what that would sound like from where the camera is -
// distance, direction, the Doppler shift of a pass, the delay before a far
// explosion arrives - and hands the result to the mixer. Like the visual
// effects, it is presentation only and feeds nothing back into the simulation.

#include "ofs/aircraft.hpp"
#include "ofs/aircraft_definition.hpp"
#include "ofs/weapons.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace ofs::client {

inline constexpr int kSoundRate = 48000;

// Things that sound once.
enum class SoundKind : std::uint8_t {
  // Weapons and the damage they do, heard in the world.
  Gun,            // one round leaving a cannon
  MissileLaunch,  // a motor lighting on the rail
  Explosion,      // an aircraft blowing up
  Crash,          // an aircraft flying into the ground
  PartBreak,      // a wing or fin tearing away
  Eject,          // the canopy going and the seat firing
  Parachute,      // the canopy of a parachute snapping open
  Detonation,     // a warhead going off
  Impact,         // a round striking something else
  HitTaken,       // a round striking the pilot's own airframe
  Flare,
  Chaff,
  // The pilot's own airframe.
  ReheatLight,    // an afterburner lighting
  GearLock,
  Touchdown,
  Crunch,         // the airframe striking the ground
  // The flight deck and the interface.
  DryFire,        // the trigger pulled on an empty gun
  WeaponSelect,
  LockBeep,
  LockLost,
  Caution,        // damage that needs attention
  MissileAlert,   // a missile has been fired at the pilot
  HitMarker,      // own rounds striking
  KillChime,
  Killed,
  Serviced,       // repaired and rearmed
  ChatBlip,
  UiClick,
  UiOpen,
  UiClose,
  Count
};

// The four volume controls a pilot has, besides the master.
enum class SoundBus : std::uint8_t { Engines, Weapons, Airframe, Cockpit, Count };

struct SoundEvent {
  SoundKind kind{SoundKind::UiClick};
  float gain{1};
  float pan{0};       // -1 left .. 1 right
  float delay{0};     // seconds before it is heard
  float distance{0};  // metres; air dulls what has travelled far
  float pitch{1};
  float interior{0};  // 0 heard in the open .. 1 through the canopy
};

// One aircraft's engines as the listener hears them.
struct JetSound {
  std::uint64_t id{};    // 0 for an empty slot
  float power[2]{};      // 0 idle .. 1 full; negative for an engine that has stopped
  float reheat[2]{};
  float gain{};          // what distance leaves of it
  float pan{};
  float doppler{1};      // pitch ratio
  float rear{};          // 0 heard from ahead .. 1 from behind, in the exhaust
  float interior{};      // 1 from its own flight deck
  float air{20000};      // Hz above which the distance has absorbed it
  float pitch{1};        // the engine type's own voice
  float fan{};           // 0 a fighter's turbojet .. 1 an airliner's turbofan
};

struct MissileSound {
  std::uint64_t id{};
  float gain{}, pan{}, doppler{1}, air{20000};
};

// Everything that sounds continuously, as of one frame.
struct SoundScene {
  static constexpr std::size_t kJets = 5, kMissiles = 4;
  std::array<JetSound, kJets> jets{};  // the pilot's own first
  std::array<MissileSound, kMissiles> missiles{};
  // Air past the listener, and what the pilot's own airframe adds to it.
  float airspeed{};      // m/s
  float density{1};      // relative to sea level
  float own{};           // how much of the pilot's own airframe is heard, 0..1
  float buffet{};        // 0..1
  float airbrake{};      // 0..1, already scaled by the speed that makes it roar
  float gearDrag{};
  float roll{};          // m/s on the wheels
  float scrape{};        // 0..1, the airframe along the ground
  float gearMotor{}, flapMotor{};
  float interior{};      // the listener sits in the flight deck
  // The flight deck's own voices.
  float stall{};         // 0 or 1
  float warning{};       // 0 none, otherwise how close the missile is, up to 1
  float seeker{};        // 0 silent, below 1 a heat seeker finding its target, 1 locked on
  float strain{};        // 0 rested .. 1 blacked out: the pilot's own pulse
  // Levels.
  float master{.8f};
  std::array<float, std::size_t(SoundBus::Count)> volume{1, 1, 1, 1};
  float world{1};        // 0 while the flight is held; the interface still sounds
  float muffle{};        // 1 after the pilot has been shot down
};

class SoundMixer {
 public:
  SoundMixer();
  ~SoundMixer();
  SoundMixer(const SoundMixer&) = delete;
  SoundMixer& operator=(const SoundMixer&) = delete;

  // From the game thread.
  void setScene(const SoundScene& scene);
  void play(const SoundEvent& event);

  // From the audio thread: `frames` interleaved stereo samples at kSoundRate.
  // Never blocks and never allocates.
  void render(float* stereo, std::size_t frames);

  // Loudest sample produced so far, for the tests and the developer overlay.
  float peak() const { return peak_.load(std::memory_order_relaxed); }
  std::size_t activeVoices() const { return activeVoices_.load(std::memory_order_relaxed); }

 private:
  struct State;
  State* state_;
  std::mutex mutex_;
  SoundScene pending_;
  bool dirty_{};
  std::array<SoundEvent, 96> queue_{};
  std::size_t queued_{};
  std::atomic<float> peak_{};
  std::atomic<std::size_t> activeVoices_{};
};

// What the director is told each frame.
struct SoundFrame {
  double dt{};
  bool held{};           // the flight is paused
  bool focused{true};    // the window has the keyboard
  // The listener: the camera.
  Vec3 eye{}, forward{1, 0, 0}, up{0, 0, -1};
  bool attached{};       // the camera rides the pilot's aircraft
  bool cockpit{};
  // The pilot's own aircraft.
  const State* own{};
  AircraftType type{AircraftType::A320};
  bool alive{true};      // false while shot down or crashed
  Instruments instruments{};
  double qbar{};
  bool onWheels{};
  double gearCommand{1}; // the lever, 0 up .. 1 down
  // Everyone else, and the missiles in flight.
  struct Aircraft { const State* state{}; AircraftType type{AircraftType::A320}; std::uint64_t id{}; };
  std::span<const Aircraft> others;
  struct Missile { std::uint64_t id{}; Vec3 position{}, velocity{}; bool powered{}, own{}; double age{}; };
  std::span<const Missile> missiles;
  // Missiles flying at the pilot.
  struct Threat { Vec3 position{}, velocity{}; bool decoyed{}; };
  std::span<const Threat> threats;
  // The selected missile's seeker.
  bool missileSelected{};
  weapons::WeaponType weapon{weapons::WeaponType::None};
  bool seekerTracking{}, seekerReady{};
  double lockProgress{};
  bool radarLocked{};
  double health{100};
  // How far the pilot's sight has gone under load, 0..1, and whether they are out.
  double strain{};
  bool unconscious{};
  // The pilot's settings.
  bool enabled{true}, muteUnfocused{true};
  float master{.8f};
  std::array<float, std::size_t(SoundBus::Count)> volume{1, 1, 1, 1};
};

class SoundDirector {
 public:
  explicit SoundDirector(SoundMixer& mixer) : mixer_(mixer) {}

  // A sound somewhere in the world, heard from wherever the camera is when the
  // frame is resolved. `own` marks one made by the pilot's own aircraft.
  void at(SoundKind kind, const Vec3& position, float gain = 1, bool own = false);
  // A sound in the flight deck or the interface, heard as it is.
  void cue(SoundKind kind, float gain = 1, float pitch = 1);
  // The airframe is sliding along the ground, 0..1; it dies away unless repeated.
  void scrape(float amount);
  // Resolves the frame: the continuous scene, and every sound queued since the last.
  void update(const SoundFrame& frame);
  // Forgets what was sounding, for a new flight.
  void reset();

  const SoundScene& scene() const { return scene_; }

 private:
  struct Pending { SoundKind kind; Vec3 position; float gain; bool own; };
  struct Emitter { float gain{}, pan{}, doppler{1}, air{20000}, rear{}; double distance{}; };
  Emitter hear(const Vec3& position, const Vec3& velocity, const Vec3* nose, double reference, double reach) const;
  float random();

  SoundMixer& mixer_;
  SoundScene scene_;
  std::vector<Pending> pending_;
  std::uint32_t seed_{0x9e3779b9u};
  // The listener, as of the last frame.
  Vec3 eye_{}, forward_{1, 0, 0}, right_{0, 1, 0}, velocity_{};
  bool listenerKnown_{}, cockpit_{};
  // What was true last frame, for the things that are only heard to change.
  bool primed_{};
  std::array<float, SoundScene::kJets> reheat_{};
  std::array<bool, 2> engineRunning_{true, true};
  std::vector<std::uint64_t> missilesSeen_;
  std::size_t threats_{};
  bool onWheels_{true}, radarLocked_{}, seekerReady_{}, alive_{true};
  double airborne_{}, gearCommand_{1}, gearMotor_{}, flap_{}, flapMotor_{}, health_{100};
  int lockStep_{};
  double scrape_{};
};

}  // namespace ofs::client
