#include "ofs/earth_terrain.hpp"

#include "ofs/water.hpp"
#include "zstd.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace ofs {
namespace {

constexpr std::uint8_t kLand = 0, kSea = 1, kShore = 2;
constexpr double kOpenSeaBed = -50;  // open-sea tiles store no heights
constexpr std::size_t kCachedTiles = 256;
constexpr std::int16_t kDryCode = -32768;
// Region packs: their level, how many of their tiles are cached, and how many
// samples from a tile edge with no detail beyond it the detail fades out over.
constexpr int kDetailLevel = 7;
constexpr std::size_t kCachedDetail = 384;
constexpr double kDetailFade = 48;

// Face normal, right and up, as in scripts/build_earth_terrain.py.
const std::array<std::array<Vec3, 3>, 6> kFaces{{
    {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}},
    {{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}},
    {{{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}}},
    {{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}},
    {{{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}}},
    {{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}},
}};

template <class T>
T readAt(const std::vector<char>& bytes, std::size_t offset) {
  T value;
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

double smooth(double low, double high, double value) {
  const double t = std::clamp((value - low) / (high - low), 0., 1.);
  return t * t * (3 - 2 * t);
}

// Value noise in three dimensions, from integer hashing alone, -1..1.
double lattice(std::int64_t x, std::int64_t y, std::int64_t z) {
  std::uint64_t h = std::uint64_t(x) * 0x9e3779b97f4a7c15ull ^ std::uint64_t(y) * 0xc2b2ae3d27d4eb4full ^
                    std::uint64_t(z) * 0x165667b19e3779f9ull;
  h ^= h >> 31;
  h *= 0xbf58476d1ce4e5b9ull;
  h ^= h >> 27;
  h *= 0x94d049bb133111ebull;
  h ^= h >> 33;
  return double(h >> 11) * (2. / 9007199254740992.) - 1;
}
double noise(double x, double y, double z) {
  const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
  const auto ix = std::int64_t(fx), iy = std::int64_t(fy), iz = std::int64_t(fz);
  const auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
  const double u = fade(x - fx), v = fade(y - fy), w = fade(z - fz);
  const auto lerp2 = [](double a, double b, double t) { return a + (b - a) * t; };
  double plane[2];
  for (int k = 0; k < 2; ++k) {
    const double a = lerp2(lattice(ix, iy, iz + k), lattice(ix + 1, iy, iz + k), u);
    const double b = lerp2(lattice(ix, iy + 1, iz + k), lattice(ix + 1, iy + 1, iz + k), u);
    plane[k] = lerp2(a, b, v);
  }
  return lerp2(plane[0], plane[1], w);
}
// Relief finer than the pack's spacing: about -1..1. From `first` on, only the
// finer octaves: those below a region pack's spacing.
double detail(const Vec3& ecef, int first = 0) {
  double sum = 0, weight = .5, wavelength = 360;
  for (int octave = 0; octave < 5; ++octave) {
    if (octave < first) {
      weight *= .5;
      wavelength *= .5;
      continue;
    }
    const double offset = 17.3 * octave;
    sum += weight * noise(ecef.x / wavelength + offset, ecef.y / wavelength - offset, ecef.z / wavelength + 2 * offset);
    weight *= .5;
    wavelength *= .5;
  }
  return sum / .97;
}

}  // namespace

