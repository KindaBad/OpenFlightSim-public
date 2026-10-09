#pragma once
// The two sides of a team game and what each holds on the ground.
//
// Red and Blue each have a home airfield (ofs/terrain.hpp) with a depot beside
// it, and three outposts out in the country between the two. Everything that
// can be bombed is listed here, in one fixed order, so the server that keeps
// score and every client that draws the map agree on it without sending it.
#include "ofs/terrain.hpp"
#include <cstdint>
#include <span>

namespace ofs {

enum class Team : std::uint8_t { None, Red, Blue };
inline const char* teamName(Team team) {
  return team == Team::Red ? "Red" : team == Team::Blue ? "Blue" : "Nobody";
}
inline Team opponent(Team team) {
  return team == Team::Red ? Team::Blue : team == Team::Blue ? Team::Red : Team::None;
}
// The home airfield of a side; the origin airfield belongs to nobody.
inline const AirfieldSite& teamAirfield(Team team) {
  return kAirfieldSites[team == Team::Red ? 1 : team == Team::Blue ? 2 : 0];
}
// Whose airfield a point stands on, within its level ground.
inline Team airfieldOwner(double north, double east) {
  for (const Team team : {Team::Red, Team::Blue})
    if (std::hypot(north - teamAirfield(team).north, east - teamAirfield(team).east) < 3000.) return team;
  return Team::None;
}

enum class StructureKind : std::uint8_t { Command, Fuel, Ammunition, Radar, Depot, Sam, Flak };
struct Structure {
  Team team;
  StructureKind kind;
  std::uint8_t site;  // 0 is the home base, 1 to 3 the outposts
  double north, east;
  // What a side scores for destroying it.
  int points;
};
const char* structureName(StructureKind);
// Every structure of both sides: Red's, then Blue's. At most 64.
std::span<const Structure> structures();
inline constexpr int kOutposts = 3;
// The middle of a side's outpost, 1 to kOutposts.
AirfieldSite outpostSite(Team, int outpost);
// Points for shooting down an aircraft of the other side.
inline constexpr int kKillPoints = 10;
// Seconds before a destroyed structure stands again.
inline constexpr double kStructureRebuildSeconds = 240;

// Base defences. A flak gun engages inside its range; a missile site launches
// at aircraft above its floor and reloads between shots.
inline constexpr double kFlakRange = 2600, kSamRange = 11000, kSamFloor = 120, kSamReloadSeconds = 22;

}  // namespace ofs
