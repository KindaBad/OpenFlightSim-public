#include "procedural.hpp"

#include "texture_mips.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <numeric>
#include <thread>

namespace ofs::client::procedural {
namespace {

constexpr float kTau = 6.28318530718f;

inline std::uint32_t mix32(std::uint32_t h) {
  h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
  return h;
}
inline std::uint32_t hash3(int x, int y, int z, std::uint32_t seed) {
  return mix32(std::uint32_t(x) * 0x8da6b343u ^ std::uint32_t(y) * 0xd8163841u ^
               std::uint32_t(z) * 0xcb1ab31fu ^ (seed + 1u) * 0x9e3779b9u);
}
inline int wrap(int v, int period) {
  v %= period;
  return v < 0 ? v + period : v;
}
inline float unit(std::uint32_t h) { return float(h & 0xffffffu) / float(0x1000000); }
inline float saturate(float v) { return std::clamp(v, 0.f, 1.f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float smooth(float a, float b, float x) {
  const float t = saturate((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}
inline float remap(float v, float l0, float h0, float l1, float h1) {
  return l1 + (v - l0) / (h0 - l0) * (h1 - l1);
}

inline float gradient(std::uint32_t h, float x, float y, float z) {
  switch (h & 15u) {
    case 0: return x + y;   case 1: return -x + y;  case 2: return x - y;   case 3: return -x - y;
    case 4: return x + z;   case 5: return -x + z;  case 6: return x - z;   case 7: return -x - z;
    case 8: return y + z;   case 9: return -y + z;  case 10: return y - z;  case 11: return -y - z;
    case 12: return x + y;  case 13: return -y + z; case 14: return -x + y; default: return -y - z;
  }
}

// Gradient noise with an independent repeat length on every axis, which lets a
// stretched pattern (cirrus streaks, grass grain) still tile.
float perlinAxes(float x, float y, float z, int px, int py, int pz, std::uint32_t seed) {
  const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
  const int ix = int(fx), iy = int(fy), iz = int(fz);
  const float tx = x - fx, ty = y - fy, tz = z - fz;
  const auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
  const float u = fade(tx), v = fade(ty), w = fade(tz);
  const int x0 = wrap(ix, px), x1 = wrap(ix + 1, px), y0 = wrap(iy, py), y1 = wrap(iy + 1, py);
  const int z0 = wrap(iz, pz), z1 = wrap(iz + 1, pz);
  const float n000 = gradient(hash3(x0, y0, z0, seed), tx, ty, tz);
  const float n100 = gradient(hash3(x1, y0, z0, seed), tx - 1, ty, tz);
  const float n010 = gradient(hash3(x0, y1, z0, seed), tx, ty - 1, tz);
  const float n110 = gradient(hash3(x1, y1, z0, seed), tx - 1, ty - 1, tz);
  const float n001 = gradient(hash3(x0, y0, z1, seed), tx, ty, tz - 1);
  const float n101 = gradient(hash3(x1, y0, z1, seed), tx - 1, ty, tz - 1);
  const float n011 = gradient(hash3(x0, y1, z1, seed), tx, ty - 1, tz - 1);
  const float n111 = gradient(hash3(x1, y1, z1, seed), tx - 1, ty - 1, tz - 1);
  return lerp(lerp(lerp(n000, n100, u), lerp(n010, n110, u), v),
              lerp(lerp(n001, n101, u), lerp(n011, n111, u), v), w);
}

// Two-dimensional conveniences over the 3D primitives, for the tiling images.
float fbm2(float u, float v, int period, int octaves, std::uint32_t seed) {
  return perlinFbm(u * period, v * period, .5f, period, octaves, seed);
}
Cell cell2(float u, float v, int period, std::uint32_t seed) {
  return worley(u * period, v * period, .5f, period, seed);
}

// Stretches a float field to the full 0..1 range.
void normalise(std::vector<float>& field) {
  const auto [low, high] = std::minmax_element(field.begin(), field.end());
  const float a = *low, range = std::max(*high - *low, 1e-6f);
  for (float& v : field) v = (v - a) / range;
}

// Replaces each value with its rank, so a threshold t selects a fraction t of
// the area. The weather map uses this to make "coverage" mean sky fraction.
void equalise(std::vector<float>& field) {
  std::vector<std::uint32_t> order(field.size());
  std::iota(order.begin(), order.end(), 0u);
  std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return field[a] < field[b]; });
  const float scale = 1.f / float(std::max<std::size_t>(field.size() - 1, 1));
  for (std::size_t rank = 0; rank < order.size(); ++rank) field[order[rank]] = float(rank) * scale;
}

std::uint8_t toByte(float v) { return std::uint8_t(saturate(v) * 255.f + .5f); }

struct Color { float r, g, b; };
inline Color mixColor(const Color& a, const Color& b, float t) {
  return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t)};
}
inline Color scale(const Color& c, float s) { return {c.r * s, c.g * s, c.b * s}; }

// One synthesised material sample: linear albedo, height, roughness.
struct Surface { Color albedo; float height, roughness; };

Surface grass(float u, float v) {
  const float broad = fbm2(u, v, 4, 4, 11);
  const float fine = fbm2(u, v, 32, 3, 12);
  // Blade grain in two directions so the tile has no single lay.
  const float grainA = perlinAxes(u * 24, v * 160, .5f, 24, 160, 1, 13);
  const float grainB = perlinAxes(u * 160, v * 24, .5f, 160, 24, 1, 14);
  const float grain = std::max(grainA, grainB);
  const Cell clump = cell2(u, v, 14, 15);
  const float height = saturate(.42f + .22f * fine + .26f * (1 - clump.f1) + .2f * grain);
  Color albedo = mixColor({.032f, .060f, .017f}, {.082f, .120f, .034f}, saturate(.5f + .6f * broad + .3f * fine));
  const float dry = saturate(smooth(.12f, .62f, fbm2(u, v, 7, 4, 16)) * .62f + fine * .2f) * .5f;
  albedo = mixColor(albedo, {.185f, .165f, .082f}, dry);
  return {scale(albedo, .72f + .56f * height), height, .92f};
}

Surface soil(float u, float v) {
  const float broad = fbm2(u, v, 5, 4, 21);
  const float clods = std::abs(fbm2(u, v, 24, 3, 22));
  const Cell pebble = cell2(u, v, 48, 23);
  const float stone = (1 - smooth(.10f, .22f, pebble.f1)) * (unit(pebble.id) > .62f ? 1.f : 0.f);
  const float height = saturate(.35f + .45f * clods + .35f * stone);
  Color albedo = mixColor({.105f, .078f, .050f}, {.205f, .150f, .092f}, saturate(.5f + .7f * broad));
  albedo = mixColor(albedo, {.30f, .28f, .25f}, stone * .8f);
  return {scale(albedo, .78f + .44f * height), height, .95f - .15f * stone};
}

Surface rock(float u, float v) {
  // A periodic domain warp keeps the fracture network from reading as paving.
  const float warpU = fbm2(u, v, 4, 3, 36) * .07f, warpV = fbm2(u, v, 4, 3, 37) * .07f;
  const float ridged = 1 - std::abs(fbm2(u + warpU, v + warpV, 5, 5, 31));
  const float facets = 1 - std::abs(fbm2(u, v, 11, 4, 38));
  const float fine = fbm2(u, v, 64, 2, 32);
  const Cell block = cell2(u + warpU * 1.6f, v + warpV * 1.6f, 5, 33);
  const float crack = (1 - smooth(0.f, .034f, block.f2 - block.f1)) * smooth(-.1f, .35f, fbm2(u, v, 3, 3, 39));
  const float warp = fbm2(u, v, 3, 3, 34);
  const float strata = .5f + .5f * std::sin((v + .085f * warp) * kTau * 14);
  const float height = saturate(ridged * .5f + facets * .22f + strata * .1f + fine * .08f - crack * .45f + .08f);
  Color albedo = mixColor({.150f, .142f, .132f}, {.335f, .305f, .262f},
                          saturate(ridged * .5f + facets * .22f + strata * .16f + fine * .25f - .12f));
  const float tint = unit(block.id) - .5f;
  albedo = {albedo.r * (1 + tint * .16f), albedo.g * (1 + tint * .10f), albedo.b * (1 - tint * .08f)};
  const float lichen = smooth(.22f, .5f, fbm2(u, v, 9, 3, 35)) * .28f;
  albedo = mixColor(albedo, {.215f, .235f, .115f}, lichen);
  return {scale(albedo, 1 - .62f * crack), height, .86f + .08f * crack};
}

Surface forest(float u, float v) {
  const Cell crown = cell2(u, v, 10, 41);
  const float radius = crown.f1 / .70f;
  const float dome = std::sqrt(std::max(0.f, 1 - radius * radius));
  // Crowns fade to a shared shadow tone well inside their cell, so the cell
  // boundaries themselves never show.
  const float gap = smooth(.46f, .72f, crown.f1);
  const float leaf = fbm2(u, v, 64, 2, 42);
  const float height = saturate(dome * .82f + leaf * .16f + .06f);
  Color tint = mixColor({.018f, .040f, .011f}, {.048f, .082f, .021f}, unit(crown.id));
  if (unit(mix32(crown.id)) > .62f) tint = {.014f, .033f, .015f};  // conifer
  Color albedo = scale(tint, .5f + .66f * dome + .28f * leaf);
  return {mixColor(albedo, {.007f, .014f, .006f}, gap), height, .9f};
}

Surface snow(float u, float v) {
  const float warp = fbm2(u, v, 3, 3, 51);
  const float ripple = .5f + .5f * std::sin((u * 7 + .9f * warp + .3f * fbm2(u, v, 9, 3, 53)) * kTau);
  const float drift = fbm2(u, v, 12, 3, 52);
  const float height = saturate(.36f + ripple * .2f + drift * .42f);
  const Color albedo = mixColor({.80f, .84f, .90f}, {.90f, .91f, .92f}, height);
  return {albedo, height, .52f + .12f * drift};
}

Surface asphalt(float u, float v) {
  const float speckle = perlinAxes(u * 128, v * 128, .5f, 128, 128, 1, 61);
  const Cell grit = cell2(u, v, 90, 62);
  const float stone = 1 - smooth(.14f, .40f, grit.f1);
  const float stain = fbm2(u, v, 3, 4, 63);
  const Cell slab = cell2(u, v, 3, 64);
  // Sealed cracks: a few short runs, not a network.
  const float crack = (1 - smooth(0.f, .008f, slab.f2 - slab.f1)) * smooth(.22f, .42f, fbm2(u, v, 4, 2, 65)) * .55f;
  const float grey = (.036f + .034f * stone * unit(grit.id) + .008f * speckle) * (.88f + .3f * stain);
  const float height = saturate(.3f + stone * .5f + speckle * .15f - crack);
  return {scale({grey * 1.02f, grey, grey * .97f}, 1 - .7f * crack), height, .86f - .1f * stain + .08f * crack};
}

using Generator = Surface (*)(float, float);

}  // namespace

void parallelRows(int rows, const std::function<void(int)>& fn) {
  const unsigned workers = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
  if (workers == 1 || rows < 8) {
    for (int row = 0; row < rows; ++row) fn(row);
    return;
  }
  std::atomic<int> next{0};
  const auto work = [&] {
    for (int row = next.fetch_add(1); row < rows; row = next.fetch_add(1)) fn(row);
  };
  std::vector<std::thread> threads;
  threads.reserve(workers - 1);
  for (unsigned i = 1; i < workers; ++i) threads.emplace_back(work);
  work();
  for (auto& thread : threads) thread.join();
}

float perlin(float x, float y, float z, int period, std::uint32_t seed) {
  return perlinAxes(x, y, z, period, period, period, seed);
}

float perlinFbm(float x, float y, float z, int period, int octaves, std::uint32_t seed) {
  float sum = 0, amplitude = 1, total = 0;
  for (int octave = 0; octave < octaves; ++octave) {
    sum += amplitude * perlinAxes(x, y, z, period, period, period, seed + std::uint32_t(octave) * 131u);
    total += amplitude;
    amplitude *= .5f;
    x *= 2; y *= 2; z *= 2;
    period *= 2;
  }
  // Classic gradient noise peaks near 0.87; rescale toward a unit range.
  return std::clamp(sum / total * 1.5f, -1.f, 1.f);
}

Cell worley(float x, float y, float z, int period, std::uint32_t seed) {
  const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
  const int ix = int(fx), iy = int(fy), iz = int(fz);
  Cell result{9.f, 9.f, 0};
  for (int dz = -1; dz <= 1; ++dz) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
    const std::uint32_t h = hash3(wrap(ix + dx, period), wrap(iy + dy, period), wrap(iz + dz, period), seed);
    const float px = float(ix + dx) + float(h & 1023u) / 1023.f;
    const float py = float(iy + dy) + float((h >> 10) & 1023u) / 1023.f;
    const float pz = float(iz + dz) + float((h >> 20) & 1023u) / 1023.f;
    const float d = (px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z);
    if (d < result.f1) { result.f2 = result.f1; result.f1 = d; result.id = h; }
    else if (d < result.f2) result.f2 = d;
  }
  result.f1 = std::sqrt(result.f1);
  result.f2 = std::sqrt(result.f2);
  return result;
}

