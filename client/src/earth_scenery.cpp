#include "earth_scenery.hpp"

#include "earth_pattern.hpp"
#include "ofs/water.hpp"
#include "zstd.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace ofs::client {
namespace {

// Face normal, right and up, as in scripts/build_earth_terrain.py.
const std::array<std::array<Vec3, 3>, 6> kFaces{{
    {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}},
    {{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}},
    {{{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}}},
    {{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}},
    {{{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}}},
    {{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}},
}};
constexpr int kRoots = 3, kPackLevels = 6, kPackIntervals = 224;
constexpr double kQuarterMeridian = 10001966.;
constexpr float kDry = -10000.f;

Vec3 faceDirection(int face, double s, double t) { return earthFaceDirection(face, s, t); }
Surface surfaceOf(const Vec3& n) { return earthSurfaceOf(n); }
Vec3 seaLevel(const Surface& s) { return earthSeaLevel(s); }

}  // namespace

Vec3 earthFaceDirection(int face, double s, double t) {
  const auto& f = kFaces[std::size_t(face)];
  const double u = std::tan((2 * s - 1) * kPi / 4), v = std::tan((2 * t - 1) * kPi / 4);
  return (f[0] + f[1] * u + f[2] * v).normalized();
}

void earthFaceOf(const Vec3& up, int& face, double& s, double& t) {
  face = 0;
  double best = -2;
  for (int i = 0; i < 6; ++i)
    if (const double d = up.dot(kFaces[std::size_t(i)][0]); d > best) best = d, face = i;
  const auto& f = kFaces[std::size_t(face)];
  s = (std::atan(up.dot(f[1]) / best) * 4 / kPi + 1) * .5;
  t = (std::atan(up.dot(f[2]) / best) * 4 / kPi + 1) * .5;
}

Surface earthSurfaceOf(const Vec3& n) {
  Surface s;
  s.sinLat = n.z;
  s.cosLat = std::hypot(n.x, n.y);
  if (s.cosLat > 0) {
    s.sinLon = n.y / s.cosLat;
    s.cosLon = n.x / s.cosLat;
  }
  return s;
}

Vec3 earthSeaLevel(const Surface& s) {
  const double n = kEarthEquatorialRadius / std::sqrt(1 - kEarthEccentricity2 * s.sinLat * s.sinLat);
  return {n * s.cosLat * s.cosLon, n * s.cosLat * s.sinLon, n * (1 - kEarthEccentricity2) * s.sinLat};
}

