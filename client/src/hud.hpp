#pragma once
// Gameplay HUD.
//
// Deliberately separate from the developer ImGui windows: this is the small
// flight instrument strip a player reads, drawn with ImGui's background draw
// list so it costs no extra draw calls or font switching.

#include "camera.hpp"
#include "chat.hpp"
#include "damage_visuals.hpp"
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
  // The wheel brakes are being held on.
  bool braking{};
  // Standing on the ground to be repaired and rearmed: how far through the
  // wait, 0..1, or negative when there is nothing to wait for; and the seconds
  // left to say that it has just been done.
  double serviceProgress{-1}, serviced{};
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
  // What the selected weapon is looking at and how far its lock has built.
  weapons::EntityRef seekerTarget{};
  double lockProgress{};
  // One entry per station: the weapon it was loaded with, and whether it is still there.
  struct Station { weapons::WeaponType type{weapons::WeaponType::None}; bool mounted{}; };
  std::span<const Station> stations;
  // The pilot's own missiles in flight.
  std::span<const Vec3> ownMissiles;
  bool missileSelected{};
  unsigned activeMissiles{};
  double missileSpeed{}, missileAge{};
  std::string missileOutcome;
  // Flares and chaff left, or negative for an aircraft that carries none.
  int flares{-1}, chaff{-1};
  // Missiles flying at this pilot. One that has gone after a decoy is still
  // shown, marked as defeated, until it is seen to be gone.
  struct Threat {
    Vec3 position, velocity;
    weapons::WeaponType type{weapons::WeaponType::Infrared};
    bool decoyed{};
  };
  std::span<const Threat> threats;
  bool alive{true};
  double health{100};
  std::uint16_t ammo{600};
  bool gunReady{true};
  double respawnSeconds{}, respawnSpan{4};
  // The pilot under load: how much of the view is lost, 0..1, whether to
  // black (0) or red (1), and whether they are unconscious.
  double visionLoss{}, redOut{};
  bool unconscious{};
  // The pilot has ejected; and how far the eject key has been held, 0..1.
  bool ejected{};
  double ejectHold{};
  bool firing{};
  // Gun solution in body FRD, projected to screen by the caller when present.
  bool gunPointValid{};
  Vec3 gunPoint{};
  // Mouse aim: the option is on, and while it flies, where the aim and the
  // nose point, as distant world positions.
  bool mouseAimEnabled{}, mouseAim{};
  Vec3 mouseAimPoint{}, nosePoint{};
  // The full map is open, in place of the minimap.
  bool fullMap{};
  // Battle damage to the pilot's own aircraft, and the cues that go with a
  // fight: seconds left to show own rounds striking, a kill, and being hit.
  DamageView damage;
  DamagePart damagedPart{DamagePart::Fuselage};
  double hitMarker{}, killMarker{}, damageFlash{};
  // Chat lines to draw, oldest first, at the time `now`; `chatOpen` while the
  // pilot is typing.
  std::span<const ChatEntry* const> chat;
  double now{};
  bool chatOpen{};
  // Everyone in the game, for the scoreboard, which is drawn while it is held.
  struct Score {
    std::string name;
    AircraftType type{AircraftType::A320};
    unsigned kills{}, deaths{};
    bool self{}, alive{true};
  };
  std::span<const Score> scores;
  bool showScores{};
  int pingMs{-1};
  // The Esc menu is up: the HUD keeps to itself underneath it.
  bool menuOpen{};
  // A notice across the middle of the view while a game is being joined, or
  // after it could not be: a title and one line of explanation.
  std::string bannerTitle, bannerDetail;
  bool bannerProblem{};
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
  bool showMinimap{true};
};

// Draws the HUD using the renderer's projection for world-space markers.
void drawHud(const HudFrame& frame, const HudSettings& settings, const Renderer& renderer);

// Frees any cached GPU font handle. Called on shutdown.
void shutdownHud();

}  // namespace ofs::client