std::vector<std::uint8_t> cloudShapeVolume(int size) {
  std::vector<float> field(std::size_t(size) * size * size);
  const auto inverted = [](float x, float y, float z, int cells, std::uint32_t seed) {
    return saturate(1 - worley(x * cells, y * cells, z * cells, cells, seed).f1);
  };
  parallelRows(size, [&](int k) {
    const float z = (k + .5f) / size;
    for (int j = 0; j < size; ++j) for (int i = 0; i < size; ++i) {
      const float x = (i + .5f) / size, y = (j + .5f) / size;
      const float billow = perlinFbm(x * 4, y * 4, z * 4, 4, 4, 101) * .5f + .5f;
      const float w6 = inverted(x, y, z, 6, 102), w12 = inverted(x, y, z, 12, 103);
      const float w8 = inverted(x, y, z, 8, 104), w16 = inverted(x, y, z, 16, 105);
      const float w32 = inverted(x, y, z, 32, 106);
      // Schneider's Perlin-Worley: billowing gradient noise whose lower bound is
      // lifted by cellular noise, then eroded by a higher-frequency Worley sum.
      const float perlinWorley = remap(billow, 0, 1, w6 * .7f + w12 * .3f, 1);
      const float low = (w8 * .625f + w16 * .25f + w32 * .125f) * .625f + (w16 * .75f + w32 * .25f) * .25f + w32 * .125f;
      field[(std::size_t(k) * size + j) * size + i] = remap(perlinWorley, low - 1, 1, 0, 1);
    }
  });
  normalise(field);
  std::vector<std::uint8_t> out(field.size());
  for (std::size_t i = 0; i < field.size(); ++i) out[i] = toByte(field[i]);
  return out;
}