EarthTerrain::EarthTerrain(const std::filesystem::path& pack) : file_(pack, std::ios::binary) {
  if (!file_) throw std::runtime_error("Earth terrain pack not found: " + pack.string());
  std::vector<char> header(52);
  if (!file_.read(header.data(), std::streamsize(header.size())) || std::memcmp(header.data(), "OFSE", 4) != 0 ||
      readAt<std::uint16_t>(header, 4) != 2)
    throw std::runtime_error("Not an Earth terrain pack this game reads: " + pack.string());
  tile_ = readAt<std::uint16_t>(header, 6);
  roots_ = readAt<std::uint16_t>(header, 8);
  levels_ = readAt<std::uint16_t>(header, 10);
  const auto count = readAt<std::uint32_t>(header, 12);
  step_ = readAt<float>(header, 16);
  const auto runwayCount = readAt<std::uint32_t>(header, 20);
  const auto indexOffset = readAt<std::uint64_t>(header, 24);
  const auto runwayOffset = readAt<std::uint64_t>(header, 32);
  dataOffset_ = readAt<std::uint64_t>(header, 40);
  if (tile_ != 224 || roots_ != 3 || levels_ != 6 || !(step_ > 0))
    throw std::runtime_error("Unexpected Earth terrain pack layout: " + pack.string());

  // The index lists every tile in a fixed order: face, level, row, column.
  constexpr std::size_t entrySize = 31;
  std::vector<char> index(std::size_t(count) * entrySize);
  file_.seekg(std::streamoff(indexOffset));
  if (!file_.read(index.data(), std::streamsize(index.size())))
    throw std::runtime_error("Earth terrain pack index is truncated");
  entries_.resize(count);
  std::uint32_t id = 0;
  for (int face = 0; face < 6; ++face)
    for (int level = 0; level < levels_; ++level) {
      if (level == levels_ - 1) leafBase_[face] = id;
      levelBase_.push_back(id);
      const int side = roots_ << level;
      for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x, ++id) {
          if (id >= count) throw std::runtime_error("Earth terrain pack index is short");
          const std::size_t at = std::size_t(id) * entrySize;
          if (readAt<std::uint8_t>(index, at) != face || readAt<std::uint8_t>(index, at + 1) != level ||
              readAt<std::uint16_t>(index, at + 2) != x || readAt<std::uint16_t>(index, at + 4) != y)
            throw std::runtime_error("Earth terrain pack index is out of order");
          Entry& e = entries_[id];
          e.kind = readAt<std::uint8_t>(index, at + 6);
          e.heightOffset = readAt<std::uint64_t>(index, at + 7);
          e.heightSize = readAt<std::uint32_t>(index, at + 15);
          e.waterOffset = readAt<std::uint64_t>(index, at + 19);
          e.waterSize = readAt<std::uint32_t>(index, at + 27);
        }
    }

  constexpr std::size_t runwaySize = 44;
  std::vector<char> table(std::size_t(runwayCount) * runwaySize);
  file_.seekg(std::streamoff(runwayOffset));
  if (!file_.read(table.data(), std::streamsize(table.size())))
    throw std::runtime_error("Earth terrain pack runways are truncated");
  runways_.reserve(runwayCount);
  for (std::uint32_t i = 0; i < runwayCount; ++i) {
    const std::size_t at = std::size_t(i) * runwaySize;
    EarthRunway r;
    for (int end = 0; end < 2; ++end) {
      r.ends[end] = ecefFromGeodetic({readAt<double>(table, at + 16 * end), readAt<double>(table, at + 16 * end + 8), 0});
      r.altitude[end] = readAt<float>(table, at + 36 + 4 * end);
    }
    r.width = readAt<float>(table, at + 32);
    r.length = (r.ends[1] - r.ends[0]).norm();
    if (!(r.length > 1)) continue;
    r.centre = (r.ends[0] + r.ends[1]) * .5;
    r.along = (r.ends[1] - r.ends[0]) / r.length;
    const Surface s = surfaceFromEcef(r.centre);
    r.up = {s.cosLat * s.cosLon, s.cosLat * s.sinLon, s.sinLat};
    runways_.push_back(r);
  }
  indexRunways();
  // FNV-1a over the header, the index and the runways: the index holds every
  // tile's compressed size, so any other build of the pack differs here.
  std::uint64_t hash = 0xcbf29ce484222325ull;
  for (const auto* bytes : {&header, &index, &table})
    for (const char c : *bytes) hash = (hash ^ std::uint8_t(c)) * 0x100000001b3ull;
  hash_ = baseHash_ = hash;
}

EarthTerrain::~EarthTerrain() = default;

EarthTerrain::Cell EarthTerrain::cellOf(const Surface& s) const {
  const Vec3 n{s.cosLat * s.cosLon, s.cosLat * s.sinLon, s.sinLat};
  const double ax = std::abs(n.x), ay = std::abs(n.y), az = std::abs(n.z);
  const int face = az >= ax && az >= ay ? (n.z > 0 ? 4 : 5) : ax >= ay ? (n.x > 0 ? 0 : 2) : (n.y > 0 ? 1 : 3);
  const auto& f = kFaces[std::size_t(face)];
  const double depth = n.dot(f[0]);
  const double u = std::atan(n.dot(f[1]) / depth) * (4 / kPi), v = std::atan(n.dot(f[2]) / depth) * (4 / kPi);
  const int side = roots_ << (levels_ - 1);
  const double faceSamples = double(side) * tile_;
  const double fu = std::clamp((u + 1) * .5 * faceSamples, 0., faceSamples);
  const double fv = std::clamp((v + 1) * .5 * faceSamples, 0., faceSamples);
  const int x = std::min(int(fu / tile_), side - 1), y = std::min(int(fv / tile_), side - 1);
  return {leafBase_[face] + std::uint32_t(y * side + x), fu - double(x) * tile_, fv - double(y) * tile_};
}

