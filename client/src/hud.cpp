#include "hud.hpp"
#include "map.hpp"
#include "ofs/units.hpp"
#include <imgui.h>
#include <algorithm>
#include <initializer_list>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <string>
#include <vector>

namespace ofs::client {
namespace {
// One palette for the flight display, shared with the menus: deep navy panels,
// pale text, green flight symbology, and amber and red kept for warnings.
constexpr ImU32 kPanel = IM_COL32(9, 16, 25, 205);
constexpr ImU32 kBorder = IM_COL32(88, 132, 178, 95);
constexpr ImU32 kText = IM_COL32(237, 243, 252, 255);
constexpr ImU32 kMuted = IM_COL32(157, 178, 204, 255);
constexpr ImU32 kAccent = IM_COL32(126, 226, 160, 255);
constexpr ImU32 kBlue = IM_COL32(64, 164, 255, 255);
constexpr ImU32 kAmber = IM_COL32(255, 192, 95, 255);
constexpr ImU32 kDanger = IM_COL32(255, 104, 93, 255);

std::string number(double value, int decimals = 0) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  return buffer;
}
void text(ImDrawList* draw, ImVec2 pos, const std::string& value, ImU32 color = kText,
          float size = 15) {
  const ImU32 shadow = IM_COL32(0, 0, 0, ((color >> 24) & 0xff) * 210 / 255);
  draw->AddText(ImGui::GetFont(), size, {pos.x+1,pos.y+1}, shadow, value.c_str());
  draw->AddText(ImGui::GetFont(), size, pos, color, value.c_str());
}
float textWidth(const std::string& value, float size) {
  return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, value.c_str()).x;
}
void textRight(ImDrawList* draw, ImVec2 pos, const std::string& value, ImU32 color = kText, float size = 15) {
  text(draw, {pos.x - textWidth(value, size), pos.y}, value, color, size);
}
void panel(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 fill = kPanel) {
  draw->AddRectFilled(min, max, fill, 5);
  draw->AddRect(min, max, kBorder, 5);
}
// A horizontal gauge: a track with the filled part of it in `color`.
void bar(ImDrawList* draw, ImVec2 min, ImVec2 max, float fraction, ImU32 color) {
  draw->AddRectFilled(min, max, IM_COL32(255, 255, 255, 28), 2);
  fraction = std::clamp(fraction, 0.f, 1.f);
  if (fraction > 0) draw->AddRectFilled(min, {min.x + (max.x - min.x) * fraction, max.y}, color, 2);
}
ImU32 blend(ImU32 from, ImU32 to, float t) {
  t = std::clamp(t, 0.f, 1.f);
  const auto channel = [&](int shift) {
    const float a = float((from >> shift) & 0xff), b = float((to >> shift) & 0xff);
    return ImU32(a + (b - a) * t) << shift;
  };
  return channel(0) | channel(8) | channel(16) | channel(24);
}
const char* cameraName(CameraMode mode) {
  switch (mode) {
    case CameraMode::Chase: return "CHASE";
    case CameraMode::CloseChase: return "CLOSE CHASE";
    case CameraMode::Orbit: return "ORBIT";
    case CameraMode::FirstPerson: return "FLIGHT DECK";
    case CameraMode::Pursuit: return "PURSUIT";
    default: return "FREE CAMERA";
  }
}

constexpr ImU32 kFriendly = IM_COL32(110, 185, 255, 255);
constexpr double kRunwayHalfLength = 1300;  // the airfield's runway lies north-south through the origin

double headingOf(const State& state) {
  const Vec3 nose = state.att.rotate({1, 0, 0});
  return std::atan2(nose.y, nose.x);
}
// Aircraft marker: a dart pointing along `heading`, radians clockwise from north (screen up).
void dart(ImDrawList* draw, ImVec2 centre, double heading, float length, ImU32 color) {
  const ImVec2 along{float(std::sin(heading)), float(-std::cos(heading))}, across{-along.y, along.x};
  const auto at = [&](float forward, float side) {
    return ImVec2{centre.x + (along.x * forward + across.x * side) * length,
                  centre.y + (along.y * forward + across.y * side) * length};
  };
  const ImVec2 outline[4]{at(1, 0), at(-.75f, .62f), at(-.3f, 0), at(-.75f, -.62f)};
  draw->AddTriangleFilled(outline[0], outline[1], outline[2], color);
  draw->AddTriangleFilled(outline[0], outline[2], outline[3], color);
  draw->AddPolyline(outline, 4, IM_COL32(0, 0, 0, 230), ImDrawFlags_Closed, 1.2f);
}

