#pragma once
// Navigation map.
//
// One north-up picture of the landscape square is painted at start-up from the
// same terrain and land cover the renderer draws. The HUD shows a window of it
// around the aircraft as the minimap and the whole of it as the full map. This
// file holds the picture and the ground-to-screen mapping; it has no window or
// renderer dependency, so the headless tests cover it.

#include "landscape.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ofs::client {

struct MapImage {
  int size{};
  // RGBA8. Row 0 is the northern edge and column 0 the western edge, covering
  // Landscape::kExtent either side of the airfield.
  std::vector<std::uint8_t> rgba;
};

// Paints the map: elevation tints with relief shading, forest, farmland and lakes.
MapImage buildMapImage(const Landscape& landscape, int size = 512);

// A square on screen showing a square of ground, north up.
struct MapFrame {
  float x{}, y{}, size{};       // top-left corner and side, pixels
  double north{}, east{};       // ground position at the centre, metres
  double halfSpan{1};           // ground distance from the centre to an edge, metres

  // Screen position of a ground point. False when it lies outside the frame;
  // the position is still valid, so callers can pin a marker to the edge.
  bool project(double pointNorth, double pointEast, float& px, float& py) const {
    const double u = (pointEast - east) / (2 * halfSpan) + .5, v = (north - pointNorth) / (2 * halfSpan) + .5;
    px = x + float(u) * size;
    py = y + float(v) * size;
    return u >= 0 && u <= 1 && v >= 0 && v <= 1;
  }

  // The part of the frame the picture covers, with its texture coordinates.
  // False when the frame lies wholly outside the picture.
  bool picture(float& left, float& top, float& right, float& bottom,
               float& u0, float& v0, float& u1, float& v1) const {
    const double extent = Landscape::kExtent;
    const double west = std::max(east - halfSpan, -extent), eastEdge = std::min(east + halfSpan, extent);
    const double northEdge = std::min(north + halfSpan, extent), south = std::max(north - halfSpan, -extent);
    if (west >= eastEdge || south >= northEdge) return false;
    project(northEdge, west, left, top);
    project(south, eastEdge, right, bottom);
    u0 = float((west + extent) / (2 * extent));
    u1 = float((eastEdge + extent) / (2 * extent));
    v0 = float((extent - northEdge) / (2 * extent));
    v1 = float((extent - south) / (2 * extent));
    return true;
  }
};

// Ground distance from the centre to the edge of the minimap, metres. It opens
// out with speed, so the map shows about the same flying time at any speed.
inline double minimapHalfSpan(double speed) { return std::clamp(speed * 45.0, 6000.0, 30000.0); }

}  // namespace ofs::client
