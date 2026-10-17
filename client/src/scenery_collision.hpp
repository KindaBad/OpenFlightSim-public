#pragma once
// What an aircraft can fly into besides the ground: trees and buildings.
//
// Scenery is built by each player's own game (landscape.cpp in the valley,
// earth_objects.cpp on the round Earth), from settings the server never sees,
// so the game tests the player's own aircraft against what it has built. A
// strike is a crash; in a game with others it is reported to the server
// (WeaponActionKind::Strike), which can cost no one but the one reporting it.
//
// A building is a box, a tree an upright cylinder as wide as its crown. Both
// are given in the frame of whatever holds them: x east, y up, z south.
#include "landscape.hpp"
#include "ofs/math.hpp"
#include "scenery.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ofs::client {

struct SceneryBox {
  float origin[3]{};
  float axes[3][3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};  // unit, in the holder's frame
  float low[3]{}, high[3]{};                         // extent along each axis from the origin
};

// The box round geometry made in its own axes and placed at `origin` along
// `x`, `y` and `z`, as earth_objects.cpp places a building into its tile.
inline SceneryBox sceneryBox(const Vec3& origin, const Vec3& x, const Vec3& y, const Vec3& z, const std::vector<SurfaceVertex>& made) {
  SceneryBox box;
  const Vec3 axes[3]{x, y, z};
  const auto component = [](const Vec3& v, int b) { return float(b == 0 ? v.x : b == 1 ? v.y : v.z); };
  for (int a = 0; a < 3; ++a) {
    box.origin[a] = component(origin, a);
    for (int b = 0; b < 3; ++b) box.axes[a][b] = component(axes[a], b);
    box.low[a] = 1e9f, box.high[a] = -1e9f;
  }
  for (const auto& v : made) {
    const float p[3]{v.x, v.y, v.z};
    for (int a = 0; a < 3; ++a) box.low[a] = std::min(box.low[a], p[a]), box.high[a] = std::max(box.high[a], p[a]);
  }
  return box;
}

// A house as geometry::house builds it: centred on (x, z) of the valley's
// render axes, `width` across and `depth` along its heading, from `base` up to
// its ridge.
inline SceneryBox houseBox(float x, float z, float width, float depth, float base, float top, float yaw) {
  SceneryBox box;
  box.origin[0] = x, box.origin[1] = 0, box.origin[2] = z;
  const float across[3]{std::cos(yaw), 0, -std::sin(yaw)}, along[3]{std::sin(yaw), 0, std::cos(yaw)};
  for (int b = 0; b < 3; ++b) box.axes[0][b] = across[b], box.axes[1][b] = b == 1, box.axes[2][b] = along[b];
  box.low[0] = -width * .5f, box.high[0] = width * .5f;
  box.low[1] = base, box.high[1] = top;
  box.low[2] = -depth * .5f, box.high[2] = depth * .5f;
  return box;
}

// Whether a point, in the holder's frame, is within `margin` metres of a box.
inline bool insideBox(const SceneryBox& box, const Vec3& p, double margin) {
  const double d[3]{p.x - box.origin[0], p.y - box.origin[1], p.z - box.origin[2]};
  for (int a = 0; a < 3; ++a) {
    const double along = d[0] * box.axes[a][0] + d[1] * box.axes[a][1] + d[2] * box.axes[a][2];
    if (along < box.low[a] - margin || along > box.high[a] + margin) return false;
  }
  return true;
}

// How far a tree's crown reaches from its trunk: the unit tree meshes
// (scenery.hpp) are about this wide for their height.
inline float treeRadius(const TreeInstance& tree) { return tree.height * tree.spread * (tree.conifer > .5f ? .20f : .26f); }

// Whether a point is within `margin` metres of a tree.
inline bool insideTree(const TreeInstance& tree, const Vec3& p, double margin) {
  if (p.y < tree.up - margin || p.y > tree.up + tree.height + margin) return false;
  const double reach = treeRadius(tree) + margin;
  const double dx = p.x - tree.east, dz = p.z - tree.south;
  return dx * dx + dz * dz < reach * reach;
}

}  // namespace ofs::client