// Draws the ground and everything on it into `map`. The minimap and the full
// map differ only in their frame and in how much they label.
void drawMap(ImDrawList* draw, const MapFrame& map, const HudFrame& frame, const Renderer& renderer, bool full) {
  const ImVec2 min{map.x, map.y}, max{map.x + map.size, map.y + map.size};
  draw->AddRectFilled(min, max, IM_COL32(18, 22, 26, full ? 245 : 215));
  draw->PushClipRect(min, max, true);
  float left, top, right, bottom, u0, v0, u1, v1;
  if (renderer.mapTexture() && map.picture(left, top, right, bottom, u0, v0, u1, v1))
    draw->AddImage(static_cast<ImTextureID>(renderer.mapTexture()), {left, top}, {right, bottom}, {u0, v0}, {u1, v1},
                   IM_COL32(255, 255, 255, full ? 255 : 235));

  const double spacing = map.halfSpan <= 8000 ? 2000 : map.halfSpan <= 20000 ? 5000 : 10000;
  for (double line = std::ceil((map.east - map.halfSpan) / spacing) * spacing; line <= map.east + map.halfSpan; line += spacing) {
    float x, y;
    map.project(map.north, line, x, y);
    draw->AddLine({x, min.y}, {x, max.y}, IM_COL32(255, 255, 255, 30));
    if (full && line != 0) text(draw, {x + 3, min.y + 3}, (line > 0 ? "E " : "W ") + number(std::abs(line) / 1000), kMuted, 11);
  }
  for (double line = std::ceil((map.north - map.halfSpan) / spacing) * spacing; line <= map.north + map.halfSpan; line += spacing) {
    float x, y;
    map.project(line, map.east, x, y);
    draw->AddLine({min.x, y}, {max.x, y}, IM_COL32(255, 255, 255, 30));
    if (full && line != 0) text(draw, {min.x + 3, y + 2}, (line > 0 ? "N " : "S ") + number(std::abs(line) / 1000), kMuted, 11);
  }

  // Marks that fall outside the minimap are held on its edge, so the way back
  // to the airfield and to other aircraft is always shown.
  const auto place = [&](const Vec3& position, ImVec2& point) {
    const bool inside = map.project(position.x, position.y, point.x, point.y);
    point.x = std::clamp(point.x, min.x + 7, max.x - 7);
    point.y = std::clamp(point.y, min.y + 7, max.y - 7);
    return inside;
  };

  ImVec2 runwayStart, runwayEnd, airfield;
  map.project(-kRunwayHalfLength, 0, runwayStart.x, runwayStart.y);
  map.project(kRunwayHalfLength, 0, runwayEnd.x, runwayEnd.y);
  draw->AddLine(runwayStart, runwayEnd, IM_COL32(0, 0, 0, 200), 5);
  draw->AddLine(runwayStart, runwayEnd, IM_COL32(232, 232, 226, 255), 2.5f);
  const bool airfieldInside = place({}, airfield);
  if (!airfieldInside) draw->AddRectFilled({airfield.x - 4, airfield.y - 4}, {airfield.x + 4, airfield.y + 4}, IM_COL32(0, 0, 0, 200));
  if (!airfieldInside || full)
    draw->AddRect({airfield.x - 4, airfield.y - 4}, {airfield.x + 4, airfield.y + 4}, kText, 0, 0, 1.5f);
  if (full && airfieldInside) text(draw, {airfield.x + 9, airfield.y - 7}, "AIRFIELD", kText, 12);

  const ImU32 otherColor = frame.dogfight ? kDanger : kFriendly;
  for (const auto& remote : frame.remotes) {
    if (!remote.alive) continue;
    ImVec2 point;
    if (place(remote.state.pos_ned, point)) {
      dart(draw, point, headingOf(remote.state), full ? 8.f : 7.f, otherColor);
      if (full) text(draw, {point.x + 10, point.y - 6}, remote.name.empty() ? "Aircraft" : remote.name, otherColor, 12);
    } else {
      draw->AddCircleFilled(point, 3.5f, otherColor);
      draw->AddCircle(point, 3.5f, IM_COL32(0, 0, 0, 230), 12, 1.2f);
    }
  }
  for (const auto& track : frame.radarTracks) {
    if (!(track.entity == frame.lockedTarget) && !(track.entity == frame.selectedTarget)) continue;
    ImVec2 point;
    place(track.position, point);
    draw->AddCircle(point, 10, track.entity == frame.lockedTarget ? kAmber : kAccent, 20, 1.6f);
  }

  // Own aircraft, with the ground it covers in the next half minute.
  const State& own = *frame.local;
  ImVec2 self, ahead;
  place(own.pos_ned, self);
  map.project(own.pos_ned.x + own.vel_ned.x * 30, own.pos_ned.y + own.vel_ned.y * 30, ahead.x, ahead.y);
  draw->AddLine(self, ahead, IM_COL32(255, 255, 255, 120), 1.2f);
  dart(draw, self, headingOf(own), full ? 10.f : 9.f, kText);
  draw->PopClipRect();
  draw->AddRect(min, max, kBorder, 1);

  if (full) {
    const double range = std::hypot(own.pos_ned.x, own.pos_ned.y);
    const double bearing = std::fmod(std::atan2(-own.pos_ned.y, -own.pos_ned.x) / kDeg2Rad + 360, 360);
    text(draw, {min.x, min.y - 24}, "MAP", kText, 18);
    text(draw, {max.x - 86, min.y - 19}, "N TO CLOSE", kMuted, 12);
    text(draw, {min.x, max.y + 7},
         "Airfield " + number(range / 1000, 1) + " km, bearing " + number(bearing) + "     Grid " +
             number(spacing / 1000) + " km     North is up",
         kMuted, 12);
  } else {
    text(draw, {min.x + 6, max.y - 18}, "GRID " + number(spacing / 1000) + " km", kText, 11);
    text(draw, {max.x - 46, max.y - 18}, "N MAP", kMuted, 11);
    text(draw, {(min.x + max.x) * .5f - 4, min.y + 3}, "N", kText, 11);
  }
}

// The whole landscape square, or the same area about an aircraft that has left it.
void drawFullMap(ImDrawList* draw, ImVec2 display, const HudFrame& frame, const Renderer& renderer) {
  const Vec3& position = frame.local->pos_ned;
  const bool away = std::max(std::abs(position.x), std::abs(position.y)) > Landscape::kExtent * .97;
  MapFrame map;
  map.size = std::floor(std::min(display.x, display.y) * .78f);
  map.x = std::floor((display.x - map.size) * .5f);
  map.y = std::floor((display.y - map.size) * .5f);
  map.north = away ? position.x : 0;
  map.east = away ? position.y : 0;
  map.halfSpan = Landscape::kExtent;
  draw->AddRectFilled({0, 0}, display, IM_COL32(0, 0, 0, 110));
  drawMap(draw, map, frame, renderer, true);
}

// ---- Weapons ---------------------------------------------------------------

// Four corner ticks of a square: a target marker that leaves the target visible.
void brackets(ImDrawList* draw, ImVec2 c, float half, ImU32 color, float thickness = 1.5f) {
  const float tick = std::max(4.f, half * .45f);
  for (const float sx : {-1.f, 1.f})
    for (const float sy : {-1.f, 1.f}) {
      const ImVec2 corner{c.x + sx * half, c.y + sy * half};
      draw->AddLine(corner, {corner.x - sx * tick, corner.y}, color, thickness);
      draw->AddLine(corner, {corner.x, corner.y - sy * tick}, color, thickness);
    }
}
void diamond(ImDrawList* draw, ImVec2 c, float half, ImU32 color, float thickness = 1.6f) {
  const ImVec2 points[4]{{c.x, c.y - half}, {c.x + half, c.y}, {c.x, c.y + half}, {c.x - half, c.y}};
  draw->AddPolyline(points, 4, color, ImDrawFlags_Closed, thickness);
}
// A ring broken into four arcs, the look of a caged seeker's field of view.
void brokenRing(ImDrawList* draw, ImVec2 c, float radius, ImU32 color, float thickness, float turn) {
  for (int i = 0; i < 4; ++i) {
    const float start = turn + float(kPi) * .5f * i + .22f;
    draw->PathArcTo(c, radius, start, start + float(kPi) * .5f - .44f, 14);
    draw->PathStroke(color, 0, thickness);
  }
}
void centred(ImDrawList* draw, ImVec2 at, const std::string& value, ImU32 color, float size = 12) {
  const float width = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, value.c_str()).x;
  text(draw, {at.x - width * .5f, at.y}, value, color, size);
}
ImU32 faded(ImU32 color, float alpha) {
  return (color & 0x00ffffffu) | (ImU32(std::clamp(alpha, 0.f, 1.f) * 255) << 24);
}
float ease(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return t * t * (3 - 2 * t);
}

