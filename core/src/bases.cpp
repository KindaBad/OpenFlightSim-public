#include "ofs/bases.hpp"

#include <vector>

namespace ofs {

const char* structureName(StructureKind kind) {
  switch (kind) {
    case StructureKind::Command: return "command post";
    case StructureKind::Fuel: return "fuel store";
    case StructureKind::Ammunition: return "ammunition store";
    case StructureKind::Radar: return "radar";
    case StructureKind::Depot: return "supply depot";
    case StructureKind::Sam: return "missile site";
    case StructureKind::Flak: return "flak gun";
  }
  return "structure";
}

AirfieldSite outpostSite(Team team, int outpost) {
  // Level, dry ground in the low country on each side's half of the valley.
  static constexpr AirfieldSite red[kOutposts]{{10000, -6000}, {8000, 0}, {14000, 2000}};
  static constexpr AirfieldSite blue[kOutposts]{{-8000, 6000}, {-6000, 2000}, {-10000, 10000}};
  const int index = outpost < 1 ? 0 : outpost > kOutposts ? kOutposts - 1 : outpost - 1;
  return team == Team::Blue ? blue[index] : red[index];
}

std::span<const Structure> structures() {
  static const std::vector<Structure> all = [] {
    struct Placed { StructureKind kind; double north, east; int points; };
    // The depot stands east of the airfield fence, far enough apart that one
    // small bomb takes one building. The defences ring the field.
    static constexpr Placed home[]{
        {StructureKind::Command, 0, 820, 40},     {StructureKind::Fuel, 260, 700, 20},
        {StructureKind::Fuel, 260, 940, 20},      {StructureKind::Ammunition, -260, 700, 20},
        {StructureKind::Ammunition, -260, 940, 20}, {StructureKind::Radar, 0, 1150, 20},
        {StructureKind::Sam, 2000, 500, 15},      {StructureKind::Sam, -2000, 500, 15},
        {StructureKind::Flak, 700, 520, 10},      {StructureKind::Flak, -700, 520, 10},
        {StructureKind::Flak, 1500, -900, 10},    {StructureKind::Flak, -1500, -900, 10}};
    static constexpr Placed outpost[]{{StructureKind::Depot, 0, 0, 15},
                                      {StructureKind::Depot, 130, 90, 15},
                                      {StructureKind::Radar, -110, 140, 15},
                                      {StructureKind::Flak, 60, -150, 10}};
    std::vector<Structure> result;
    for (const Team team : {Team::Red, Team::Blue}) {
      const auto& field = teamAirfield(team);
      for (const auto& p : home)
        result.push_back({team, p.kind, 0, field.north + p.north, field.east + p.east, p.points});
      for (int o = 1; o <= kOutposts; ++o) {
        const auto site = outpostSite(team, o);
        for (const auto& p : outpost)
          result.push_back({team, p.kind, std::uint8_t(o), site.north + p.north, site.east + p.east, p.points});
      }
    }
    return result;
  }();
  return all;
}

}  // namespace ofs
