#include "hud.hpp"
#include "map.hpp"
#include "ofs/units.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ofs::client {
namespace {
constexpr ImU32 kPanel = IM_COL32(24, 25, 24, 185);
constexpr ImU32 kBorder = IM_COL32(180, 177, 162, 75);
constexpr ImU32 kText = IM_COL32(236, 243, 246, 255);
constexpr ImU32 kMuted = IM_COL32(190, 190, 181, 255);
constexpr ImU32 kAccent = IM_COL32(160, 218, 133, 255);
constexpr ImU32 kAmber = IM_COL32(255, 192, 95, 255);
constexpr ImU32 kDanger = IM_COL32(255, 104, 93, 255);

std::string number(double value, int decimals = 0) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  return buffer;
}
void text(ImDrawList* draw, ImVec2 pos, const std::string& value, ImU32 color = kText,
          float size = 15) {
  draw->AddText(ImGui::GetFont(), size, {pos.x+1,pos.y+1}, IM_COL32(0,0,0,210), value.c_str());
  draw->AddText(ImGui::GetFont(), size, pos, color, value.c_str());
}
void panel(ImDrawList* draw, ImVec2 min, ImVec2 max) {
  draw->AddRectFilled(min, max, kPanel, 1);
  draw->AddRect(min, max, kBorder, 1);
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
}

