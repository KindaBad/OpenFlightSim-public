#pragma once
// The round Earth's land and water as meshes, for the renderer
// (docs/WHOLE_EARTH_PLAN.md, phase 4).
//
// The planet is drawn as a quadtree on the same equal-angle cube as the terrain
// pack. A chunk is a 33 x 33 grid of vertices over one node. Down to the pack's
// leaf level (about 100 km a node) a chunk's vertices are the pack's own stored
// samples for that level, seven apart; below it, they come from the same ground
// function the simulation uses, so close to the aircraft the drawn land is the
// land it stands on. Chunks are built on a background thread; the renderer
// keeps the nodes the camera needs and draws a coarser one until finer ones are
// ready. Skirts hang from every edge so neighbours of different detail meet
// without gaps.
#include "ofs/earth_terrain.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ofs::client {

// The terrain pack's equal-angle cube. A point on face `face` at (s, t), each
// 0..1 across it, lies along the unit surface normal earthFaceDirection gives;
// outside 0..1 the face extends past its edge.
Vec3 earthFaceDirection(int face, double s, double t);
// The face a surface normal lies on, and where on it.
void earthFaceOf(const Vec3& up, int& face, double& s, double& t);
// The surface whose normal is `up`, and its point at sea level.
Surface earthSurfaceOf(const Vec3& up);
Vec3 earthSeaLevel(const Surface& surface);

// An airport with a runway in the pack (OurAirports).
struct EarthAirport {
  double latitude{}, longitude{};  // radians
  int kind{};                      // 1 small, 2 medium, 3 large
  std::string ident, name;
};
// A town or city (Natural Earth's populated places).
struct EarthTown {
  double latitude{}, longitude{};  // radians
  float population{};
};

// The colour of the land from its cover: Natural Earth II, about 2 km a pixel,
// and the places scenery is built around (scripts/build_earth_terrain.py cover).
class EarthCover {
 public:
  explicit EarthCover(const std::filesystem::path& file);
  // sRGB colour at a latitude and longitude in radians, packed ABGR.
  std::uint32_t colour(double latitude, double longitude) const;
  // Empty for a version 1 file.
  const std::vector<EarthAirport>& airports() const { return airports_; }
  const std::vector<EarthTown>& towns() const { return towns_; }

 private:
  int width_{}, height_{};
  std::vector<std::uint8_t> rgb_;
  std::vector<EarthAirport> airports_;
  std::vector<EarthTown> towns_;
};

struct EarthVertex {
  float x, y, z;        // ECEF metres from the chunk's centre
  float nx, ny, nz;     // ECEF unit normal
  float ground, water;  // altitudes; water -10000 where dry
  float u, v;           // metres along the face, for detail textures
  std::uint32_t cover;  // sRGB ABGR
};

struct EarthNode {
  std::uint8_t face{}, level{};
  std::uint32_t x{}, y{};
  bool operator==(const EarthNode&) const = default;
  std::uint64_t key() const { return std::uint64_t(face) << 56 | std::uint64_t(level) << 48 | std::uint64_t(x) << 24 | y; }
  EarthNode child(int i) const {
    return {face, std::uint8_t(level + 1), x * 2 + std::uint32_t(i & 1), y * 2 + std::uint32_t(i >> 1)};
  }
};

struct EarthChunkMesh {
  EarthNode node;
  Vec3 centre;           // ECEF
  double radius{};       // of a sphere about the centre holding every vertex
  float patternU{}, patternV{};  // pattern coordinates of the texture coordinates' origin (earth_pattern.hpp)
  std::vector<EarthVertex> vertices;
};

inline constexpr int kEarthChunkIntervals = 32;
inline constexpr int kEarthChunkSide = kEarthChunkIntervals + 1;
inline constexpr int kEarthMaxLevel = 13;  // about 13 m between vertices

class EarthChunkBuilder {
 public:
  EarthChunkBuilder(const EarthTerrain& terrain, const EarthCover& cover);
  ~EarthChunkBuilder();
  EarthChunkBuilder(const EarthChunkBuilder&) = delete;
  EarthChunkBuilder& operator=(const EarthChunkBuilder&) = delete;

  // Asks for a chunk; lower priority values come first.
  void request(const EarthNode& node, double priority);
  // Drops requests not yet started; the renderer asks again each frame for
  // what it still needs.
  void clearQueue();
  // Chunks finished since the last call, at most `limit`.
  std::vector<EarthChunkMesh> finished(std::size_t limit);
  // Builds one chunk on the calling thread.
  EarthChunkMesh build(const EarthNode& node) const;
  // Indices shared by every chunk: the grid, then the skirts.
  static std::vector<std::uint16_t> indices();
  // Where a node lies, before it is built: its centre at sea level and its edge length.
  static Vec3 nodeCentre(const EarthNode& node);
  static double nodeSize(int level);

 private:
  void work();
  const EarthTerrain& terrain_;
  const EarthCover& cover_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::set<std::pair<double, std::uint64_t>> queue_;
  std::unordered_map<std::uint64_t, EarthNode> pending_;
  std::vector<EarthChunkMesh> done_;
  bool stop_{};
 public:
  // Chunks built so far, and the seconds spent building them.
  std::atomic<std::uint64_t> built_{};
  std::atomic<double> buildSeconds_{};
 private:
  std::vector<std::thread> workers_;
};

}  // namespace ofs::client