std::vector<float> EarthTerrain::unpackHeights(std::ifstream& file, std::uint64_t at, std::uint32_t size, float step) const {
  const std::size_t samples = std::size_t(tile_ + 1) * (tile_ + 1);
  std::vector<char> packed(size);
  std::vector<std::uint8_t> raw(samples * 2);
  file.clear();
  file.seekg(std::streamoff(at));
  if (!file.read(packed.data(), size) || ZSTD_decompress(raw.data(), raw.size(), packed.data(), packed.size()) != raw.size())
    throw std::runtime_error("Earth terrain pack tile is damaged");
  // Lorenzo-predicted, zigzagged, low bytes then high bytes; the inverse is a 2D prefix sum.
  const int side = tile_ + 1;
  std::vector<std::int32_t> sums(samples);
  std::vector<float> heights(samples);
  for (int j = 0; j < side; ++j)
    for (int i = 0; i < side; ++i) {
      const std::size_t k = std::size_t(j) * side + i;
      const std::uint32_t z = std::uint32_t(raw[k]) | std::uint32_t(raw[samples + k]) << 8;
      std::int32_t value = std::int32_t(z >> 1) ^ -std::int32_t(z & 1);
      if (i) value += sums[k - 1];
      if (j) value += sums[k - side];
      if (i && j) value -= sums[k - side - 1];
      sums[k] = value;
      heights[k] = float(std::int16_t(value)) * step;
    }
  return heights;
}

std::shared_ptr<const EarthTerrain::Tile> EarthTerrain::load(std::uint32_t id) const {
  const Entry& e = entries_[id];
  auto tile = std::make_shared<Tile>();
  tile->kind = e.kind;
  const std::size_t samples = std::size_t(tile_ + 1) * (tile_ + 1);
  if (e.kind != kSea) tile->heights = unpackHeights(file_, dataOffset_ + e.heightOffset, e.heightSize, step_);
  if (e.kind == kShore) {
    std::vector<char> packed(e.waterSize);
    std::vector<std::uint8_t> raw(samples * 2);
    file_.clear();
    file_.seekg(std::streamoff(dataOffset_ + e.waterOffset));
    if (!file_.read(packed.data(), e.waterSize) ||
        ZSTD_decompress(raw.data(), raw.size(), packed.data(), packed.size()) != raw.size())
      throw std::runtime_error("Earth terrain pack tile is damaged");
    tile->water.resize(samples);
    for (std::size_t k = 0; k < samples; ++k) {
      std::int16_t code;
      std::memcpy(&code, raw.data() + 2 * k, 2);
      tile->water[k] = code == kDryCode ? float(kNoWater) : float(code) * step_;
    }
  }
  return tile;
}

std::shared_ptr<const EarthTerrain::TileData> EarthTerrain::tile(int face, int level, int x, int y) const {
  const int side = roots_ << level;
  if (face < 0 || face > 5 || level < 0 || level >= levels_ || x < 0 || y < 0 || x >= side || y >= side)
    throw std::out_of_range("no such Earth terrain tile");
  return leaf(levelBase_[std::size_t(face * levels_ + level)] + std::uint32_t(y * side + x));
}

std::shared_ptr<const EarthTerrain::Tile> EarthTerrain::leaf(std::uint32_t id) const {
  std::lock_guard lock(mutex_);
  if (const auto found = cache_.find(id); found != cache_.end()) {
    recent_.splice(recent_.begin(), recent_, found->second.second);
    return found->second.first;
  }
  auto tile = load(id);
  recent_.push_front(id);
  cache_[id] = {tile, recent_.begin()};
  if (cache_.size() > kCachedTiles) {
    cache_.erase(recent_.back());
    recent_.pop_back();
  }
  return tile;
}