// When the current lock began, so its marker can close in on the target.
struct LockAnimation {
  weapons::EntityRef target{};
  double since{};
} lockAnimation;

void drawWeapons(ImDrawList* draw, ImVec2 display, const HudFrame& frame, const Renderer& renderer) {
  const State& state = *frame.local;
  const double now = ImGui::GetTime();
  const bool infrared = frame.missileWeapon == weapons::WeaponType::Infrared;
  // With the selected missile type spent there is nothing to aim, so the
  // radar picture is shown as it is with the gun.
  const bool missile = frame.missileSelected && (infrared ? frame.irCount : frame.radarCount) > 0;
  const float pulse = .5f + .5f * float(std::sin(now * 12));

  // Where an entity is drawn this frame. The rendered aircraft is preferred to
  // the radar's filtered track, so markers sit on the aircraft and not near it.
  const auto worldOf = [&](weapons::EntityRef entity, Vec3& position) {
    for (const auto& remote : frame.remotes)
      if (remote.entity == entity.id && remote.alive) { position = remote.state.pos_ned; return true; }
    for (const auto& track : frame.radarTracks)
      if (track.entity == entity) { position = track.position; return true; }
    return false;
  };
  const auto screenOf = [&](weapons::EntityRef entity, ImVec2& point, double& range) {
    Vec3 position;
    float depth;
    if (!worldOf(entity, position)) return false;
    range = (position - state.pos_ned).norm();
    return renderer.projectToScreen(position, point.x, point.y, depth);
  };
  const weapons::EntityRef target = frame.seekerTarget;
  if (!(target == lockAnimation.target)) lockAnimation = {target, now};
  const float lockAge = float(now - lockAnimation.since);

  // ---- Radar scope ----
  {
    const float x = display.x - 245, y = 52;
    panel(draw, {x, y}, {x + 225, y + 205});
    text(draw, {x + 12, y + 9}, frame.lockedTarget.id ? "RADAR  TRACK" : "RADAR  SEARCH",
         frame.lockedTarget.id ? kAmber : kAccent, 12);
    text(draw, {x + 152, y + 9}, "90 km", kMuted, 11);
    // A B-scope: azimuth across, range up, with the own aircraft at the bottom.
    const ImVec2 min{x + 22, y + 32}, max{x + 203, y + 192};
    draw->AddRect(min, max, kBorder);
    for (int i = 1; i < 3; ++i) {
      const float gy = min.y + (max.y - min.y) * i / 3;
      draw->AddLine({min.x, gy}, {max.x, gy}, IM_COL32(88, 132, 178, 45));
    }
    draw->AddLine({(min.x + max.x) * .5f, min.y}, {(min.x + max.x) * .5f, max.y}, IM_COL32(88, 132, 178, 45));
    // The antenna's sweep, two seconds from edge to edge as the radar scans.
    const float sweep = float(std::fmod(now, 2.) / 2.);
    const float sx = min.x + (max.x - min.x) * sweep;
    draw->AddLine({sx, min.y}, {sx, max.y}, faded(kAccent, .35f), 1.5f);
    for (const auto& track : frame.radarTracks) {
      const auto relative = state.att.inverseRotate(track.position - state.pos_ned);
      const float az = float(std::clamp(std::atan2(relative.y, relative.x) / (60 * kDeg2Rad), -1., 1.));
      const ImVec2 point{(min.x + max.x) * .5f + az * (max.x - min.x) * .5f,
                         max.y - float(std::min(relative.norm() / 90000., 1.)) * (max.y - min.y)};
      const bool locked = track.entity == frame.lockedTarget, selected = track.entity == frame.selectedTarget;
      const ImU32 color = locked ? kAmber : selected ? kAccent : kMuted;
      draw->AddRectFilled({point.x - 3, point.y - 1.5f}, {point.x + 3, point.y + 1.5f}, color);
      if (locked || selected) brackets(draw, point, 7, color, 1.2f);
    }
  }

  // ---- Weapon strip ----
  {
    const float x = 20, y = 176;
    panel(draw, {x, y}, {x + 290, y + 84});
    const auto slot = [&](float sx, const char* key, const char* name, weapons::WeaponType type, bool selected) {
      const ImU32 color = selected ? kText : kMuted;
      if (selected) draw->AddRectFilled({sx - 6, y + 6}, {sx + 84, y + 44}, IM_COL32(64, 164, 255, 46), 3);
      text(draw, {sx, y + 9}, std::string(key) + "  " + name, color, 13);
      float px = sx;
      for (const auto& station : frame.stations) {
        if (station.type != type) continue;
        // One missile-shaped pip per station, hollow once it has been fired.
        const ImVec2 a{px, y + 30}, b{px + 22, y + 36};
        if (station.mounted) draw->AddRectFilled(a, b, selected ? kAccent : kMuted, 2);
        else draw->AddRect(a, b, faded(kMuted, .55f), 2);
        px += 28;
      }
    };
    const bool chosen = frame.missileSelected;
    slot(x + 14, "1", "GUN", weapons::WeaponType::None, !chosen);
    text(draw, {x + 14, y + 28}, number(frame.ammo), !chosen ? kAccent : kMuted, 12);
    slot(x + 108, "2", "IR-90", weapons::WeaponType::Infrared, chosen && infrared);
    slot(x + 202, "3", "AR-157", weapons::WeaponType::ActiveRadar, chosen && !infrared);
    const unsigned left = infrared ? frame.irCount : frame.radarCount;
    std::string status = "L RADAR LOCK    SPACE FIRE";
    ImU32 statusColor = kMuted;
    if (chosen && !left) { status = "NO MISSILES LEFT"; statusColor = kDanger; }
    else if (missile) {
      if (frame.seekerReady && frame.envelope.targetRange < frame.envelope.minimum) {
        status = "TOO CLOSE"; statusColor = kAmber;
      } else if (frame.seekerReady) {
        status = frame.envelope.inside ? "LOCKED    SPACE TO FIRE" : "LOCKED    OUT OF RANGE";
        statusColor = frame.envelope.inside ? kDanger : kAmber;
      } else if (infrared) {
        status = target.id ? "SEEKER ACQUIRING" : "SEEKER SEARCHING    POINT AT A TARGET";
        statusColor = target.id ? kAmber : kMuted;
      } else {
        status = frame.radarTracks.empty() ? "NO RADAR CONTACTS" : "L TO LOCK NEAREST    T/Y TO CYCLE";
      }
    }
    text(draw, {x + 14, y + 55}, status, statusColor, 12);
    if (frame.activeMissiles)
      text(draw, {x + 14, y + 68}, "IN FLIGHT " + number(frame.activeMissiles) + "    " +
           number(frame.missileAge, 1) + " s    " + number(frame.missileSpeed * 3.6) + " km/h", kMuted, 11);
  }
  if (frame.fullMap) return;

  // ---- Radar contacts in the world ----
  for (const auto& track : frame.radarTracks) {
    if (track.entity == target && missile) continue;  // drawn as the weapon's target below
    ImVec2 point;
    double range;
    if (!screenOf(track.entity, point, range)) continue;
    const bool locked = track.entity == frame.lockedTarget, selected = track.entity == frame.selectedTarget;
    const ImU32 color = locked ? kAmber : selected ? kAccent : faded(kAccent, .7f);
    brackets(draw, point, locked ? 14.f : 10.f, color, locked ? 2.f : 1.3f);
    if (locked || selected) centred(draw, {point.x, point.y + 18}, number(range / 1000, 1) + " km", color);
  }

  // ---- The selected weapon's target ----
  ImVec2 boresight{};
  float depth;
  const Vec3 nose = state.att.rotate({1, 0, 0});
  const bool boresightVisible = renderer.projectToScreen(state.pos_ned + nose * 3000, boresight.x, boresight.y, depth);
  ImVec2 point;
  double range = 0;
  const bool targetVisible = missile && target.id && screenOf(target, point, range);
  const ImU32 lockColor = frame.seekerReady ? kDanger : kAmber;
  if (missile && infrared && frame.irCount) {
    // The seeker's field of view, as a ring the pilot puts over a target. It
    // leaves the nose and tightens onto the target as the lock builds.
    const auto& seeker = weapons::missileDefinition(weapons::WeaponType::Infrared).seeker;
    ImVec2 edge;
    float ring = 90;
    const Vec3 up = state.att.rotate({0, 0, -1});
    if (boresightVisible &&
        renderer.projectToScreen(state.pos_ned + (nose * std::cos(seeker.fov * .5) + up * std::sin(seeker.fov * .5)) * 3000,
                                 edge.x, edge.y, depth))
      ring = std::clamp(std::hypot(edge.x - boresight.x, edge.y - boresight.y), 40.f, display.y * .45f);
    if (targetVisible) {
      const float travel = boresightVisible ? ease(lockAge / .25f) : 1;
      const ImVec2 centre{boresight.x + (point.x - boresight.x) * travel, boresight.y + (point.y - boresight.y) * travel};
      const float radius = ring + (26 - ring) * ease(float(frame.lockProgress));
      draw->AddCircle(centre, radius, IM_COL32(0, 0, 0, 120), 48, 3.5f);
      draw->AddCircle(centre, radius, lockColor, 48, frame.seekerReady ? 2.f + pulse : 1.6f);
    } else if (boresightVisible) {
      brokenRing(draw, boresight, ring, faded(kAccent, .85f), 1.5f, float(now) * .6f);
      draw->AddCircleFilled(boresight, 1.6f, kAccent);
    }
  }
  if (targetVisible) {
    if (infrared) {
      diamond(draw, point, 13, IM_COL32(0, 0, 0, 150), 3.5f);
      diamond(draw, point, 13, lockColor, frame.seekerReady ? 2.2f : 1.6f);
    } else {
      // A radar lock closes in on its target, then holds as a solid box.
      const float half = 16 + 30 * (1 - ease(lockAge / .35f));
      draw->AddRect({point.x - half, point.y - half}, {point.x + half, point.y + half}, IM_COL32(0, 0, 0, 150), 0, 0, 3.5f);
      draw->AddRect({point.x - half, point.y - half}, {point.x + half, point.y + half}, lockColor, 0, 0,
                    frame.seekerReady ? 2.f : 1.5f);
      if (!frame.seekerReady) brackets(draw, point, half + 6, faded(lockColor, .5f + .5f * pulse));
    }
    const char* word = !frame.seekerReady ? "ACQUIRING" :
        frame.envelope.targetRange < frame.envelope.minimum ? "TOO CLOSE" :
        frame.envelope.inside ? "LOCK" : "OUT OF RANGE";
    centred(draw, {point.x, point.y - 38}, word, lockColor, 14);
    centred(draw, {point.x, point.y + 22},
            number(range / 1000, 1) + " km    " + (frame.envelope.closure >= 0 ? "+" : "") +
                number(frame.envelope.closure * 3.6) + " km/h", lockColor);
    // Launch zone: the target's range between the nearest and furthest shot.
    if (frame.envelope.kinematicRange > frame.envelope.minimum) {
      const float left = point.x + 38, top = point.y - 26, bottom = point.y + 26;
      const double span = frame.envelope.kinematicRange * 1.25;
      const auto at = [&](double value) { return bottom - float(std::clamp(value / span, 0., 1.)) * (bottom - top); };
      draw->AddLine({left, top}, {left, bottom}, faded(kText, .45f), 1.2f);
      draw->AddLine({left, at(frame.envelope.kinematicRange)}, {left, at(frame.envelope.minimum)},
                    frame.envelope.inside ? kAccent : kMuted, 3.5f);
      const float mark = at(frame.envelope.targetRange);
      draw->AddTriangleFilled({left + 3, mark}, {left + 10, mark - 4}, {left + 10, mark + 4}, lockColor);
    }
  } else if (missile && target.id) {
    text(draw, {display.x * .5f - 62, display.y * .5f + 120}, "TARGET OFF SCREEN", lockColor, 13);
  }

  // The pilot's own missiles, so a shot can be followed to its target.
  for (const Vec3& position : frame.ownMissiles) {
    ImVec2 at;
    if (!renderer.projectToScreen(position, at.x, at.y, depth)) continue;
    draw->AddTriangle({at.x, at.y - 7}, {at.x - 6, at.y + 5}, {at.x + 6, at.y + 5}, kText, 1.4f);
  }
}
}

