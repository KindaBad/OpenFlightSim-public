#pragma once
// The airfield, as geometry.
//
// Everything here lies on the level ground the simulation keeps around the
// origin (ofs/terrain.hpp), in renderer axes: +X east, +Y up, +Z south. The
// runway runs north-south through the origin, where a flight starts. Building
// the airfield is pure computation, so it is done without a renderer and its
// layout can be checked by a test.

#include "scenery.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace ofs::client {

// Ground that is drawn into the terrain: paving, and the paint on it.
struct AirfieldGround {
  std::vector<SurfaceVertex> runway;       // asphalt
  std::vector<SurfaceVertex> taxiways;     // asphalt, a shade lighter
  std::vector<SurfaceVertex> concrete;     // aprons, shoulders and hardstands
  std::vector<SurfaceVertex> roads;        // asphalt, outside the fence
  std::vector<SurfaceVertex> whitePaint;   // runway and stand markings
  std::vector<SurfaceVertex> yellowPaint;  // taxiway lines and chevrons
};

// What a batch of solid structure is made of. One draw call each.
enum class AirfieldMaterial : std::uint8_t {
  Concrete,   // walls, shelters' aprons, bunds
  Cladding,   // hangar and warehouse sheet metal
  Roof,       // dark roofing
  Shelter,    // hardened shelters, weathered grey-green
  Glass,      // glazing
  Dark,       // open doors and what is behind them
  White,      // tanks, the radome, markings on structures
  Red,        // obstruction paint, fire station doors, the windsock
  Steel,      // masts, fence and lattice
  Olive,      // military vehicles
  Yellow,     // ground equipment
  CarLight, CarDark,
  LightWhite, LightAmber, LightRed, LightGreen, LightBlue,
  Count
};

// Lamps, glass and the dark of an open doorway throw no shadow worth drawing.
inline bool airfieldCastsShadow(AirfieldMaterial material) {
  return material != AirfieldMaterial::Glass && material != AirfieldMaterial::Dark && material < AirfieldMaterial::LightWhite;
}

struct AirfieldPart {
  AirfieldMaterial material{AirfieldMaterial::Concrete};
  std::vector<SurfaceVertex> vertices;
};

// A footprint on the ground, east and south, for checking the layout.
struct AirfieldBox {
  float x0, z0, x1, z1;
  const char* name;
};

struct Airfield {
  AirfieldGround ground;
  std::array<AirfieldPart, std::size_t(AirfieldMaterial::Count)> parts;
  // Footprints of every solid building, and of every strip an aircraft taxis on.
  std::vector<AirfieldBox> buildings, movement;
};

Airfield buildAirfield();

// The runway itself, shared with anything that needs to know where it is.
inline constexpr float kRunwayHalfWidth = 22.5f, kRunwayHalfLength = 1300.f;
// The fence line: nothing grows inside it.
inline constexpr float kFenceWest = -720, kFenceEast = 340, kFenceNorth = -1740, kFenceSouth = 1740;

// How the ground at a point is used, for the map and for where trees stand.
enum class AirfieldUse : std::uint8_t { Outside, Grass, Paved };
AirfieldUse airfieldUse(double north, double east);

}  // namespace ofs::client
