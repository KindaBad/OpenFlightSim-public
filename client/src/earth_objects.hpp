#pragma once
// What stands on the round Earth (docs/WHOLE_EARTH_PLAN.md): woods and
// hedgerow trees, towns and villages, masts and chimneys, and the buildings of
// every airport. None of it is data: it is placed from the land cover, the
// places in the cover file and hashes of where things stand, so every machine
// sees the same thing. It is scenery and is not collided with.
//
// It is built a tile at a time on background threads, a tile being one node of
// the terrain pack's cube at kEarthObjectLevel (about 1.6 km across). A tile's
// contents are in its own frame: x east, y up and z south at its centre, the
// valley's axes, so the valley's tree and building drawing serves unchanged.
// The patterns they stand on are in earth_pattern.hpp.

#include "earth_scenery.hpp"
#include "landscape.hpp"
#include "scenery.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ofs::client {

inline constexpr int kEarthObjectLevel = 11;

// What each batch of a tile's buildings is made of. One draw call each.
enum class EarthPart : std::uint8_t {
  HouseWalls,  // limewash and render
  RoofTiles,   // red clay
  RoofSlate,   // grey, in the hills and the north
  Concrete,    // blocks, terminals, chimneys
  FlatRoof,
  Glass,
  Cladding,    // sheds, hangars, warehouses
  Red,         // obstruction paint
  White,
  Steel,       // masts
  Dark,        // open doors
  LampRed,
  Count
};
inline constexpr std::size_t kEarthParts = std::size_t(EarthPart::Count);

// How each part looks: plain colours weathered in the shader, as the
// airfield's are. `detail` 1 weathers, 2 adds cladding seams; `glass` is a
// reflective pane.
struct EarthPartLook {
  float color[3];  // linear
  float metallic, roughness, detail;
  float emissive;  // times the colour
  float glass;
};
EarthPartLook earthPartLook(EarthPart part);
bool earthPartCastsShadow(EarthPart part);

// A vertex of a tile's buildings with its part's look in it, so all of them
// go in one draw. The colour is stored as its square root, which keeps the
// dark parts' precision in eight bits.
struct EarthBuildingVertex {
  float x, y, z, nx, ny, nz, u, v;
  std::uint8_t color[4];  // sqrt(rgb), glass
  std::uint8_t look[4];   // metallic, roughness, detail / 2, emissive / 4
};
static_assert(sizeof(EarthBuildingVertex) == 40);

struct EarthObjectTile {
  EarthNode node;
  Vec3 centre;                   // ECEF, on the ground
  Vec3 east, up, south;          // ECEF unit axes of the tile's frame
  float radius{};                // of everything in it, about the centre
  float low{}, high{};           // its extent up and down, in the tile's frame
  std::vector<TreeInstance> trees;  // broadleaf first
  std::uint32_t broadleaf{};
  std::array<std::vector<SurfaceVertex>, kEarthParts> parts;
  std::uint32_t buildings{};
  // The parts in one, those that cast shadows first (see mergeEarthParts).
  std::vector<EarthBuildingVertex> merged;
  std::uint32_t casters{};  // vertices
};

// Fills `merged` and `casters` from `parts`, and empties `parts`.
void mergeEarthParts(EarthObjectTile& tile);

// A town or village, as its scenery sees it.
struct EarthPlace {
  Vec3 centre;         // ECEF, at sea level
  Vec3 east, south;    // ECEF unit axes of its street grid
  float radius{};      // metres
  int kind{};          // 0 village, 1 town, 2 city
  std::uint32_t seed{};
};

// An airport's own ground and buildings, laid along its longest runway.
struct EarthAirportLayout {
  int kind{};             // 1 small, 2 medium, 3 large
  float side{1};          // which side of the runway, along `across`
  float taxiOffset{};     // from the runway's centre line to the taxiway's, 0 for none
  float taxiHalfWidth{};
  float apronAlong{};     // the apron's centre, metres from the runway's centre
  float apronHalfLength{}, apronDepth{};
  bool primary{};         // the runway the airport is laid along
};

class EarthObjects {
 public:
  EarthObjects(const EarthTerrain& terrain, const EarthCover& cover);

  // Builds one tile. `treeDensity` is tree candidates per square kilometre.
  EarthObjectTile build(const EarthNode& node, int treeDensity) const;

  // Towns and villages reaching within `reach` metres of an ECEF point,
  // most important first: cities, then the nearest.
  std::vector<EarthPlace> placesNear(const Vec3& ecef, double reach) const;
  // The layout belonging to a runway of the pack.
  const EarthAirportLayout& airport(std::size_t runway) const { return layouts_[runway]; }

  // The tile holding a point on the surface, given by its normal.
  static EarthNode tileOf(const Vec3& up);

 private:
  struct Spot;
  Spot spot(int face, double s, double t) const;
  bool cleared(const Vec3& ecef, std::uint32_t nearbyRunways) const;
  std::vector<std::uint32_t> runwaysNear(const Vec3& ecef, double reach) const;
  void airportBuildings(std::uint32_t runway, EarthObjectTile& tile) const;
  void townBuildings(const EarthPlace& place, std::size_t index, const std::vector<EarthPlace>& places,
                     const std::vector<std::uint32_t>& runways, EarthObjectTile& tile) const;
  void mast(EarthObjectTile& tile) const;

  const EarthTerrain& terrain_;
  const EarthCover& cover_;
  struct Town {
    Vec3 centre, up;
    float radius{};
    std::uint32_t seed{};
  };
  std::vector<Town> towns_;
  std::vector<EarthAirportLayout> layouts_;
  std::vector<int> airportOf_;  // per runway, into cover.airports(), or -1
};

// Builds tiles on background threads, nearest first.
class EarthObjectBuilder {
 public:
  explicit EarthObjectBuilder(const EarthObjects& objects);
  ~EarthObjectBuilder();
  EarthObjectBuilder(const EarthObjectBuilder&) = delete;
  EarthObjectBuilder& operator=(const EarthObjectBuilder&) = delete;

  void request(const EarthNode& node, double priority, int treeDensity);
  void clearQueue();
  std::vector<EarthObjectTile> finished(std::size_t limit);
  std::atomic<std::uint64_t> built_{};
  std::atomic<double> buildSeconds_{};

 private:
  void work();
  const EarthObjects& objects_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::set<std::pair<double, std::uint64_t>> queue_;
  std::unordered_map<std::uint64_t, std::pair<EarthNode, int>> pending_;
  std::vector<EarthObjectTile> done_;
  bool stop_{};
  std::vector<std::thread> workers_;
};

}  // namespace ofs::client