EarthCover::EarthCover(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("Earth land cover not found: " + file.string());
  char header[32];
  if (!in.read(header, 16) || std::memcmp(header, "OFSC", 4) != 0)
    throw std::runtime_error("Not an Earth land cover file: " + file.string());
  std::uint16_t version;
  std::uint32_t width, height;
  std::memcpy(&version, header + 4, 2);
  std::memcpy(&width, header + 8, 4);
  std::memcpy(&height, header + 12, 4);
  std::uint64_t colourSize = 0, placesSize = 0;
  if (version >= 2) {
    if (!in.read(header + 16, 16)) throw std::runtime_error("Earth land cover is damaged: " + file.string());
    std::memcpy(&colourSize, header + 16, 8);
    std::memcpy(&placesSize, header + 24, 8);
  }
  std::vector<char> packed((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (version < 2) colourSize = packed.size();
  if (colourSize + placesSize > packed.size()) throw std::runtime_error("Earth land cover is damaged: " + file.string());
  width_ = int(width);
  height_ = int(height);
  rgb_.resize(std::size_t(width) * height * 3);
  if (ZSTD_decompress(rgb_.data(), rgb_.size(), packed.data(), colourSize) != rgb_.size())
    throw std::runtime_error("Earth land cover is damaged: " + file.string());
  if (placesSize == 0) return;
  const char* blob = packed.data() + colourSize;
  const unsigned long long full = ZSTD_getFrameContentSize(blob, placesSize);
  if (full == ZSTD_CONTENTSIZE_ERROR || full == ZSTD_CONTENTSIZE_UNKNOWN || full < 8 || full > (1ull << 30))
    throw std::runtime_error("Earth places are damaged: " + file.string());
  std::vector<char> places(full);
  if (ZSTD_decompress(places.data(), places.size(), blob, placesSize) != places.size())
    throw std::runtime_error("Earth places are damaged: " + file.string());
  std::uint32_t airports, towns;
  std::memcpy(&airports, places.data(), 4);
  std::memcpy(&towns, places.data() + 4, 4);
  constexpr std::size_t kAirport = 8 + 8 + 1 + 8 + 40, kTown = 8 + 8 + 4;
  if (8 + airports * kAirport + towns * kTown != places.size()) throw std::runtime_error("Earth places are damaged: " + file.string());
  const char* at = places.data() + 8;
  const auto text = [](const char* p, std::size_t n) { return std::string(p, strnlen(p, n)); };
  airports_.reserve(airports);
  for (std::uint32_t i = 0; i < airports; ++i, at += kAirport) {
    EarthAirport a;
    std::memcpy(&a.latitude, at, 8);
    std::memcpy(&a.longitude, at + 8, 8);
    a.kind = std::uint8_t(at[16]);
    a.ident = text(at + 17, 8);
    a.name = text(at + 25, 40);
    airports_.push_back(std::move(a));
  }
  towns_.reserve(towns);
  for (std::uint32_t i = 0; i < towns; ++i, at += kTown) {
    EarthTown t;
    std::memcpy(&t.latitude, at, 8);
    std::memcpy(&t.longitude, at + 8, 8);
    std::memcpy(&t.population, at + 16, 4);
    towns_.push_back(t);
  }
}

std::uint32_t EarthCover::colour(double latitude, double longitude) const {
  const double x = (longitude / kPi + 1) * .5 * width_ - .5;
  const double y = (.5 - latitude / kPi) * height_ - .5;
  const int x0 = int(std::floor(x)), y0 = std::clamp(int(std::floor(y)), 0, height_ - 2);
  const double fx = x - x0, fy = std::clamp(y - y0, 0., 1.);
  std::uint32_t out = 0xff000000u;
  for (int c = 0; c < 3; ++c) {
    const auto at = [&](int dx, int dy) {
      const int col = ((x0 + dx) % width_ + width_) % width_;
      return double(rgb_[(std::size_t(y0 + dy) * width_ + col) * 3 + c]);
    };
    const double value = (at(0, 0) * (1 - fx) + at(1, 0) * fx) * (1 - fy) + (at(0, 1) * (1 - fx) + at(1, 1) * fx) * fy;
    out |= std::uint32_t(std::clamp(std::lround(value), 0l, 255l)) << (8 * c);
  }
  return out;
}

EarthChunkBuilder::EarthChunkBuilder(const EarthTerrain& terrain, const EarthCover& cover) : terrain_(terrain), cover_(cover) {
  const unsigned count = std::clamp(std::thread::hardware_concurrency() / 4, 1u, 3u);
  for (unsigned i = 0; i < count; ++i) workers_.emplace_back([this] { work(); });
}

EarthChunkBuilder::~EarthChunkBuilder() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  wake_.notify_all();
  for (auto& worker : workers_) worker.join();
}

void EarthChunkBuilder::request(const EarthNode& node, double priority) {
  {
    std::lock_guard lock(mutex_);
    const auto key = node.key();
    if (pending_.contains(key)) return;
    pending_[key] = node;
    queue_.insert({priority, key});
  }
  wake_.notify_one();
}

void EarthChunkBuilder::clearQueue() {
  std::lock_guard lock(mutex_);
  for (const auto& [priority, key] : queue_) pending_.erase(key);
  queue_.clear();
}
// A request already queued, being built or built and waiting is not repeated.

std::vector<EarthChunkMesh> EarthChunkBuilder::finished(std::size_t limit) {
  std::lock_guard lock(mutex_);
  std::vector<EarthChunkMesh> out;
  const std::size_t count = std::min(limit, done_.size());
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    pending_.erase(done_[i].node.key());
    out.push_back(std::move(done_[i]));
  }
  done_.erase(done_.begin(), done_.begin() + std::ptrdiff_t(count));
  return out;
}

void EarthChunkBuilder::work() {
  for (;;) {
    EarthNode node;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [&] { return stop_ || !queue_.empty(); });
      if (stop_) return;
      const auto first = queue_.begin();
      node = pending_.at(first->second);
      queue_.erase(first);
    }
    const auto start = std::chrono::steady_clock::now();
    EarthChunkMesh mesh = build(node);
    buildSeconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    ++built_;
    std::lock_guard lock(mutex_);
    done_.push_back(std::move(mesh));  // still pending until handed over
  }
}

double EarthChunkBuilder::nodeSize(int level) { return kQuarterMeridian / (kRoots << level); }

Vec3 EarthChunkBuilder::nodeCentre(const EarthNode& node) {
  const double side = double(kRoots << node.level);
  return seaLevel(surfaceOf(faceDirection(node.face, (node.x + .5) / side, (node.y + .5) / side)));
}

