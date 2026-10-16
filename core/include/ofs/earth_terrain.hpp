#pragma once
// The Earth's land and water, read from the pack scripts/build_earth_terrain.py
// builds (docs/WHOLE_EARTH_PLAN.md).
//
// The pack holds heights on an equal-angle cube over the planet, ~465 m apart,
// with the levels of the sea and of every lake where there is water, and the
// world's runways. The ground at a point is the pack's heights interpolated,
// with finer relief added by deterministic noise, and laid level along each
// runway. Every machine with the same pack computes the same ground.
//
// Heights are taken as metres above the WGS84 ellipsoid: the source's sea level
// (the geoid) is the ellipsoid here, so the sea is at altitude 0 everywhere.
//
// Region packs (scripts/build_earth_terrain.py detail) add Copernicus GLO-90
// heights, ~116 m apart, over squares of 10 degrees. Where one is loaded the
// ground follows them, with only the finest noise added, and blends back to
// the base heights within a few kilometres of land the regions do not cover.
// The content hash covers the regions loaded, so every machine in a game must
// load the same ones.
#include "earth.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ofs {

struct EarthRunway {
  Vec3 ends[2];          // ECEF, at sea level under each end
  double altitude[2]{};  // of the runway surface at each end
  double width{}, length{};
  Vec3 centre, along, up;  // ECEF; unit vectors
};

class EarthTerrain {
 public:
  // Throws std::runtime_error if the pack is missing or not a version this reads.
  explicit EarthTerrain(const std::filesystem::path& pack);
  ~EarthTerrain();

  // The ground's altitude under an ECEF point whose surface normal is given.
  double groundAltitude(const Vec3& ecef, const Surface& surface) const;
  // The water's altitude there, or kNoWater where there is none.
  double waterAltitude(const Surface& surface) const;

  // One tile of the pack at any level: 225 x 225 samples, rows along the
  // face's up axis, as built. Heights are empty for open sea (bed -50 m,
  // water 0); water is empty where there is none, kNoWater where dry.
  struct TileData {
    std::uint8_t kind{};  // 0 land, 1 open sea, 2 shore
    std::vector<float> heights, water;
  };
  std::shared_ptr<const TileData> tile(int face, int level, int x, int y) const;
  int tileSamples() const { return tile_ + 1; }
  int levels() const { return levels_; }
  int roots() const { return roots_; }

  const std::vector<EarthRunway>& runways() const { return runways_; }
  // Identifies the pack and the regions loaded: two machines with the same
  // hash have the same ground.
  std::uint64_t contentHash() const { return hash_; }

  // Loads a region pack. Throws std::runtime_error if it is not one this
  // reads, or was built for another layout. Load regions before the ground is
  // first asked for: tiles already cached do not change.
  void addDetail(const std::filesystem::path& regionPack);
  // The regions loaded, by name (N40E010), in order.
  std::vector<std::string> detailRegions() const;
  // Where region packs are kept for a pack: a `detail` folder beside it, each
  // named earth-detail-<region>.ofsd.
  static std::filesystem::path detailFolder(const std::filesystem::path& pack);
  static std::filesystem::path detailPath(const std::filesystem::path& pack, const std::string& region);
  std::size_t tileCount() const { return entries_.size(); }

 private:
  struct Entry {
    std::uint8_t kind{};
    std::uint64_t heightOffset{}, waterOffset{};
    std::uint32_t heightSize{}, waterSize{};
  };
  using Tile = TileData;
  struct Cell {
    std::uint32_t tile{};
    double i{}, j{};  // sample coordinates within the leaf tile, 0..224
  };
  Cell cellOf(const Surface& surface) const;
  std::shared_ptr<const Tile> leaf(std::uint32_t id) const;
  std::shared_ptr<const Tile> load(std::uint32_t id) const;
  void indexRunways();
  std::vector<float> unpackHeights(std::ifstream& file, std::uint64_t at, std::uint32_t size, float step) const;

  // Region packs.
  struct DetailPack {
    std::string region;
    std::unique_ptr<std::ifstream> file;
    std::uint64_t dataOffset{}, hash{};
    float step{};
  };
  struct DetailEntry {
    std::uint16_t pack{};
    std::uint32_t size{};
    std::uint64_t offset{};
  };
  struct DetailTile {
    std::vector<float> heights;
    std::uint8_t absent{};  // neighbours with no detail, a bit for each of the eight
  };
  struct DetailCell {
    std::uint64_t key{};
    double i{}, j{};
  };
  static std::uint64_t detailKey(int face, int x, int y);
  DetailCell detailCellOf(const Vec3& normal) const;
  std::shared_ptr<const DetailTile> detailTile(std::uint64_t key) const;
  double detailGround(const Vec3& normal, double& weight) const;
  std::uint64_t baseHash_{};
  std::vector<DetailPack> detailPacks_;
  std::unordered_map<std::uint64_t, DetailEntry> detail_;
  mutable std::list<std::uint64_t> detailRecent_;
  mutable std::unordered_map<std::uint64_t, std::pair<std::shared_ptr<const DetailTile>, std::list<std::uint64_t>::iterator>> detailCache_;

  int tile_{}, roots_{}, levels_{};
  std::uint64_t hash_{};
  float step_{};
  std::uint64_t dataOffset_{};
  std::vector<Entry> entries_;  // by tile id: face, then level, then row, then column
  std::uint32_t leafBase_[6]{};
  std::vector<std::uint32_t> levelBase_;  // first tile id of each face and level
  std::vector<EarthRunway> runways_;
  std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> runwaysByTile_;

  mutable std::mutex mutex_;
  mutable std::ifstream file_;
  mutable std::list<std::uint32_t> recent_;
  mutable std::unordered_map<std::uint32_t, std::pair<std::shared_ptr<const Tile>, std::list<std::uint32_t>::iterator>> cache_;
};

}  // namespace ofs