void EarthTerrain::indexRunways() {
  // Each runway is listed under every leaf tile its blended surroundings reach.
  for (std::uint32_t r = 0; r < runways_.size(); ++r) {
    const EarthRunway& runway = runways_[r];
    const Vec3 across = runway.along.cross(runway.up).normalized();
    const double reach = runway.width * .5 + 300;
    std::vector<std::uint32_t> tiles;
    for (double t = -runway.length * .5 - 400; t <= runway.length * .5 + 400; t += 150)
      for (const double side : {-reach, 0., reach}) {
        const Vec3 point = runway.centre + runway.along * t + across * side;
        const std::uint32_t tile = cellOf(surfaceFromEcef(point)).tile;
        if (std::find(tiles.begin(), tiles.end(), tile) == tiles.end()) tiles.push_back(tile);
      }
    for (const auto tile : tiles) runwaysByTile_[tile].push_back(r);
  }
}

double EarthTerrain::groundAltitude(const Vec3& point, const Surface& surface) const {
  // Everything is worked out at sea level straight below, so the ground does
  // not depend on the height it is asked from.
  const Vec3 ecef = point - Vec3{surface.cosLat * surface.cosLon, surface.cosLat * surface.sinLon, surface.sinLat} * surface.altitude;
  const Cell cell = cellOf(surface);
  const auto tile = leaf(cell.tile);
  const int side = tile_ + 1;
  const int i0 = std::min(int(cell.i), tile_ - 1), j0 = std::min(int(cell.j), tile_ - 1);
  const double fi = cell.i - i0, fj = cell.j - j0;
  double base = kOpenSeaBed, water = kNoWater;
  if (!tile->heights.empty()) {
    const auto at = [&](int di, int dj) { return double(tile->heights[std::size_t(j0 + dj) * side + i0 + di]); };
    base = (at(0, 0) * (1 - fi) + at(1, 0) * fi) * (1 - fj) + (at(0, 1) * (1 - fi) + at(1, 1) * fi) * fj;
  }
  if (tile->kind == kSea) water = 0;
  if (!tile->water.empty())
    for (int dj = 0; dj < 2; ++dj)
      for (int di = 0; di < 2; ++di) water = std::max(water, double(tile->water[std::size_t(j0 + dj) * side + i0 + di]));
  // Finer relief, rougher high up, and fading out toward any water's edge.
  double amplitude = 1.5 + 22 * smooth(300, 2200, base);
  if (water > kNoWater + 1) amplitude *= smooth(water + 1, water + 12, base);
  double ground = base + amplitude * detail(ecef);
  // GLO-90 where a region is loaded, with only the noise finer than its
  // spacing, faded back to the base toward land no region covers.
  if (!detail_.empty()) {
    double weight = 0;
    const double fine = detailGround({surface.cosLat * surface.cosLon, surface.cosLat * surface.sinLon, surface.sinLat}, weight);
    if (weight > 0) ground += (fine + amplitude * detail(ecef, 3) - ground) * weight;
  }
  // Runways lie level across and straight along, blended into the land around.
  if (const auto found = runwaysByTile_.find(cell.tile); found != runwaysByTile_.end())
    for (const auto r : found->second) {
      const EarthRunway& runway = runways_[r];
      const Vec3 d = ecef - runway.centre;
      const double along = d.dot(runway.along);
      const Vec3 flat = d - runway.along * along - runway.up * d.dot(runway.up);
      const double inner = runway.width * .5 + 40;
      const double beyond = std::max(0., std::abs(along) - (runway.length * .5 + 120));
      const double weight = (1 - smooth(inner, inner + 250, flat.norm())) * (1 - smooth(0, 250, beyond));
      if (weight <= 0) continue;
      const double t = std::clamp(along / runway.length + .5, 0., 1.);
      ground += (runway.altitude[0] + (runway.altitude[1] - runway.altitude[0]) * t - ground) * weight;
    }
  return ground;
}

double EarthTerrain::waterAltitude(const Surface& surface) const {
  const Cell cell = cellOf(surface);
  const auto tile = leaf(cell.tile);
  if (tile->kind == kSea) return 0;
  if (tile->water.empty()) return kNoWater;
  const int side = tile_ + 1;
  const int i0 = std::min(int(cell.i), tile_ - 1), j0 = std::min(int(cell.j), tile_ - 1);
  double water = kNoWater;
  for (int dj = 0; dj < 2; ++dj)
    for (int di = 0; di < 2; ++di) water = std::max(water, double(tile->water[std::size_t(j0 + dj) * side + i0 + di]));
  return water;
}

// ---------------------------------------------------------------------------
// Region packs
// ---------------------------------------------------------------------------