EarthChunkMesh EarthChunkBuilder::build(const EarthNode& node) const {
  constexpr int n = kEarthChunkIntervals;
  // One ring past the edges, for normals.
  constexpr int ring = n + 3;
  struct Sample {
    Vec3 position, up;
    float ground, water;
    double latitude, longitude;
  };
  std::vector<Sample> grid(std::size_t(ring) * ring);
  const double side = double(kRoots << node.level) * n;
  std::shared_ptr<const EarthTerrain::TileData> tile;
  if (node.level < kPackLevels) tile = terrain_.tile(node.face, node.level, int(node.x), int(node.y));
  const int stride = kPackIntervals / n;
  for (int j = -1; j <= n + 1; ++j)
    for (int i = -1; i <= n + 1; ++i) {
      const Vec3 up = faceDirection(node.face, (node.x * double(n) + i) / side, (node.y * double(n) + j) / side);
      Surface surface = surfaceOf(up);
      const Vec3 base = seaLevel(surface);
      float ground, water;
      const bool stored = tile && i >= 0 && j >= 0 && i <= n && j <= n;
      if (stored) {
        const std::size_t k = std::size_t(j * stride) * (kPackIntervals + 1) + std::size_t(i * stride);
        ground = tile->heights.empty() ? -50.f : tile->heights[k];
        water = tile->kind == 1 ? 0.f : tile->water.empty() ? kDry : std::max(tile->water[k], kDry);
      } else {
        ground = float(terrain_.groundAltitude(base, surface));
        const double level = terrain_.waterAltitude(surface);
        water = level > kNoWater + 1 ? float(level) : kDry;
      }
      if (water <= ground) water = kDry;
      Sample& s = grid[std::size_t(j + 1) * ring + std::size_t(i + 1)];
      s.up = up;
      s.ground = ground;
      s.water = water;
      s.position = base + up * double(std::max(ground, water));
      s.latitude = std::atan2(surface.sinLat, surface.cosLat);
      s.longitude = std::atan2(surface.sinLon, surface.cosLon);
    }

  EarthChunkMesh mesh;
  mesh.node = node;
  const auto at = [&](int i, int j) -> const Sample& { return grid[std::size_t(j + 1) * ring + std::size_t(i + 1)]; };
  mesh.centre = (at(0, 0).position + at(n, 0).position + at(0, n).position + at(n, n).position) * .25;
  // Detail texture coordinates, in metres along the face, from a base that
  // keeps them small within the chunk. Texture tiles divide the base, so the
  // textures run on unbroken from one chunk to the next; the patterns laid
  // over the land need where the base lies too.
  const double metresPerStep = 2 * kQuarterMeridian / side;
  const double baseU = std::floor(node.x * double(n) * metresPerStep / kEarthTextureBase) * kEarthTextureBase;
  const double baseV = std::floor(node.y * double(n) * metresPerStep / kEarthTextureBase) * kEarthTextureBase;
  mesh.patternU = earthPattern(baseU);
  mesh.patternV = earthPattern(baseV);
  const double skirt = 15 + nodeSize(node.level) * .015;
  mesh.vertices.reserve(std::size_t(kEarthChunkSide) * kEarthChunkSide + 4 * kEarthChunkSide);
  const auto vertex = [&](int i, int j, double drop) {
    const Sample& s = at(i, j);
    const Vec3 du = at(i + 1, j).position - at(i - 1, j).position, dv = at(i, j + 1).position - at(i, j - 1).position;
    Vec3 normal = s.water > kDry ? s.up : du.cross(dv).normalized();
    if (normal.dot(s.up) < 0) normal = -normal;
    const Vec3 p = s.position - s.up * drop - mesh.centre;
    mesh.radius = std::max(mesh.radius, p.norm());
    EarthVertex v{};
    v.x = float(p.x), v.y = float(p.y), v.z = float(p.z);
    v.nx = float(normal.x), v.ny = float(normal.y), v.nz = float(normal.z);
    v.ground = s.ground;
    v.water = s.water;
    v.u = float((node.x * double(n) + i) * metresPerStep - baseU);
    v.v = float((node.y * double(n) + j) * metresPerStep - baseV);
    v.cover = cover_.colour(s.latitude, s.longitude);
    mesh.vertices.push_back(v);
  };
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i) vertex(i, j, 0);
  // Skirts: bottom, right, top, left edges, each from its first corner.
  for (int k = 0; k <= n; ++k) vertex(k, 0, skirt);
  for (int k = 0; k <= n; ++k) vertex(n, k, skirt);
  for (int k = 0; k <= n; ++k) vertex(k, n, skirt);
  for (int k = 0; k <= n; ++k) vertex(0, k, skirt);
  return mesh;
}

std::vector<std::uint16_t> EarthChunkBuilder::indices() {
  constexpr int n = kEarthChunkIntervals, side = kEarthChunkSide;
  std::vector<std::uint16_t> out;
  const auto quad = [&](int a, int b, int c, int d) {
    for (const int k : {a, b, c, a, c, d}) out.push_back(std::uint16_t(k));
  };
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) quad(j * side + i, j * side + i + 1, (j + 1) * side + i + 1, (j + 1) * side + i);
  // Each skirt is drawn from both sides, so its winding does not matter.
  const int base = side * side;
  const auto edge = [&](int e, auto grid) {
    for (int k = 0; k < n; ++k) {
      const int a = grid(k), b = grid(k + 1), c = base + e * side + k + 1, d = base + e * side + k;
      quad(a, b, c, d);
      quad(a, d, c, b);
    }
  };
  edge(0, [&](int k) { return k; });
  edge(1, [&](int k) { return k * side + n; });
  edge(2, [&](int k) { return n * side + k; });
  edge(3, [&](int k) { return k * side; });
  return out;
}

}  // namespace ofs::client