std::vector<std::uint8_t> cloudDetailVolume(int size) {
  std::vector<float> field(std::size_t(size) * size * size);
  parallelRows(size, [&](int k) {
    const float z = (k + .5f) / size;
    for (int j = 0; j < size; ++j) for (int i = 0; i < size; ++i) {
      const float x = (i + .5f) / size, y = (j + .5f) / size;
      const auto inverted = [&](int cells, std::uint32_t seed) {
        return saturate(1 - worley(x * cells, y * cells, z * cells, cells, seed).f1);
      };
      field[(std::size_t(k) * size + j) * size + i] = inverted(3, 111) * .625f + inverted(6, 112) * .25f + inverted(12, 113) * .125f;
    }
  });
  normalise(field);
  std::vector<std::uint8_t> out(field.size());
  for (std::size_t i = 0; i < field.size(); ++i) out[i] = toByte(field[i]);
  return out;
}

std::vector<std::uint8_t> weatherMap(int size) {
  const std::size_t count = std::size_t(size) * size;
  std::vector<float> coverage(count), type(count), cirrus(count), density(count);
  parallelRows(size, [&](int j) {
    const float v = (j + .5f) / size;
    for (int i = 0; i < size; ++i) {
      const float u = (i + .5f) / size;
      const std::size_t at = std::size_t(j) * size + i;
      // Synoptic-scale systems broken into cumulus cells.
      const float systems = fbm2(u, v, 3, 5, 201) * .5f + .5f;
      const Cell cells = cell2(u, v, 14, 202);
      const float cellular = saturate(1 - cells.f1 * 1.15f);
      coverage[at] = systems * .68f + cellular * .32f + fbm2(u, v, 24, 3, 203) * .08f;
      type[at] = fbm2(u, v, 2, 4, 204);
      // Cirrus: fibres drawn out along one axis and bent by a slow warp.
      const float warp = fbm2(u, v, 2, 3, 205) * .22f;
      const float fibre = 1 - std::abs(perlinAxes((u + warp) * 4, (v + warp * .6f) * 40, .5f, 4, 40, 1, 206) * .7f +
                                       perlinAxes((u + warp) * 9, (v - warp * .4f) * 90, .5f, 9, 90, 1, 207) * .3f) * 1.6f;
      const float sheet = smooth(-.15f, .45f, fbm2(u, v, 3, 4, 208));
      cirrus[at] = saturate(fibre) * saturate(fibre) * sheet;
      density[at] = fbm2(u, v, 6, 4, 209);
    }
  });
  equalise(coverage);
  normalise(type);
  normalise(cirrus);
  normalise(density);
  std::vector<std::uint8_t> out(count * 4);
  for (std::size_t i = 0; i < count; ++i) {
    out[i * 4] = toByte(coverage[i]);
    out[i * 4 + 1] = toByte(type[i]);
    out[i * 4 + 2] = toByte(cirrus[i]);
    out[i * 4 + 3] = toByte(density[i]);
  }
  return out;
}

