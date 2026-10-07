#pragma once
// SI, FRD/NED. Engineering development weapons; no classified reproduction.
#include "ofs/aircraft_definition.hpp"
#include "ofs/atmosphere.hpp"
#include "ofs/inertia.hpp"
#include <cstdint>
#include <span>
#include <vector>
namespace ofs::weapons {
struct EntityRef {
  std::uint64_t id{};
  std::uint32_t generation{};
  bool operator==(const EntityRef &) const = default;
};
struct SensorTarget {
  EntityRef entity;
  Vec3 position, velocity;
  Quat attitude;
  AircraftType type{AircraftType::Typhoon};
  double power{.5}, afterburner{};
  bool alive{true};
};
enum class WeaponType : std::uint8_t { None, Infrared, ActiveRadar };
enum class MotorPhase : std::uint8_t { Ignition, Boost, Sustain, Burnout };
enum class SeekerPhase : std::uint8_t { Searching, Tracking, Lost, Midcourse };
struct MotorDefinition {
  double propellant{}, boostThrust{}, boostTime{}, sustainThrust{},
      sustainTime{};
  // Seconds between release and motor ignition; ejected stores fall clear.
  double ignitionDelay{};
};
struct AeroDefinition {
  double area{}, normalSlope{20}, controlLift{2}, cd0{.45}, induced{.07},
      transonic{.35}, wave{.20}, stability{6}, controlMoment{12}, damping{20};
};
struct SeekerDefinition {
  double fov{20 * kDeg2Rad}, gimbal{60 * kDeg2Rad}, rate{120 * kDeg2Rad},
      signalRange{8000}, acquireTime{.12}, memoryTime{.5},
      activationRange{12000};
  // Seconds a mounted seeker holds its target before the launch is released.
  double lockTime{.55};
};
struct MissileDefinition {
  WeaponType type;
  const char *name;
  double length{}, diameter{}, launchMass{};
  MotorDefinition motor;
  AeroDefinition aero;
  SeekerDefinition seeker;
  double navigation{3.5}, maxG{35}, maxAlpha{25 * kDeg2Rad},
      controlLimit{25 * kDeg2Rad}, actuatorRate{180 * kDeg2Rad}, fuseRadius{7},
      damageRadius{18}, damage{180}, armTime{.35}, minimumRange{350},
      lifetime{45};
};
const MissileDefinition &missileDefinition(WeaponType);
struct Station {
  Vec3 position;
  std::uint8_t compatible{}; // bit 1 IR, bit 2 active radar
  WeaponType mounted{WeaponType::None};
};
struct Inventory {
  std::vector<Station> stations;
  WeaponType selected{WeaponType::Infrared};
  void reset(AircraftType);
  unsigned remaining(WeaponType) const;
  int nextStation() const;
  bool consume(unsigned station, WeaponType);
  // Uses the existing payload/CG path with a distributed tensor correction.
  void applyPayload(const AircraftConfig &, State &) const;
};
struct Measurement {
  Vec3 position, velocity;
  bool valid{};
};
struct SeekerState {
  SeekerPhase phase{SeekerPhase::Searching};
  Vec3 boresight{1, 0,
                 0}; // inertially stabilized LOS represented in world frame
  Measurement measurement;
  double acquisition{}, missed{};
};
struct MissileTelemetry {
  double speed{}, mach{}, altitude{}, kineticEnergy{}, propellant{}, age{},
      distance{}, targetRange{}, commandedG{}, achievedG{}, thrust{},
      dynamicPressure{};
};
struct MissileState {
  Vec3 position, velocity, omega;
  Quat attitude;
  double age{}, propellant{}, mass{}, distance{}, pitchControl{}, yawControl{};
  Vec3 inertia;
  MotorPhase motor{MotorPhase::Ignition};
  SeekerState seeker;
  Measurement midcourse;
  double estimateAge{};
  bool autonomous{};
  MissileTelemetry telemetry;
};
MissileState launchState(const MissileDefinition &, const State &, Vec3 station,
                         Measurement);
double motorThrust(const MotorDefinition &, double age);
double dragCoefficient(const AeroDefinition &, double mach,
                       double normalCoefficient);
Vec3 proportionalNavigation(Vec3 relativePosition, Vec3 relativeVelocity,
                            Vec3 missileVelocity, double navigation,
                            double accelerationLimit);
bool lineOfSight(Vec3, Vec3);
double targetRcs(const SensorTarget &, Vec3 observer);
double infraredSignal(const SensorTarget &, Vec3 observer);
// Whether a seeker at `position`, carried along `forward` and looking along
// `boresight`, receives enough signal from the target inside its field of view.
bool seekerDetects(const MissileDefinition &, Vec3 position, Vec3 forward,
                   Vec3 boresight, const SensorTarget &);
Measurement updateSeeker(const MissileDefinition &, MissileState &,
                         const SensorTarget *, double dt);
void advanceMissile(const MissileDefinition &, MissileState &,
                    const SensorTarget *, const Measurement *support,
                    const Weather &, double dt);
struct Envelope {
  double minimum{}, kinematicRange{}, targetRange{}, closure{};
  bool inside{};
};
Envelope estimateEnvelope(const MissileDefinition &, const State &,
                          const Measurement &);
struct RadarDefinition {
  Vec3 position{4, 0, 0};
  double azimuth{60 * kDeg2Rad}, elevation{30 * kDeg2Rad},
      beamWidth{12 * kDeg2Rad}, range{90000}, referenceRange{55000},
      scanPeriod{2}, revisit{.1}, coastTime{3};
};
enum class RadarMode : std::uint8_t { Search, Track, MissileSupport };
struct Track {
  EntityRef entity;
  Vec3 position, velocity;
  double lastDetection{}, quality{}, age{};
};
class Radar {
public:
  RadarDefinition definition;
  RadarMode mode{RadarMode::Search};
  EntityRef selected{}, locked{};
  bool emitting{true};
  void reset();
  void update(const State &, std::span<const SensorTarget>, EntityRef self,
              double time);
  void cycle(int direction);
  bool toggleLock();
  // Selects and locks the lockable track closest to the nose.
  bool lockNearest(const State &);
  const Track *find(EntityRef) const;
  const std::vector<Track> &tracks() const { return tracks_; }
  double signal(const State &, const SensorTarget &) const;

private:
  double nextScan_{};
  std::vector<Track> tracks_;
};
} // namespace ofs::weapons
