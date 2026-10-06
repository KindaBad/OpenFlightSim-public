#pragma once

#include "ofs/aircraft.hpp"
#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace ofs {

// ID 2 (retired temporary Falcon) is reserved and rejected. Stable wire IDs; filenames are never supplied by a network peer.
enum class AircraftType : std::uint8_t { A320 = 1, Typhoon = 3, SR71 = 4, Su57 = 5 };

struct GunConfig {
  double rpm{600}, muzzleVelocity{850}, dispersion{.0015}, damage{25};
  double lifetime{3}, range{2400};
  Vec3 muzzle{19, 0, 0}, direction{1, 0, 0};
  std::uint16_t ammo{600};
  std::uint64_t respawnDelay{480};
};

struct AircraftVisualConfig {
  Vec3 assetCg; // glTF Y-up; same asset-to-body rotation for both owned models.
  Vec3 cockpit, chaseOffset, closeChaseOffset, chaseTarget, orbitCenter;
  Vec3 exhaust[2], wingtip[2]; // Body FRD.
  double radius{}, gearSeconds{}, flapSeconds{}, wheelRadius{}, noseWheelRadius{};
  double exhaustOpacity{};
  bool cockpitGeometry{}; // Permit the authored cockpit to appear at the pilot eye.
  double exhaustRadiusScale{1}, exhaustLengthScale{1}; // Shared plume geometry.
  double cockpitPitch{}; // Optional resting eye angle, radians in body frame.
};

struct AircraftDefinition {
  AircraftType type;
  std::string_view key, displayName, modelAsset;
  AircraftConfig flight;
  AircraftVisualConfig visual;
  // Relative scaling of the established body-space collision regions.
  Vec3 hitboxScale;
  std::optional<GunConfig> gun;
  // Optional authored reduced meshes. Empty retains established clustering.
  std::array<std::string_view, 3> lodAssets{};
  struct CollisionSphere { Vec3 center; double radius{}; };
  // Optional body-space sphere chain: 8 fuselage, 4 per wing, 1 fin.
  std::array<CollisionSphere,17> collision{};
};

AircraftConfig typhoonConfig();
AircraftConfig sr71Config();
AircraftConfig su57Config();
std::span<const AircraftDefinition> aircraftDefinitions();
bool validAircraftType(AircraftType type);
AircraftType aircraftTypeFromName(std::string_view name); // throws on unknown
const AircraftDefinition& aircraftDefinition(AircraftType type);

} // namespace ofs
