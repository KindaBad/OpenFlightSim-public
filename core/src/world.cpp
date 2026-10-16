#include "ofs/world.hpp"

#include "ofs/terrain.hpp"
#include "ofs/water.hpp"

namespace ofs {

Ground FlatWorld::terrain(const Vec3& p) const {
  const TerrainSample sample = sampleTerrain(p.x, p.y);
  return {-sample.heightNed, sample.normalNed};
}

double FlatWorld::waterAltitude(const Vec3& p) const { return waterSurfaceElevation(p.x, p.y); }

namespace {
const FlatWorld valley;
const WorldShape* current = &valley;
}  // namespace

const WorldShape& world() { return *current; }
void useWorld(const WorldShape& world) { current = &world; }

}  // namespace ofs