std::filesystem::path EarthTerrain::detailFolder(const std::filesystem::path& pack) { return pack.parent_path() / "detail"; }

std::filesystem::path EarthTerrain::detailPath(const std::filesystem::path& pack, const std::string& region) {
  return detailFolder(pack) / ("earth-detail-" + region + ".ofsd");
}

std::uint64_t EarthTerrain::detailKey(int face, int x, int y) {
  return std::uint64_t(face) << 40 | std::uint64_t(std::uint32_t(y)) << 20 | std::uint64_t(std::uint32_t(x));
}

void EarthTerrain::addDetail(const std::filesystem::path& regionPack) {
  auto file = std::make_unique<std::ifstream>(regionPack, std::ios::binary);
  if (!*file) throw std::runtime_error("Earth detail region not found: " + regionPack.string());
  std::vector<char> header(44);
  if (!file->read(header.data(), std::streamsize(header.size())) || std::memcmp(header.data(), "OFSD", 4) != 0 ||
      readAt<std::uint16_t>(header, 4) != 1)
    throw std::runtime_error("Not an Earth detail region this game reads: " + regionPack.string());
  if (readAt<std::uint16_t>(header, 6) != tile_ || readAt<std::uint16_t>(header, 8) != roots_ ||
      readAt<std::uint16_t>(header, 10) != kDetailLevel)
    throw std::runtime_error("Unexpected Earth detail region layout: " + regionPack.string());
  const auto count = readAt<std::uint32_t>(header, 12);
  DetailPack pack;
  pack.step = readAt<float>(header, 16);
  pack.region.assign(header.data() + 20, 8);
  pack.region.erase(pack.region.find_last_not_of('\0') + 1);
  const auto indexOffset = readAt<std::uint64_t>(header, 28);
  pack.dataOffset = readAt<std::uint64_t>(header, 36);
  if (!(pack.step > 0) || pack.region.empty()) throw std::runtime_error("Earth detail region header is damaged: " + regionPack.string());
  std::lock_guard lock(mutex_);
  for (const auto& loaded : detailPacks_)
    if (loaded.region == pack.region) throw std::runtime_error("Earth detail region loaded twice: " + pack.region);
  constexpr std::size_t entrySize = 21;
  std::vector<char> index(std::size_t(count) * entrySize);
  file->seekg(std::streamoff(indexOffset));
  if (!file->read(index.data(), std::streamsize(index.size()))) throw std::runtime_error("Earth detail region index is truncated");
  const int side = roots_ << kDetailLevel;
  for (std::uint32_t k = 0; k < count; ++k) {
    const std::size_t at = std::size_t(k) * entrySize;
    const int face = readAt<std::uint8_t>(index, at);
    const auto x = readAt<std::uint32_t>(index, at + 1), y = readAt<std::uint32_t>(index, at + 5);
    if (face > 5 || x >= std::uint32_t(side) || y >= std::uint32_t(side))
      throw std::runtime_error("Earth detail region index is damaged");
    detail_[detailKey(face, int(x), int(y))] = {std::uint16_t(detailPacks_.size()), readAt<std::uint32_t>(index, at + 17),
                                                pack.dataOffset + readAt<std::uint64_t>(index, at + 9)};
  }
  std::uint64_t hash = 0xcbf29ce484222325ull;
  for (const auto* bytes : {&header, &index})
    for (const char c : *bytes) hash = (hash ^ std::uint8_t(c)) * 0x100000001b3ull;
  pack.hash = hash;
  pack.file = std::move(file);
  detailPacks_.push_back(std::move(pack));
  // The base pack's hash, then each region's in order of name.
  std::vector<const DetailPack*> byName;
  for (const auto& loaded : detailPacks_) byName.push_back(&loaded);
  std::sort(byName.begin(), byName.end(), [](const DetailPack* a, const DetailPack* b) { return a->region < b->region; });
  hash = baseHash_;
  for (const auto* loaded : byName)
    for (int b = 0; b < 8; ++b) hash = (hash ^ ((loaded->hash >> (8 * b)) & 0xff)) * 0x100000001b3ull;
  hash_ = hash;
  detailCache_.clear();
  detailRecent_.clear();
}

std::vector<std::string> EarthTerrain::detailRegions() const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> names;
  for (const auto& pack : detailPacks_) names.push_back(pack.region);
  std::sort(names.begin(), names.end());
  return names;
}

