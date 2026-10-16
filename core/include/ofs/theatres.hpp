#pragma once
// Where games on the round Earth are played (docs/WHOLE_EARTH_PLAN.md,
// phase 5). A theatre is an anchor, the origin of the game frame, and the
// airports players start at. Positions are OurAirports' reference points; the
// start is the longest runway of the pack near each one.

#include "earth_terrain.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ofs {

struct TheatreAirport {
  std::string_view ident, name;
  double latitude{}, longitude{};  // degrees
};

struct Theatre {
  std::string_view id, name;
  double latitude{}, longitude{};  // degrees, of the anchor
  std::span<const TheatreAirport> airports;
};

std::span<const Theatre> theatres();
// Null when there is no such theatre.
const Theatre* findTheatre(std::string_view id);
// Null when the theatre has no such airport.
const TheatreAirport* findTheatreAirport(const Theatre& theatre, std::string_view ident);

// The longest runway within 5 km of an airport, or null.
const EarthRunway* theatreRunway(const EarthTerrain& terrain, const TheatreAirport& airport);

// Where an aircraft starts at a runway: on its centre line, `fromEnd` metres in
// from the end it takes off from (ECEF, at sea level), and the way it points.
struct RunwayStart {
  Vec3 point, along;
};
RunwayStart runwayStart(const EarthRunway& runway, double fromEnd = 300);

// The detail regions a theatre's games use: every square of 10 degrees that a
// box 4.5 degrees of latitude (500 km) either side of its anchor, and as many
// kilometres east and west, touches. Named by their south-west corner
// (N40E010). Mirrors theatre_regions in scripts/build_earth_terrain.py.
std::vector<std::string> theatreRegions(const Theatre& theatre);
std::string earthRegionName(double latitudeDeg, double longitudeDeg);

// Loads the region packs beside a terrain pack: a theatre's regions that are
// there, or with no theatre (free flight alone) every one there. Server and
// players load the same ones for a theatre, so a player missing one of the
// server's has another content hash and is turned away. Returns those loaded.
std::vector<std::string> loadEarthDetail(EarthTerrain& terrain, const std::filesystem::path& pack, const Theatre* theatre);

}  // namespace ofs
