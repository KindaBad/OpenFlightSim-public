#pragma once
// Gameplay HUD.
//
// Deliberately separate from the developer ImGui windows: this is the small
// flight instrument strip a player reads, drawn with ImGui's background draw
// list so it costs no extra draw calls or font switching.

#include "camera.hpp"
#include "ofs/weapons.hpp"
#include "renderer.hpp"
#include "settings.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace ofs::client {

// Everything the HUD needs for one frame. Purely presentational values.
struct HudFrame {
  const State* local{};
  const Controls* controls{};
  Instruments instruments{};
  CameraMode cameraMode{CameraMode::Chase};
  AircraftType type{AircraftType::A320};
  bool parkingBrake{}, paused{};
  std::span<const RemoteAircraft> remotes;
  // Empty in offline mode, in which case the HUD hides the combat block.
  bool multiplayer{};
  bool dogfight{};
  std::span<const weapons::Track> radarTracks;
  weapons::EntityRef selectedTarget{}, lockedTarget{};
  weapons::WeaponType missileWeapon{weapons::WeaponType::None};
  weapons::Envelope envelope;
  unsigned irCount{}, radarCount{};
  bool seekerReady{};
  bool missileSelected{};
  unsigned activeMissiles{};
  double missileSpeed{}, missileAge{};
  std::string missileOutcome;
  bool alive{true};
  double health{100};
  std::uint16_t ammo{600};
  bool gunReady{true};
  double respawnSeconds{};
  bool firing{};
  // Gun solution in body FRD, projected to screen by the caller when present.
  bool gunPointValid{};
  Vec3 gunPoint{};
};

struct HudSettings {
  bool show{true};
  bool showPitchLadder{true};
  bool showHeading{true};
  bool showFlightPathMarker{true};
  bool showGunsight{true};
  bool showLabels{true};
  float labelMaxDistance{6000};
  bool showGLoad{true};
  bool showStall{true};
  bool showFps{false};
};

// Draws the HUD using the renderer's projection for world-space markers.
void drawHud(const HudFrame& frame, const HudSettings& settings, const Renderer& renderer);

// Frees any cached GPU font handle. Called on shutdown.
void shutdownHud();

}  // namespace ofs::client