EarthTerrain::DetailCell EarthTerrain::detailCellOf(const Vec3& n) const {
  const double ax = std::abs(n.x), ay = std::abs(n.y), az = std::abs(n.z);
  const int face = az >= ax && az >= ay ? (n.z > 0 ? 4 : 5) : ax >= ay ? (n.x > 0 ? 0 : 2) : (n.y > 0 ? 1 : 3);
  const auto& f = kFaces[std::size_t(face)];
  const double depth = n.dot(f[0]);
  const double u = std::atan(n.dot(f[1]) / depth) * (4 / kPi), v = std::atan(n.dot(f[2]) / depth) * (4 / kPi);
  const int side = roots_ << kDetailLevel;
  const double faceSamples = double(side) * tile_;
  const double fu = std::clamp((u + 1) * .5 * faceSamples, 0., faceSamples);
  const double fv = std::clamp((v + 1) * .5 * faceSamples, 0., faceSamples);
  const int x = std::min(int(fu / tile_), side - 1), y = std::min(int(fv / tile_), side - 1);
  return {detailKey(face, x, y), fu - double(x) * tile_, fv - double(y) * tile_};
}

std::shared_ptr<const EarthTerrain::DetailTile> EarthTerrain::detailTile(std::uint64_t key) const {
  std::lock_guard lock(mutex_);
  if (const auto found = detailCache_.find(key); found != detailCache_.end()) {
    detailRecent_.splice(detailRecent_.begin(), detailRecent_, found->second.second);
    return found->second.first;
  }
  const auto entry = detail_.find(key);
  if (entry == detail_.end()) return nullptr;
  const DetailPack& pack = detailPacks_[entry->second.pack];
  auto tile = std::make_shared<DetailTile>();
  tile->heights = unpackHeights(*pack.file, entry->second.offset, entry->second.size, pack.step);
  // Which of the eight neighbours have no detail: found from a point a tile
  // over, so neighbours across a face's edge are found too.
  const int face = int(key >> 40), y = int(key >> 20 & 0xfffff), x = int(key & 0xfffff);
  const auto& f = kFaces[std::size_t(face)];
  const double side = double(roots_ << kDetailLevel);
  int bit = 0;
  for (int dj = -1; dj <= 1; ++dj)
    for (int di = -1; di <= 1; ++di) {
      if (!di && !dj) continue;
      const double u = -1 + 2 * (x + .5 + di) / side, v = -1 + 2 * (y + .5 + dj) / side;
      const Vec3 n = (f[0] + f[1] * std::tan(u * kPi / 4) + f[2] * std::tan(v * kPi / 4)).normalized();
      if (!detail_.contains(detailCellOf(n).key)) tile->absent |= std::uint8_t(1 << bit);
      ++bit;
    }
  detailRecent_.push_front(key);
  detailCache_[key] = {tile, detailRecent_.begin()};
  if (detailCache_.size() > kCachedDetail) {
    detailCache_.erase(detailRecent_.back());
    detailRecent_.pop_back();
  }
  return tile;
}

double EarthTerrain::detailGround(const Vec3& normal, double& weight) const {
  const DetailCell cell = detailCellOf(normal);
  const auto tile = detailTile(cell.key);
  weight = 0;
  if (!tile) return 0;
  weight = 1;
  if (tile->absent) {
    const double far = tile_;
    int bit = 0;
    for (int dj = -1; dj <= 1; ++dj)
      for (int di = -1; di <= 1; ++di) {
        if (!di && !dj) continue;
        if (tile->absent >> bit++ & 1) {
          const double dx = di < 0 ? cell.i : di > 0 ? far - cell.i : 1e9;
          const double dy = dj < 0 ? cell.j : dj > 0 ? far - cell.j : 1e9;
          const double away = di && dj ? std::max(dx, dy) : std::min(dx, dy);
          weight = std::min(weight, smooth(0, kDetailFade, away));
        }
      }
    if (weight <= 0) return 0;
  }
  const int side = tile_ + 1;
  const int i0 = std::min(int(cell.i), tile_ - 1), j0 = std::min(int(cell.j), tile_ - 1);
  const double fi = cell.i - i0, fj = cell.j - j0;
  const auto at = [&](int di, int dj) { return double(tile->heights[std::size_t(j0 + dj) * side + i0 + di]); };
  return (at(0, 0) * (1 - fi) + at(1, 0) * fi) * (1 - fj) + (at(0, 1) * (1 - fi) + at(1, 1) * fi) * fj;
}

}  // namespace ofs
