#pragma once
// Simple solid geometry for scenery: boxes, beams, cylinders, domes and
// vaults, in renderer axes (+X east, +Y up, +Z south). The airfield
// (airfield.cpp) and the round Earth's buildings (earth_objects.cpp) are built
// from these.

#include "scenery.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace ofs::client::geometry {

struct P {
  float x, y, z;
};
inline P operator+(P a, P b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline P operator-(P a, P b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline P operator*(P a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline P cross(P a, P b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float dot(P a, P b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline P unit(P a) {
  const float length = std::sqrt(dot(a, a));
  return length > 1e-9f ? a * (1 / length) : P{0, 1, 0};
}

// A rectangle of ground, east-west by north-south.
struct Rect {
  float x0, z0, x1, z1;
};


// A quad as two triangles, both turned to face `normal`, with the corners'
// texture coordinates running round it.
inline void facingQuad(std::vector<SurfaceVertex>& out, P a, P b, P c, P d, P normal) {
  const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  const P* corners[4] = {&a, &b, &c, &d};
  const bool reverse = dot(cross(b - a, c - a), normal) < 0;
  const int forward[] = {0, 1, 2, 0, 2, 3};
  const int backward[] = {0, 2, 1, 0, 3, 2};
  for (const int i : (reverse ? backward : forward))
    out.push_back({corners[i]->x, corners[i]->y, corners[i]->z, normal.x, normal.y, normal.z, uv[i][0], uv[i][1]});
}

// A house `width` by `depth` on the ground at (x, z), turned by `yaw`, with
// walls `height` high and a roof rising `pitch` above them: a ridge along its
// depth, or with `spire` a point. `ground(x, z)` is the height of the ground.
// Walls go down to the lowest corner, so nothing stands on air on a slope.
template <class Ground>
void house(std::vector<SurfaceVertex>& walls, std::vector<SurfaceVertex>& roofs, const Ground& ground, float x, float z,
           float width, float depth, float height, float pitch, float yaw, bool spire) {
  const float ax = std::sin(yaw), az = std::cos(yaw), cx = std::cos(yaw), cz = -std::sin(yaw);  // along, across
  const auto corner = [&](float u, float v) -> std::pair<float, float> {
    return {x + cx * (u * width * .5f) + ax * (v * depth * .5f), z + cz * (u * width * .5f) + az * (v * depth * .5f)};
  };
  const std::pair<float, float> c[4] = {corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1)};
  float low = 1e9f, high = -1e9f;
  for (const auto& [px, pz] : c) {
    const float g = ground(px, pz);
    low = std::min(low, g);
    high = std::max(high, g);
  }
  const float base = low - .4f, eaves = high + height, ridge = eaves + pitch;
  const auto at = [](const std::pair<float, float>& p, float y) { return P{p.first, y, p.second}; };
  for (int i = 0; i < 4; ++i) {
    const auto& p = c[i];
    const auto& q = c[(i + 1) % 4];
    float ox = q.second - p.second, oz = p.first - q.first;
    const float length = std::max(std::hypot(ox, oz), 1e-6f);
    ox /= length, oz /= length;
    const float mx = (p.first + q.first) * .5f - x, mz = (p.second + q.second) * .5f - z;
    if (ox * mx + oz * mz < 0) ox = -ox, oz = -oz;
    facingQuad(walls, at(p, base), at(q, base), at(q, eaves), at(p, eaves), {ox, 0, oz});
  }
  if (spire) {
    const P top{x, ridge, z};
    for (int i = 0; i < 4; ++i) {
      const P p = at(c[i], eaves), q = at(c[(i + 1) % 4], eaves);
      P normal = unit(cross(q - p, top - p));
      if (normal.y < 0) normal = normal * -1.f;
      facingQuad(roofs, p, q, top, top, normal);
    }
    return;
  }
  // Eaves overhang the walls a little.
  const auto eave = [&](float u, float v) { return at(corner(u * 1.12f, v * 1.06f), eaves - .15f); };
  const P front = at(corner(0, -1.06f), ridge), back = at(corner(0, 1.06f), ridge);
  for (const float side : {-1.f, 1.f}) {
    const P p = eave(side, -1), q = eave(side, 1);
    P normal = unit(cross(q - p, front - p));
    if (normal.y < 0) normal = normal * -1.f;
    facingQuad(roofs, p, q, back, front, normal);
  }
  for (const float end : {-1.f, 1.f}) {
    const P top = end < 0 ? front : back;
    facingQuad(walls, at(corner(-1, end), eaves), at(corner(1, end), eaves), top, top, {ax * end, 0, az * end});
  }
}

struct Mesher {
  std::vector<SurfaceVertex>& out;

  void vertex(P p, P n, float u, float v) { out.push_back({p.x, p.y, p.z, n.x, n.y, n.z, u, v}); }
  // A triangle wound to face the way its normals point.
  void tri(P a, P b, P c, P na, P nb, P nc) {
    const P face = cross(b - a, c - a);
    if (dot(face, face) < 1e-12f) return;
    if (dot(face, na + nb + nc) < 0) {
      std::swap(b, c);
      std::swap(nb, nc);
    }
    vertex(a, na, 0, 0);
    vertex(b, nb, 1, 0);
    vertex(c, nc, 1, 1);
  }
  void quad(P a, P b, P c, P d, P n) {
    tri(a, b, c, n, n, n);
    tri(a, c, d, n, n, n);
  }
  void quad(P a, P b, P c, P d, P na, P nb, P nc, P nd) {
    tri(a, b, c, na, nb, nc);
    tri(a, c, d, na, nc, nd);
  }
  // A horizontal rectangle at height y, facing up.
  void slab(Rect r, float y) { quad({r.x0, y, r.z0}, {r.x0, y, r.z1}, {r.x1, y, r.z1}, {r.x1, y, r.z0}, {0, 1, 0}); }
  // A box standing on height y: centre, size east-west and north-south, height.
  void box(float x, float z, float w, float d, float y, float h, bool base = false) {
    const float l = x - w / 2, r = x + w / 2, f = z - d / 2, b = z + d / 2, t = y + h;
    quad({l, t, f}, {l, t, b}, {r, t, b}, {r, t, f}, {0, 1, 0});
    quad({l, y, f}, {r, y, f}, {r, t, f}, {l, t, f}, {0, 0, -1});
    quad({r, y, b}, {l, y, b}, {l, t, b}, {r, t, b}, {0, 0, 1});
    quad({l, y, b}, {l, y, f}, {l, t, f}, {l, t, b}, {-1, 0, 0});
    quad({r, y, f}, {r, y, b}, {r, t, b}, {r, t, f}, {1, 0, 0});
    if (base) quad({l, y, f}, {r, y, f}, {r, y, b}, {l, y, b}, {0, -1, 0});
  }
  void box(Rect r, float y, float h) { box((r.x0 + r.x1) / 2, (r.z0 + r.z1) / 2, r.x1 - r.x0, r.z1 - r.z0, y, h); }
  // A box between two points, square in section: masts, rails and booms.
  void beam(P a, P b, float thickness) {
    const P along = unit(b - a);
    const P side = unit(std::abs(along.y) > .9f ? cross(along, {1, 0, 0}) : cross(along, {0, 1, 0}));
    const P up = cross(side, along);
    const P s = side * (thickness / 2), u = up * (thickness / 2);
    const P corners[4]{s + u, u - s, P{0, 0, 0} - s - u, s - u};
    const P normals[4]{unit(s + u), unit(u - s), unit(P{0, 0, 0} - s - u), unit(s - u)};
    for (int i = 0; i < 4; ++i) {
      const int j = (i + 1) % 4;
      const P n = unit(normals[i] + normals[j]);
      quad(a + corners[i], a + corners[j], b + corners[j], b + corners[i], n);
    }
    quad(b + corners[0], b + corners[1], b + corners[2], b + corners[3], along);
  }
  // An upright cylinder or cone frustum, closed on top.
  void cylinder(float x, float z, float y0, float y1, float r0, float r1, int sides = 20, bool cap = true) {
    const float slope = (r0 - r1) / std::max(y1 - y0, 1e-3f);
    for (int i = 0; i < sides; ++i) {
      const float a = 6.2831853f * float(i) / float(sides), b = 6.2831853f * float(i + 1) / float(sides);
      const P na = unit({std::cos(a), slope, std::sin(a)}), nb = unit({std::cos(b), slope, std::sin(b)});
      const P a0{x + std::cos(a) * r0, y0, z + std::sin(a) * r0}, b0{x + std::cos(b) * r0, y0, z + std::sin(b) * r0};
      const P a1{x + std::cos(a) * r1, y1, z + std::sin(a) * r1}, b1{x + std::cos(b) * r1, y1, z + std::sin(b) * r1};
      quad(a0, b0, b1, a1, na, nb, nb, na);
      if (cap && r1 > 1e-3f) tri({x, y1, z}, a1, b1, {0, 1, 0}, {0, 1, 0}, {0, 1, 0});
    }
  }
  // The upper part of a sphere, down to `from` radians below the pole.
  void dome(float x, float y, float z, float radius, float from = 1.5708f, int sides = 20, int rings = 8) {
    const auto point = [&](int ring, int side) {
      const float polar = from * float(ring) / float(rings), around = 6.2831853f * float(side) / float(sides);
      const P n{std::sin(polar) * std::cos(around), std::cos(polar), std::sin(polar) * std::sin(around)};
      return std::pair{P{x, y, z} + n * radius, n};
    };
    for (int ring = 0; ring < rings; ++ring)
      for (int side = 0; side < sides; ++side) {
        const auto [a, na] = point(ring, side);
        const auto [b, nb] = point(ring, side + 1);
        const auto [c, nc] = point(ring + 1, side + 1);
        const auto [d, nd] = point(ring + 1, side);
        quad(a, b, c, d, na, nb, nc, nd);
      }
  }
  // A barrel vault standing on the ground, centred on (cx, cz): `length` along
  // `axis` (0 = east-west, 1 = north-south), `width` across and `height` at the
  // crown. The end walls are separate; see vaultEnd.
  void vault(float cx, float cz, int axis, float length, float width, float height, int segments = 14) {
    const auto point = [&](float along, int segment) {
      const float angle = 3.14159265f * float(segment) / float(segments);
      const float across = -std::cos(angle) * width / 2, up = std::sin(angle) * height;
      // The normal of an ellipse: its gradient, not the direction from the centre.
      const float flat = -std::cos(angle) / (width / 2);
      const P normal = axis == 0 ? unit({0, std::sin(angle) / height, flat}) : unit({flat, std::sin(angle) / height, 0});
      return std::pair{axis == 0 ? P{cx + along, up, cz + across} : P{cx + across, up, cz + along}, normal};
    };
    for (int segment = 0; segment < segments; ++segment) {
      const auto [a, na] = point(-length / 2, segment);
      const auto [b, nb] = point(-length / 2, segment + 1);
      const auto [c, nc] = point(length / 2, segment + 1);
      const auto [d, nd] = point(length / 2, segment);
      quad(a, b, c, d, na, nb, nc, nd);
    }
  }
  // The wall that closes one end of a vault, with an opening left in it:
  // `open` of the width and `clear` of the height, from the ground up.
  void vaultEnd(float cx, float cz, int axis, float at, float width, float height, float open, float clear, float facing,
                int segments = 14) {
    const P normal = axis == 0 ? P{facing, 0, 0} : P{0, 0, facing};
    const auto place = [&](float across, float up) { return axis == 0 ? P{cx + at, up, cz + across} : P{cx + across, up, cz + at}; };
    const float half = width / 2 * open, top = height * clear;
    for (int segment = 0; segment < segments; ++segment) {
      const float a0 = 3.14159265f * float(segment) / float(segments), a1 = 3.14159265f * float(segment + 1) / float(segments);
      const float x0 = -std::cos(a0) * width / 2, x1 = -std::cos(a1) * width / 2;
      const float y0 = std::sin(a0) * height, y1 = std::sin(a1) * height;
      // Fill from the opening's edge up to the vault over this slice.
      const auto lintel = [&](float x) { return std::abs(x) < half ? top : 0.f; };
      const float f0 = std::min(lintel(x0), y0), f1 = std::min(lintel(x1), y1);
      quad(place(x0, f0), place(x1, f1), place(x1, y1), place(x0, y0), normal);
    }
  }
};

}  // namespace ofs::client::geometry