std::vector<std::uint8_t> noiseTile(int size) {
  const std::size_t count = std::size_t(size) * size;
  std::array<std::vector<float>, 4> channel;
  for (auto& c : channel) c.resize(count);
  parallelRows(size, [&](int j) {
    const float v = (j + .5f) / size;
    for (int i = 0; i < size; ++i) {
      const float u = (i + .5f) / size;
      const std::size_t at = std::size_t(j) * size + i;
      channel[0][at] = fbm2(u, v, 4, 6, 301);
      channel[1][at] = fbm2(u, v, 8, 5, 302);
      channel[2][at] = 1 - cell2(u, v, 8, 303).f1;
      channel[3][at] = fbm2(u, v, 2, 7, 304);
    }
  });
  std::vector<std::uint8_t> out(count * 4);
  for (int c = 0; c < 4; ++c) {
    normalise(channel[c]);
    for (std::size_t i = 0; i < count; ++i) out[i * 4 + c] = toByte(channel[c][i]);
  }
  return out;
}

std::vector<std::uint8_t> waterNormalTile(int size) {
  const std::size_t count = std::size_t(size) * size;
  std::vector<float> height(count);
  parallelRows(size, [&](int j) {
    const float v = (j + .5f) / size;
    for (int i = 0; i < size; ++i) {
      const float u = (i + .5f) / size;
      // Wind ripples: slightly elongated across the wind, with sharper crests.
      height[std::size_t(j) * size + i] = perlinAxes(u * 5, v * 9, .5f, 5, 9, 1, 401) * .5f +
                                          perlinAxes(u * 11, v * 19, .5f, 11, 19, 1, 402) * .3f +
                                          perlinAxes(u * 23, v * 37, .5f, 23, 37, 1, 403) * .2f;
    }
  });
  normalise(height);
  std::vector<std::uint8_t> out(count * 4, 255);
  const auto at = [&](int i, int j) { return height[std::size_t(wrap(j, size)) * size + wrap(i, size)]; };
  for (int j = 0; j < size; ++j) for (int i = 0; i < size; ++i) {
    const float strength = float(size) * .018f;
    const float dx = (at(i + 1, j) - at(i - 1, j)) * strength, dy = (at(i, j + 1) - at(i, j - 1)) * strength;
    const float inverse = 1 / std::sqrt(dx * dx + dy * dy + 1);
    std::uint8_t* texel = &out[(std::size_t(j) * size + i) * 4];
    texel[0] = toByte(-dx * inverse * .5f + .5f);
    texel[1] = toByte(-dy * inverse * .5f + .5f);
    texel[2] = toByte(at(i, j));
  }
  return out;
}