namespace {

// ---- Damage -----------------------------------------------------------------

const char* partName(DamagePart part) {
  switch (part) {
    case DamagePart::LeftWing: return "LEFT WING";
    case DamagePart::RightWing: return "RIGHT WING";
    case DamagePart::Tail: return "TAIL";
    case DamagePart::LeftEngine: return "LEFT ENGINE";
    case DamagePart::RightEngine: return "RIGHT ENGINE";
    default: return "FUSELAGE";
  }
}
// Sound parts are a quiet blue; damage runs through amber to red.
ImU32 damageColor(float damage) {
  if (damage < .02f) return IM_COL32(104, 150, 198, 255);
  return damage < .5f ? blend(kAmber, IM_COL32(255, 150, 80, 255), damage / .5f)
                      : blend(IM_COL32(255, 150, 80, 255), kDanger, (damage - .5f) / .5f);
}

// The aircraft seen from above, each part coloured by what is left of it, with
// the hit points underneath and a line for every part in trouble.
void drawDamage(ImDrawList* draw, ImVec2 corner, const HudFrame& frame) {
  const float width = 132, height = 172;
  const ImVec2 min = corner, max{corner.x + width, corner.y + height};
  const float flash = float(std::clamp(frame.damageFlash / .6, 0., 1.));
  panel(draw, min, max);
  if (flash > 0) draw->AddRect(min, max, faded(kDanger, flash), 5, 0, 2.5f);
  text(draw, {min.x + 10, min.y + 7}, "AIRFRAME", kMuted, 11);
  // Outline in a 100 x 120 box, nose up.
  const ImVec2 origin{min.x + 16, min.y + 22};
  const auto at = [&](float x, float y) { return ImVec2{origin.x + x, origin.y + y * .98f}; };
  const auto shape = [&](DamagePart part, std::initializer_list<ImVec2> outline) {
    const float damage = frame.damage[part];
    const ImU32 color = damageColor(damage);
    std::vector<ImVec2> points(outline);
    const bool hit = flash > 0 && frame.damagedPart == part;
    draw->AddConvexPolyFilled(points.data(), int(points.size()),
                              damage >= 1 ? IM_COL32(70, 18, 16, 235) : faded(color, damage < .02f ? .22f : .5f + .3f * flash * hit));
    draw->AddPolyline(points.data(), int(points.size()), damage >= 1 ? faded(kDanger, .9f) : color, ImDrawFlags_Closed,
                      hit ? 2.4f : 1.4f);
  };
  shape(DamagePart::LeftWing, {at(43, 46), at(6, 76), at(6, 87), at(43, 80)});
  shape(DamagePart::RightWing, {at(57, 46), at(57, 80), at(94, 87), at(94, 76)});
  shape(DamagePart::Tail, {at(50, 90), at(72, 108), at(72, 115), at(50, 110), at(28, 115), at(28, 108)});
  shape(DamagePart::Fuselage, {at(50, 2), at(56, 18), at(57, 88), at(54, 104), at(46, 104), at(43, 88), at(44, 18)});
  shape(DamagePart::LeftEngine, {at(43.5f, 84), at(49, 84), at(49, 112), at(43.5f, 112)});
  shape(DamagePart::RightEngine, {at(51, 84), at(56.5f, 84), at(56.5f, 112), at(51, 112)});
  // Hit points.
  const float health = float(std::clamp(frame.health, 0., 100.));
  const ImU32 healthColor = health > 60 ? kAccent : health > 30 ? kAmber : kDanger;
  text(draw, {min.x + 10, max.y - 31}, "HULL", kMuted, 11);
  textRight(draw, {max.x - 10, max.y - 33}, number(health) + "%", healthColor, 14);
  bar(draw, {min.x + 10, max.y - 14}, {max.x - 10, max.y - 8}, health / 100, healthColor);
  // What is wrong, most serious first, stacked above the panel.
  float y = min.y - 18;
  const auto warn = [&](const std::string& line, ImU32 color) {
    textRight(draw, {max.x, y}, line, color, 12);
    y -= 15;
  };
  for (const DamagePart part : {DamagePart::Tail, DamagePart::RightWing, DamagePart::LeftWing,
                                DamagePart::RightEngine, DamagePart::LeftEngine}) {
    const float damage = frame.damage[part];
    if (damage < .15f) continue;
    const bool engine = part == DamagePart::LeftEngine || part == DamagePart::RightEngine;
    if (damage >= 1) warn(std::string(partName(part)) + (engine ? " OUT" : " GONE"), kDanger);
    else warn(std::string(partName(part)) + " " + number((1 - damage) * 100) + "%", damage > .5f ? kDanger : kAmber);
  }
}

// ---- Chat and scores --------------------------------------------------------

void drawChat(ImDrawList* draw, ImVec2 bottomLeft, const HudFrame& frame) {
  float y = bottomLeft.y - 17 * float(frame.chat.size());
  for (const ChatEntry* entry : frame.chat) {
    const float alpha = ChatLog::opacity(*entry, frame.now, frame.chatOpen);
    const std::string line = entry->notice ? entry->text : entry->name + ":  " + entry->text;
    const float width = textWidth(line, 13);
    draw->AddRectFilled({bottomLeft.x - 6, y - 1}, {bottomLeft.x + width + 8, y + 16}, IM_COL32(9, 16, 25, int(120 * alpha)), 3);
    if (entry->notice) {
      text(draw, {bottomLeft.x, y}, line, faded(kAmber, alpha), 13);
    } else {
      const std::string name = entry->name + ":  ";
      text(draw, {bottomLeft.x, y}, name, faded(entry->own ? kAccent : kBlue, alpha), 13);
      text(draw, {bottomLeft.x + textWidth(name, 13), y}, entry->text, faded(kText, alpha), 13);
    }
    y += 17;
  }
}

void drawScores(ImDrawList* draw, ImVec2 display, const HudFrame& frame) {
  const float width = std::min(display.x - 80, 520.f), row = 24;
  const float height = 58 + row * float(std::max<std::size_t>(frame.scores.size(), 1));
  const ImVec2 min{(display.x - width) * .5f, std::max(90.f, (display.y - height) * .4f)}, max{min.x + width, min.y + height};
  panel(draw, min, max, IM_COL32(9, 16, 25, 235));
  text(draw, {min.x + 18, min.y + 12}, "PILOTS", kText, 17);
  if (frame.pingMs >= 0) textRight(draw, {max.x - 18, min.y + 15}, number(frame.pingMs) + " ms", kMuted, 12);
  const float nameX = min.x + 18, typeX = min.x + width * .5f, killsX = max.x - 96, deathsX = max.x - 28;
  text(draw, {nameX, min.y + 36}, "NAME", kMuted, 11);
  text(draw, {typeX, min.y + 36}, "AIRCRAFT", kMuted, 11);
  textRight(draw, {killsX, min.y + 36}, "KILLS", kMuted, 11);
  textRight(draw, {deathsX, min.y + 36}, "LOSSES", kMuted, 11);
  float y = min.y + 54;
  for (const auto& score : frame.scores) {
    if (score.self) draw->AddRectFilled({min.x + 8, y - 3}, {max.x - 8, y + row - 5}, IM_COL32(64, 164, 255, 40), 3);
    const ImU32 color = !score.alive ? kMuted : score.self ? kText : kText;
    text(draw, {nameX, y}, score.name.empty() ? "Pilot" : score.name, score.self ? kAccent : color, 14);
    text(draw, {typeX, y}, std::string(aircraftDefinition(score.type).key), kMuted, 13);
    textRight(draw, {killsX, y}, number(score.kills), color, 14);
    textRight(draw, {deathsX, y}, number(score.deaths), color, 14);
    y += row;
  }
}

// A switch on the status strip: lit when on, dim when off.
float pill(ImDrawList* draw, ImVec2 at, const std::string& label, bool on, ImU32 color = kAccent) {
  const float width = textWidth(label, 12) + 18;
  draw->AddRectFilled(at, {at.x + width, at.y + 22}, on ? faded(color, .22f) : IM_COL32(255, 255, 255, 14), 4);
  draw->AddRect(at, {at.x + width, at.y + 22}, on ? faded(color, .8f) : IM_COL32(255, 255, 255, 40), 4);
  text(draw, {at.x + 9, at.y + 4}, label, on ? color : kMuted, 12);
  return width + 6;
}

}  // namespace