void drawHud(const HudFrame& frame, const HudSettings& settings, const Renderer& renderer) {
  if (!frame.local || !frame.controls) return;
  auto* draw = ImGui::GetBackgroundDrawList();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  if (display.x < 320 || display.y < 240) return;
  if (!settings.show) {
    // The map is asked for by name, so it opens even with the HUD hidden.
    if (frame.fullMap) drawFullMap(draw, display, frame, renderer);
    return;
  }
  const auto& state = *frame.local;
  const auto& controls = *frame.controls;
  const auto& flight = frame.instruments;
  const float cx = display.x * .5f, cy = display.y * .5f;
  const bool cockpit = frame.cameraMode == CameraMode::FirstPerson;
  const auto& definition = aircraftDefinition(frame.type);
  const bool armed = definition.gun.has_value();

  // Compact battle telemetry keeps the aircraft silhouette unobstructed.
  text(draw,{24,22},std::string(definition.key) + "  /  " + cameraName(frame.cameraMode),kMuted,12);
  if (frame.dogfight) text(draw,{24,200},"DOGFIGHT / ENEMY BOTS",kDanger,12);
  const auto readout = [&](float y,const char* label,const std::string& value,ImU32 color=kText) {
    text(draw,{24,y},label,color,16);
    text(draw,{88,y},value,color,16);
  };
  readout(46,"THR",number(controls.throttle[0]*100)+" %");
  readout(68,"IAS",number(flight.ias*3.6)+" km/h");
  readout(90,"ALT",number(flight.alt_msl)+" m");
  readout(112,"MACH",number(flight.mach,2));
  if (armed) readout(134,"MG",number(frame.ammo),frame.ammo ? kText : kDanger);
  if (settings.showGLoad) readout(156,"LOAD",number(flight.g_load,1)+" G");
  if (hasManeuverMode(definition.flight.control_law) && controls.maneuver_mode)
    text(draw,{24,180},controls.gear01 >= .5 || flight.tas >= 300 || !state.fcs_enabled
         ? "MANEUVER STANDBY [M]" : "MANEUVER MODE [M]",kAmber,12);

  if (settings.showHeading && display.x > 760) {
    panel(draw, {cx-136,20}, {cx+136,68});
    for (int delta=-30;delta<=30;delta+=10) {
      const float x = cx + delta * 3.7f;
      const int heading = (static_cast<int>(std::lround(flight.hdg_deg))+delta+360)%360;
      draw->AddLine({x,55},{x,62},kMuted);
      if (delta) text(draw,{x-10,30},number(heading),kMuted,12);
    }
    draw->AddRectFilled({cx-28,26},{cx+28,52},IM_COL32(30,55,67,255),4);
    const std::string heading = number(flight.hdg_deg);
    text(draw,{cx-18,29},heading,kAccent,18);
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
  const bool gunsight = armed && settings.showGunsight && frame.gunPointValid;
  if (gunsight) {
    float x,y,depth;
    if (renderer.projectToScreen(frame.gunPoint,x,y,depth)) {
      draw->AddCircle({x,y},10,frame.firing?kAmber:kAccent,24,1.5f);
      draw->AddCircleFilled({x,y},2,kAccent);
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

  if (settings.showMinimap && !frame.fullMap && display.x > 760 && display.y > 480) {
    MapFrame map;
    map.size = std::clamp(display.y * .24f, 150.f, 240.f);
    map.x = display.x - map.size - 20;
    map.y = display.y - map.size - 40;
    map.north = state.pos_ned.x;
    map.east = state.pos_ned.y;
    map.halfSpan = minimapHalfSpan(std::hypot(state.vel_ned.x, state.vel_ned.y));
    drawMap(draw, map, frame, renderer, false);
  }

  const float bottom=display.y-48;
  if (display.x>640) {
    panel(draw,{cx-230,bottom-10},{cx+230,bottom+26});
    text(draw,{cx-216,bottom},std::string("GEAR ")+(controls.gear01>.5?"DOWN":"UP"),kText,13);
    text(draw,{cx-112,bottom},"FLAPS "+number(controls.flap01*100)+"%",kText,13);
    text(draw,{cx,bottom},"V/S "+number(flight.vs)+" m/s",kText,13);
    text(draw,{cx+132,bottom},frame.multiplayer && armed ? "HP "+number(frame.health)+"%" :
         frame.paused ? "PAUSED" : frame.parkingBrake ? "PARKED" : "IN FLIGHT",kAmber,13);
  }
  text(draw,{24,display.y-24},frame.mouseAimEnabled ? "F1 SETTINGS   F4 HUD   TAB CAMERA   N MAP   X MOUSE AIM ON"
       : "F1 SETTINGS   F4 HUD   TAB CAMERA   N MAP   X MOUSE AIM",kMuted,11);
  if (frame.mouseAim)
    text(draw,{24,display.y-40},"MOUSE AIM   WASD/QE OVERRIDE   HOLD RIGHT MOUSE TO LOOK",kMuted,11);
  if (settings.showStall && flight.stall_warn)
    text(draw,{cx-68,cy-100},"STALL WARNING",kDanger,20);
  if (!frame.missileOutcome.empty()) {
    const float width = ImGui::CalcTextSize(frame.missileOutcome.c_str()).x;
    const ImU32 color = frame.missileOutcome == "MISSILE HIT" ? kAccent : kDanger;
    panel(draw, {cx - width * .5f - 18, cy - 148},
          {cx + width * .5f + 18, cy - 112});
    text(draw, {cx - width * .5f, cy - 142}, frame.missileOutcome, color, 22);
  }
  if (settings.showFps) text(draw,{display.x-94,display.y-26},number(renderer.stats().fps)+" FPS",kMuted,11);

  if (frame.multiplayer && armed && display.x > 760) {
    const float x = display.x - 245, y = frame.dogfight ? 200 : 145;
    panel(draw, {x, y}, {x + 225, y + 245});
    text(draw, {x + 12, y + 10},
         frame.lockedTarget.id ? "RADAR TRACK" : "RADAR SEARCH", kAccent, 13);
    const ImVec2 center{x + 112, y + 150};
    draw->AddCircle(center, 70, kBorder, 48);
    draw->AddLine({center.x - 70, center.y}, {center.x + 70, center.y},
                  kBorder);
    draw->AddLine({center.x, center.y - 70}, {center.x, center.y + 70},
                  kBorder);
    text(draw, {x + 12, y + 30}, "90 km / +/-60 deg", kMuted, 11);
    for (const auto &track : frame.radarTracks) {
      const auto relative =
          state.att.inverseRotate(track.position - state.pos_ned);
      const double range = relative.norm();
      const float az = float(std::clamp(
          std::atan2(relative.y, relative.x) / (60 * kDeg2Rad), -1., 1.));
      const ImVec2 point{center.x + az * 65,
                         center.y + 65 -
                             float(std::min(range / 90000., 1.)) * 130};
      const bool selected = track.entity == frame.selectedTarget,
                 locked = track.entity == frame.lockedTarget;
      const auto color = locked ? kAmber : selected ? kAccent : kMuted;
      draw->AddCircleFilled(point, 3, color);
      if (selected)
        draw->AddRect({point.x - 6, point.y - 6}, {point.x + 6, point.y + 6},
                      color);
      if (locked)
        draw->AddCircle(point, 9, color, 16);
      float px, py, depth;
      if (renderer.projectToScreen(track.position, px, py, depth)) {
        draw->AddRect({px - 12, py - 12}, {px + 12, py + 12}, color, 0, 0,
                      locked ? 2.f : 1.f);
        if (selected)
          text(draw, {px + 16, py - 5}, number(range / 1000, 1) + " km", color,
               12);
      }
    }
    text(draw, {x + 12, y + 225}, "T/Y target  L lock  Space launch", kMuted, 11);
    panel(draw, {20, 210}, {310, 337});
    const std::string weapon =
        frame.missileSelected
            ? (frame.missileWeapon == weapons::WeaponType::Infrared ? "IR-90"
                                                                    : "AR-157")
            : "GUN";
    text(draw, {34, 220},
         weapon + "  IR " + number(frame.irCount) + " / AR " +
             number(frame.radarCount),
         kAccent, 16);
    text(draw, {34, 247},
         frame.seekerReady ? "SEEKER / SUPPORT READY" : "NO MISSILE SOLUTION",
         frame.seekerReady ? kAccent : kMuted, 12);
    text(draw, {34, 266},
         "Kinematic estimate " + number(frame.envelope.minimum / 1000, 1) +
             " - " + number(frame.envelope.kinematicRange / 1000, 1) + " km",
         kMuted, 12);
    text(draw, {34, 308},
         "Airborne " + number(frame.activeMissiles) + "  " +
             number(frame.missileAge, 1) + " s / " +
             number(frame.missileSpeed) + " m/s",
         kMuted, 12);
    text(draw, {34, 285},
         "Range " + number(frame.envelope.targetRange / 1000, 1) +
             " km   Closure " + number(frame.envelope.closure) + " m/s",
         frame.envelope.inside ? kAccent : kAmber, 12);
  }
  if (!frame.alive) {
    panel(draw,{cx-150,cy-30},{cx+150,cy+34});
    text(draw,{cx-128,cy-17},frame.multiplayer?"AIRCRAFT DESTROYED":"AIRCRAFT CRASHED",kDanger,20);
    text(draw,{cx-128,cy+9},frame.multiplayer?"Respawn in "+number(frame.respawnSeconds,1)+" seconds":"F2: fly again   F3: reset to runway",kMuted,13);
  }
  if (settings.showLabels) for (const auto& remote:frame.remotes) {
    const double distance=(remote.state.pos_ned-state.pos_ned).norm();
    if (!remote.alive || distance>settings.labelMaxDistance) continue;
    float x,y,depth;
    if (!renderer.projectToScreen(remote.state.pos_ned,x,y,depth)) continue;
    const std::string label=(remote.name.empty()?"Aircraft":remote.name)+"  "+number(distance/1000,1)+" km";
    const float width=ImGui::CalcTextSize(label.c_str()).x;
    const auto color=frame.dogfight?kDanger:IM_COL32(110,185,255,255);
    draw->AddTriangle({x,y-8},{x-4,y-15},{x+4,y-15},color,1.5f);
    text(draw,{x-width*.5f,y-34},label,color,14);
  }
  if (frame.fullMap) drawFullMap(draw, display, frame, renderer);
}
void shutdownHud() {}
}  // namespace ofs::client