TerrainLayers terrainLayers(int size) {
  static constexpr Generator generators[kTerrainLayerCount] = {grass, soil, rock, forest, snow, asphalt};
  // Relief depth of each layer relative to its tile, which sets normal strength.
  static constexpr float relief[kTerrainLayerCount] = {.030f, .035f, .11f, .16f, .018f, .012f};
  TerrainLayers out;
  out.size = size;
  out.albedoHeight.resize(kTerrainLayerCount);
  out.normalRoughness.resize(kTerrainLayerCount);
  const std::size_t count = std::size_t(size) * size;
  for (int layer = 0; layer < kTerrainLayerCount; ++layer) {
    std::vector<Surface> samples(count);
    parallelRows(size, [&](int j) {
      for (int i = 0; i < size; ++i)
        samples[std::size_t(j) * size + i] = generators[layer]((i + .5f) / size, (j + .5f) / size);
    });
    auto& albedo = out.albedoHeight[layer];
    auto& normal = out.normalRoughness[layer];
    albedo.resize(count * 4);
    normal.resize(count * 4);
    const auto height = [&](int i, int j) {
      return samples[std::size_t(wrap(j, size)) * size + wrap(i, size)].height;
    };
    parallelRows(size, [&](int j) {
      for (int i = 0; i < size; ++i) {
        const Surface& s = samples[std::size_t(j) * size + i];
        const std::size_t at = (std::size_t(j) * size + i) * 4;
        albedo[at] = toByte(linearToSrgb(s.albedo.r));
        albedo[at + 1] = toByte(linearToSrgb(s.albedo.g));
        albedo[at + 2] = toByte(linearToSrgb(s.albedo.b));
        albedo[at + 3] = toByte(s.height);
        const float strength = relief[layer] * float(size) * .5f;
        const float dx = (height(i + 1, j) - height(i - 1, j)) * strength;
        const float dy = (height(i, j + 1) - height(i, j - 1)) * strength;
        const float inverse = 1 / std::sqrt(dx * dx + dy * dy + 1);
        // Cavities are darker than their surroundings: compare with a wider mean.
        float mean = 0;
        for (int oy = -2; oy <= 2; oy += 2) for (int ox = -2; ox <= 2; ox += 2) mean += height(i + ox * 2, j + oy * 2);
        const float cavity = saturate(1 - (mean / 9 - s.height) * 2.2f);
        normal[at] = toByte(-dx * inverse * .5f + .5f);
        normal[at + 1] = toByte(-dy * inverse * .5f + .5f);
        normal[at + 2] = toByte(s.roughness);
        normal[at + 3] = toByte(.45f + .55f * cavity);
      }
    });
  }
  return out;
}

}  // namespace ofs::client::procedural