void drawHud(const HudFrame& frame, const HudSettings& settings, const Renderer& renderer) {
  if (!frame.local || !frame.controls) return;
  auto* draw = ImGui::GetBackgroundDrawList();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  if (display.x < 320 || display.y < 240) return;
  // Chat is a conversation, not an instrument: it stays with the HUD hidden.
  const ImVec2 chatAnchor{24, display.y - (frame.chatOpen ? 84.f : 50.f)};
  if (!settings.show) {
    // The map is asked for by name, so it opens even with the HUD hidden.
    if (frame.fullMap) drawFullMap(draw, display, frame, renderer);
    drawChat(draw, chatAnchor, frame);
    if (frame.showScores) drawScores(draw, display, frame);
    return;
  }
  const auto& state = *frame.local;
  const auto& controls = *frame.controls;
  const auto& flight = frame.instruments;
  const float cx = display.x * .5f, cy = display.y * .5f;
  const bool cockpit = frame.cameraMode == CameraMode::FirstPerson;
  const auto& definition = aircraftDefinition(frame.type);
  const bool armed = definition.gun.has_value();

  // Being hit reddens the edges of the view for a moment.
  if (frame.damageFlash > 0) {
    const ImU32 edge = faded(kDanger, float(std::clamp(frame.damageFlash / .6, 0., 1.)) * .38f), none = faded(kDanger, 0);
    const float depth = std::min(display.x, display.y) * .16f;
    draw->AddRectFilledMultiColor({0, 0}, {display.x, depth}, edge, edge, none, none);
    draw->AddRectFilledMultiColor({0, display.y - depth}, display, none, none, edge, edge);
    draw->AddRectFilledMultiColor({0, 0}, {depth, display.y}, edge, none, none, edge);
    draw->AddRectFilledMultiColor({display.x - depth, 0}, display, none, edge, edge, none);
  }

  // ---- Flight panel: speed and height large, the rest beside them ----
  {
    const float x = 20, y = 18, width = 290;
    panel(draw, {x, y}, {x + width, y + 146});
    std::string title(definition.displayName.substr(0, definition.displayName.find(" |")));
    for (char& c : title) c = char(std::toupper(static_cast<unsigned char>(c)));
    text(draw, {x + 14, y + 9}, title, kText, 12);
    textRight(draw, {x + width - 14, y + 9}, cameraName(frame.cameraMode), kMuted, 11);
    draw->AddLine({x + 14, y + 28}, {x + width - 14, y + 28}, kBorder);
    text(draw, {x + 14, y + 34}, "SPEED  km/h", kMuted, 11);
    text(draw, {x + 14, y + 47}, number(flight.ias * 3.6), kText, 30);
    text(draw, {x + 152, y + 34}, "ALTITUDE  m", kMuted, 11);
    text(draw, {x + 152, y + 47}, number(flight.alt_msl), kText, 30);
    const std::string climb = (flight.vs >= 0 ? "+" : "") + number(flight.vs);
    text(draw, {x + 14, y + 84}, "MACH " + number(flight.mach, 2), kMuted, 12);
    if (settings.showGLoad)
      text(draw, {x + 104, y + 84}, number(flight.g_load, 1) + " G", std::abs(flight.g_load) > 7 ? kAmber : kMuted, 12);
    text(draw, {x + 152, y + 84}, "V/S " + climb + " m/s", kMuted, 12);
    // Throttle, with reheat shown once it is lit.
    const bool reheat = state.afterburner[0] > .05 || state.afterburner[1] > .05;
    text(draw, {x + 14, y + 105}, reheat ? "REHEAT" : "THRUST", reheat ? kAmber : kMuted, 11);
    textRight(draw, {x + 138, y + 104}, number(controls.throttle[0] * 100) + "%", kText, 12);
    bar(draw, {x + 14, y + 124}, {x + 138, y + 130}, float(controls.throttle[0]), reheat ? kAmber : kAccent);
    // Fuel, as a share of what the aircraft took off with.
    const double capacity = definition.flight.fuel_capacity > 0 ? definition.flight.fuel_capacity : definition.flight.initial_fuel;
    const double fuel = state.fuel_mass < 0 ? definition.flight.initial_fuel : state.fuel_mass;
    const float fuelShare = capacity > 0 ? float(std::clamp(fuel / capacity, 0., 1.)) : 0;
    text(draw, {x + 152, y + 105}, "FUEL", fuelShare < .15f ? kDanger : kMuted, 11);
    textRight(draw, {x + width - 14, y + 104}, number(fuel) + " kg", fuelShare < .15f ? kDanger : kText, 12);
    bar(draw, {x + 152, y + 124}, {x + width - 14, y + 130}, fuelShare, fuelShare < .15f ? kDanger : kBlue);
  }

  if (settings.showHeading && display.x > 760) {
    panel(draw, {cx-136,18}, {cx+136,62});
    for (int delta=-30;delta<=30;delta+=10) {
      const float x = cx + delta * 3.7f;
      const int heading = (static_cast<int>(std::lround(flight.hdg_deg))+delta+360)%360;
      draw->AddLine({x,50},{x,57},kMuted);
      if (delta) text(draw,{x-10,26},number(heading),kMuted,12);
    }
    draw->AddRectFilled({cx-28,22},{cx+28,48},IM_COL32(18,52,84,255),4);
    draw->AddRect({cx-28,22},{cx+28,48},faded(kBlue,.7f),4);
    const std::string heading = number(flight.hdg_deg);
    text(draw,{cx-textWidth(heading,18)*.5f,25},heading,kText,18);
  }

  // Who the pilot is flying with, top right.
  if (frame.multiplayer) {
    const std::string status = frame.dogfight ? "DOGFIGHT  /  " + number(double(frame.remotes.size())) + " BANDITS"
        : "ONLINE  /  " + number(double(frame.scores.size())) + " PILOTS" + (frame.pingMs >= 0 ? "  /  " + number(frame.pingMs) + " ms" : "");
    const float width = textWidth(status, 12) + 34;
    panel(draw, {display.x - 20 - width, 18}, {display.x - 20, 44});
    draw->AddCircleFilled({display.x - 8 - width, 31}, 4, frame.dogfight ? kDanger : kAccent);
    text(draw, {display.x - width + 2, 24}, status, kText, 12);
  }

  // Attitude marks belong to the flight-deck view, where screen and aircraft
  // orientation coincide. In exterior views they would obscure the airframe.
  if (cockpit && settings.showPitchLadder) {
    const float roll = static_cast<float>(flight.roll_deg * kDeg2Rad);
    const auto point = [&](float x, float y) {
      return ImVec2{cx+x*std::cos(roll)-y*std::sin(roll),
                    cy+x*std::sin(roll)+y*std::cos(roll)};
    };
    for (int pitch=-30;pitch<=30;pitch+=10) {
      const float y = static_cast<float>((flight.pitch_deg-pitch)*5.0);
      if (std::abs(y)>155) continue;
      draw->AddLine(point(-90,y),point(-25,y),kAccent,1.3f);
      draw->AddLine(point(25,y),point(90,y),kAccent,1.3f);
      if (pitch) text(draw,point(-116,y-7),number(pitch),kAccent,12);
    }
    draw->AddLine({cx-24,cy},{cx-6,cy+5},kText,2);
    draw->AddLine({cx+6,cy+5},{cx+24,cy},kText,2);
  }
  if (cockpit && settings.showFlightPathMarker && state.vel_ned.norm()>10) {
    float x,y,depth;
    const Vec3 path = state.pos_ned + state.vel_ned.normalized()*1500;
    if (renderer.projectToScreen(path,x,y,depth)) {
      draw->AddCircle({x,y},7,kAccent,24,1.5f);
      draw->AddLine({x-19,y},{x-7,y},kAccent,1.5f);
      draw->AddLine({x+7,y},{x+19,y},kAccent,1.5f);
    }
  }
  // Where the gun points: a ring with a pip, closing up while firing. Own
  // rounds striking, and a kill, are marked on it.
  ImVec2 sight{cx, cy};
  const bool gunsight = armed && settings.showGunsight && frame.gunPointValid && !frame.fullMap;
  if (gunsight) {
    float depth;
    if (renderer.projectToScreen(frame.gunPoint,sight.x,sight.y,depth)) {
      const ImU32 color = frame.firing ? kAmber : kAccent;
      const float radius = frame.firing ? 9.f : 11.f;
      draw->AddCircle(sight,radius,IM_COL32(0,0,0,130),28,3.2f);
      draw->AddCircle(sight,radius,color,28,1.5f);
      for (const float angle : {0.f, 1.5708f, 3.1416f, 4.7124f}) {
        const ImVec2 dir{std::cos(angle), std::sin(angle)};
        draw->AddLine({sight.x+dir.x*(radius+3),sight.y+dir.y*(radius+3)},{sight.x+dir.x*(radius+9),sight.y+dir.y*(radius+9)},color,1.5f);
      }
      draw->AddCircleFilled(sight,1.8f,color);
    }
  }
  if (frame.mouseAim) {
    // The ring is where the pilot asked to go; the cross is where the nose is.
    float x,y,depth;
    if (renderer.projectToScreen(frame.mouseAimPoint,x,y,depth)) {
      draw->AddCircle({x,y},15,IM_COL32(0,0,0,150),32,3.5f);
      draw->AddCircle({x,y},15,kText,32,1.6f);
      draw->AddCircleFilled({x,y},1.6f,kText);
    }
    if (!gunsight && renderer.projectToScreen(frame.nosePoint,x,y,depth)) {
      draw->AddLine({x-8,y},{x-3,y},kAccent,1.6f);
      draw->AddLine({x+3,y},{x+8,y},kAccent,1.6f);
      draw->AddLine({x,y-8},{x,y-3},kAccent,1.6f);
      draw->AddLine({x,y+3},{x,y+8},kAccent,1.6f);
    }
  }
  if (frame.hitMarker > 0 || frame.killMarker > 0) {
    const bool kill = frame.killMarker > 0;
    const float life = float(kill ? std::clamp(frame.killMarker / 1.2, 0., 1.) : std::clamp(frame.hitMarker / .25, 0., 1.));
    const ImU32 color = faded(kill ? kDanger : kText, life);
    const float inner = kill ? 9.f : 7.f, outer = kill ? 20.f : 14.f;
    for (const float sx : {-1.f, 1.f})
      for (const float sy : {-1.f, 1.f}) {
        draw->AddLine({sight.x+sx*inner,sight.y+sy*inner},{sight.x+sx*outer,sight.y+sy*outer},faded(IM_COL32(0,0,0,255),life*.6f),kill?4.5f:3.5f);
        draw->AddLine({sight.x+sx*inner,sight.y+sy*inner},{sight.x+sx*outer,sight.y+sy*outer},color,kill?2.6f:1.8f);
      }
    if (kill) centred(draw, {cx, 76}, "ENEMY DESTROYED", faded(kDanger, std::min(1.f, life * 2)), 20);
  }

  const bool mapShown = settings.showMinimap && !frame.fullMap && display.x > 760 && display.y > 480;
  float mapSize = 0;
  if (mapShown) {
    MapFrame map;
    map.size = mapSize = std::clamp(display.y * .24f, 150.f, 240.f);
    map.x = display.x - map.size - 20;
    map.y = display.y - map.size - 40;
    map.north = state.pos_ned.x;
    map.east = state.pos_ned.y;
    map.halfSpan = minimapHalfSpan(std::hypot(state.vel_ned.x, state.vel_ned.y));
    drawMap(draw, map, frame, renderer, false);
  }
  // The airframe diagram sits beside the map, once there is a fight to be in
  // or damage to show.
  if (!frame.fullMap && display.x > 900 && display.y > 520 && ((frame.multiplayer && armed) || frame.damage.any()))
    drawDamage(draw, {display.x - 20 - (mapShown ? mapSize + 12 : 0) - 132, display.y - 40 - 172}, frame);

  // ---- Status strip: the switches a pilot checks at a glance ----
  if (display.x > 640) {
    const bool gear = controls.gear01 > .5;
    const std::string flaps = "FLAPS " + number(controls.flap01 * 100) + "%";
    const std::string phase = frame.paused ? "PAUSED" : frame.parkingBrake ? "PARKING BRAKE" : "";
    float width = textWidth("GEAR", 12) + 24 + textWidth(flaps, 12) + 24 + textWidth("AIRBRAKE", 12) + 24;
    const bool maneuver = hasManeuverMode(definition.flight.control_law);
    if (maneuver) width += textWidth("MANEUVER", 12) + 24;
    if (!phase.empty()) width += textWidth(phase, 12) + 24;
    float x = cx - width * .5f;
    const float y = display.y - 44;
    x += pill(draw, {x, y}, "GEAR", gear, gear && flight.ias > 140 ? kAmber : kAccent);
    x += pill(draw, {x, y}, flaps, controls.flap01 > .01);
    x += pill(draw, {x, y}, "AIRBRAKE", controls.spoiler01 > .5, kAmber);
    if (maneuver)
      x += pill(draw, {x, y}, "MANEUVER", controls.maneuver_mode,
                controls.gear01 >= .5 || flight.tas >= 300 || !state.fcs_enabled ? kMuted : kAmber);
    if (!phase.empty()) pill(draw, {x, y}, phase, true, kAmber);
  }
  if (!frame.menuOpen) {
    std::string hints = "ESC MENU   TAB CAMERA   N MAP   F4 HUD";
    if (frame.multiplayer) hints += "   / CHAT   K PILOTS";
    hints += frame.mouseAimEnabled ? "   X MOUSE AIM ON" : "   X MOUSE AIM";
    text(draw,{24,display.y-24},hints,kMuted,11);
    if (frame.mouseAim)
      text(draw,{24,display.y-40},"MOUSE AIM   WASD/QE OVERRIDE   HOLD RIGHT MOUSE TO LOOK",kMuted,11);
  }
  drawChat(draw, {chatAnchor.x, chatAnchor.y - (frame.mouseAim ? 14.f : 0.f)}, frame);
  if (settings.showStall && flight.stall_warn) centred(draw,{cx,cy-104},"STALL",kDanger,22);
  if (!frame.missileOutcome.empty()) {
    const float width = textWidth(frame.missileOutcome, 22);
    const ImU32 color = frame.missileOutcome == "MISSILE HIT" ? kAccent : kDanger;
    panel(draw, {cx - width * .5f - 18, cy - 152},
          {cx + width * .5f + 18, cy - 116});
    text(draw, {cx - width * .5f, cy - 146}, frame.missileOutcome, color, 22);
  }
  if (settings.showFps) textRight(draw,{display.x-24,display.y-26},number(renderer.stats().fps)+" FPS",kMuted,11);

  if (frame.multiplayer && armed && display.x > 760) drawWeapons(draw, display, frame, renderer);
  if (!frame.alive) {
    panel(draw,{cx-170,cy-40},{cx+170,cy+44},IM_COL32(9,16,25,235));
    centred(draw,{cx,cy-28},frame.multiplayer?"AIRCRAFT DESTROYED":"AIRCRAFT CRASHED",kDanger,22);
    if (frame.multiplayer) {
      centred(draw,{cx,cy+2},"Back in the air in "+number(frame.respawnSeconds,1)+" s",kMuted,13);
      bar(draw,{cx-140,cy+26},{cx+140,cy+31},1-float(std::clamp(frame.respawnSeconds/4.,0.,1.)),kBlue);
    } else {
      centred(draw,{cx,cy+6},"F2  fly again      F3  back to the runway",kMuted,13);
    }
  }
  if (!frame.bannerTitle.empty() && !frame.menuOpen) {
    const float width = std::max(360.f, std::max(textWidth(frame.bannerTitle, 22), textWidth(frame.bannerDetail, 13)) + 56);
    panel(draw,{cx-width*.5f,cy-150},{cx+width*.5f,cy-78},IM_COL32(9,16,25,235));
    centred(draw,{cx,cy-140},frame.bannerTitle,frame.bannerProblem?kDanger:kText,22);
    centred(draw,{cx,cy-108},frame.bannerDetail,kMuted,13);
  }
  if (settings.showLabels) for (const auto& remote:frame.remotes) {
    const double distance=(remote.state.pos_ned-state.pos_ned).norm();
    if (!remote.alive || distance>settings.labelMaxDistance) continue;
    // The weapon's target carries its own marker and readout.
    if (frame.missileSelected && frame.seekerTarget.id == remote.entity) continue;
    float x,y,depth;
    if (!renderer.projectToScreen(remote.state.pos_ned,x,y,depth)) continue;
    const std::string label=(remote.name.empty()?"Aircraft":remote.name)+"  "+number(distance/1000,1)+" km";
    const auto color=frame.dogfight?kDanger:kBlue;
    draw->AddTriangle({x,y-8},{x-4,y-15},{x+4,y-15},color,1.5f);
    centred(draw,{x,y-34},label,color,13);
    // What is left of it, so a pilot knows which target is nearly finished.
    if (remote.health < 99.5) bar(draw,{x-18,y-18},{x+18,y-15},float(remote.health/100),remote.health>50?kAccent:remote.health>25?kAmber:kDanger);
  }
  if (frame.fullMap) drawFullMap(draw, display, frame, renderer);
  if (frame.showScores) drawScores(draw, display, frame);
}
void shutdownHud() {}
}  // namespace ofs::client
