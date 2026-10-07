#include "debug_ui.hpp"

#include "renderer.hpp"
#include "ofs_font.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace ofs::client {

UiContext::UiContext(SDL_Window* window, bool smokeFont) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  auto& style = ImGui::GetStyle();
  style.WindowRounding = 1;
  style.FrameRounding = 1;
  style.PopupRounding = 1;
  style.ScrollbarRounding = 1;
  style.WindowPadding = {14,12};
  style.FramePadding = {8,6};
  style.ItemSpacing = {8,8};
  style.Colors[ImGuiCol_WindowBg] = {.09f,.095f,.09f,.94f};
  style.Colors[ImGuiCol_Border] = {.42f,.41f,.36f,.5f};
  style.Colors[ImGuiCol_TitleBg] = {.13f,.14f,.13f,1};
  style.Colors[ImGuiCol_TitleBgActive] = {.24f,.25f,.21f,1};
  style.Colors[ImGuiCol_Button] = {.43f,.16f,.12f,1};
  style.Colors[ImGuiCol_ButtonHovered] = {.60f,.23f,.16f,1};
  style.Colors[ImGuiCol_ButtonActive] = {.72f,.29f,.19f,1};
  style.Colors[ImGuiCol_FrameBg] = {.20f,.21f,.18f,1};
  style.Colors[ImGuiCol_FrameBgHovered] = {.30f,.31f,.26f,1};
  style.Colors[ImGuiCol_FrameBgActive] = {.37f,.38f,.30f,1};
  style.Colors[ImGuiCol_CheckMark] = {.78f,.72f,.48f,1};
  style.Colors[ImGuiCol_SliderGrab] = {.68f,.63f,.43f,1};
  style.Colors[ImGuiCol_SliderGrabActive] = {.89f,.81f,.54f,1};
  style.Colors[ImGuiCol_Header] = {.30f,.31f,.25f,1};
  style.Colors[ImGuiCol_HeaderHovered] = {.41f,.42f,.33f,1};
  style.Colors[ImGuiCol_HeaderActive] = {.48f,.47f,.34f,1};
  // A slightly larger default font keeps the diagnostics readable at 1280x800
  // without a separate atlas.
  ImGui::GetIO().FontGlobalScale = 1.0f;
  if (!smokeFont) {
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.SizePixels = 30;
    auto* font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(ofs_ui_font), sizeof(ofs_ui_font), 30, &config);
    font->Scale = .5f;
  }
  if (!ImGui_ImplSDL3_InitForOther(window)) {
    ImGui::DestroyContext();
    throw std::runtime_error("ImGui SDL3 backend failed");
  }
}

UiContext::~UiContext() {
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

void debugUi(const Simulator& sim, Controls& controls, const Camera& camera,
             const FixedStepClock& clock, unsigned steps, double frameTime,
             double measuredTicks, const Renderer& renderer, const std::string& assetName,
             std::size_t aircraftCount, bool gamepad, UiSettings& ui, GraphicsSettings& settings) {
  const Renderer::Stats& stats = renderer.stats();
  if (!settings.showDevOverlay) {
    ImGui::SetNextWindowPos({std::max(16.f,ImGui::GetIO().DisplaySize.x-340),20}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(.86f);
    ImGui::Begin("Flight controls", nullptr, ImGuiWindowFlags_NoDecoration |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("FLIGHT OPERATIONS");
    ImGui::Separator();
    if (!ui.multiplayer) {
      if (ImGui::Button("Fly now [F2]")) ui.resetAirborne = true;
      ImGui::SameLine();
      if (ImGui::Button("Park [F3]")) ui.resetParked = true;
      ImGui::SameLine();
      if (ImGui::Button(ui.paused ? "Resume [P]" : "Pause [P]")) ui.paused = !ui.paused;
    } else ImGui::TextUnformatted(ui.dogfight ? "DOGFIGHT / ENEMY BOTS" : "ONLINE FLIGHT");
    if (ui.botsAvailable && (!ui.multiplayer || ui.dogfight)) {
      if (ImGui::Button(ui.dogfight ? "End dogfight [F5]" : "Fight bots [F5]"))
        ui.toggleDogfight = true;
      if (ui.dogfight) ImGui::TextUnformatted("L lock / Space launch   T/Y target");
    }
    if (hasManeuverMode(sim.config().control_law))
      ImGui::Checkbox("Maneuver mode [M]", &controls.maneuver_mode);
    if (!renderer.hasAircraft()) ImGui::TextColored({1,.5f,.3f,1}, "Aircraft asset unavailable - open F1");
    ImGui::End();
  }

  // ---- Simulation diagnostics ----
  if (settings.showDevOverlay) {
    ImGui::SetNextWindowPos({14, 14}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({372, 560}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("OpenFlightSim | M3.6", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::Text("%s | %s", stats.backend.c_str(), OFS_BUILD_TYPE);
      ImGui::Text("%.1f FPS | frame %.2f ms | CPU %.2f ms", stats.fps, frameTime * 1000.0,
                  stats.cpuFrameMs);
      if (stats.gpuFrameMs > 0.0)
        ImGui::Text("GPU %.2f ms | draws %u | tris %u", stats.gpuFrameMs, stats.drawCalls,
                    stats.triangles);
      else
        ImGui::Text("Draws %u | tris %u | particles %u", stats.drawCalls, stats.triangles,
                    stats.activeParticles);
      ImGui::Text("Aircraft drawn %u | %s | shadow %u x %u", stats.aircraftDrawn, stats.lodTier.c_str(),
                  stats.shadowCascades, stats.shadowMapSize);
      ImGui::Text("Particles %u / 4096 | peak %zu | prep %.3f ms", stats.activeParticles,
                  stats.particlePeak, stats.preparationMs);
      ImGui::Text("Simulation 120 Hz | %.1f ticks/s measured", measuredTicks);
      ImGui::Text("Steps/frame %u | ticks %llu", steps,
                  static_cast<unsigned long long>(clock.ticks()));
      ImGui::Text("Interpolation %.3f | dropped %.3f s", clock.alpha(), clock.droppedTime());
      ImGui::Separator();
      ImGui::Text("Asset: %s", assetName.c_str());
      ImGui::Text("Aircraft in scene: %zu", aircraftCount);
      ImGui::Separator();

      const auto& s = sim.state();
      const auto instruments = sim.instruments();
      ImGui::Text("Aircraft | simulation time %.2f s", s.time);
      ImGui::Text("N/E/D m: %.2f  %.2f  %.2f", s.pos_ned.x, s.pos_ned.y, s.pos_ned.z);
      ImGui::Text("Velocity m/s: %.2f  %.2f  %.2f", s.vel_ned.x, s.vel_ned.y, s.vel_ned.z);
      ImGui::Text("Roll/Pitch/Hdg: %.1f / %.1f / %.1f deg", instruments.roll_deg,
                  instruments.pitch_deg, instruments.hdg_deg);
      ImGui::Text("Altitude %.1f m | AGL %.1f m", instruments.alt_msl, instruments.agl);
      ImGui::Text("TAS %.1f | IAS %.1f | Mach %.3f", instruments.tas, instruments.ias,
                  instruments.mach);
      ImGui::Text("AoA %.2f | sideslip %.2f deg", instruments.alpha_deg, instruments.beta_deg);
      ImGui::Text("Load %.2f g | stall %s", instruments.g_load,
                  instruments.stall_warn ? "YES" : "no");
      ImGui::Text("Vertical speed %.2f m/s", instruments.vs);
      ImGui::Text("p/q/r: %.3f %.3f %.3f rad/s", s.omega_body.x, s.omega_body.y, s.omega_body.z);
      ImGui::Text("Engine spool: %.2f / %.2f", s.n1[0], s.n1[1]);
      const auto mass=sim.massProperties();const auto aero=sim.evalAero();const auto thrust=sim.evalThrust();const auto air=sim.airData();
      ImGui::Text("Mass %.1f kg | fuel %.1f | payload %.1f",mass.mass,s.fuel_mass,s.payload_mass);
      ImGui::Text("CG %.3f / %.3f / %.3f m",mass.cg.x,mass.cg.y,mass.cg.z);
      ImGui::Text("Ixx/Iyy/Izz %.0f / %.0f / %.0f kg m2 | Ixz %.0f",mass.inertia.x,mass.inertia.y,mass.inertia.z,mass.ixz);
      ImGui::Text("Air %.2f K %.0f Pa %.4f kg/m3 | q %.0f Pa",air.temp,air.pressure,air.rho,aero.qbar);
      ImGui::Text("Lift %.0f | drag %.0f | weight %.0f N",aero.lift_body.norm(),aero.drag_body.norm(),mass.mass*kG0);
      ImGui::Text("Thrust L/R %.0f / %.0f N | reheat %.2f / %.2f",thrust.each[0],thrust.each[1],s.afterburner[0],s.afterburner[1]);
      ImGui::Text("Fuel flow %.3f / %.3f kg/s | ground effect %.3f",thrust.fuel_flow[0],thrust.fuel_flow[1],aero.ground_effect);
      const auto wind=sim.windAt(s.pos_ned,s.time);
      ImGui::Text("Wind N/E/D %.1f / %.1f / %.1f",wind.x,wind.y,wind.z);
      ImGui::Text("Actual pitch/roll/yaw %.2f / %.2f / %.2f",s.elevator,s.aileron,s.rudder);
      ImGui::Text("Actual flap/spoiler %.2f / %.2f",s.flap,s.spoiler);
      ImGui::Text("Separation L/R %.3f / %.3f | vortex %.3f / %.3f",s.separation[0],s.separation[1],s.vortex_state[0],s.vortex_state[1]);
      ImGui::Text("Nozzle L/R %.2f / %.2f deg | LEVCON %.3f",s.nozzle_angle[0]*kRad2Deg,s.nozzle_angle[1]*kRad2Deg,s.canard);
      if(ImGui::TreeNode("Surface aerodynamics")) {
        for(std::size_t i=0;i<surfaceCount;++i) ImGui::Text("%s F %.0f/%.0f/%.0f N AoA %.1f beta %.1f q %.0f",aero.surfaces[i].name,
          aero.surfaces[i].force_body.x,aero.surfaces[i].force_body.y,aero.surfaces[i].force_body.z,
          aero.local_alpha[i]*kRad2Deg,aero.local_beta[i]*kRad2Deg,aero.local_qbar[i]);
        ImGui::Text("Aero moments %.0f/%.0f/%.0f Nm",aero.moment_body.x,aero.moment_body.y,aero.moment_body.z);
        ImGui::TreePop();
      }
      ImGui::Separator();
      ImGui::Text("Stick pitch %.2f | roll %.2f | rudder %.2f", controls.elevator_stick,
                  controls.aileron_stick, controls.rudder_pedal);
      const double zero = 0, one = 1;
      ImGui::SliderScalar("Throttle", ImGuiDataType_Double, &controls.throttle[0], &zero, &one, "%.2f");
      controls.throttle[1] = controls.throttle[0];
      const double negativeOne = -1;
      ImGui::SliderScalar("Elevator trim", ImGuiDataType_Double, &controls.elevator_trim,
                          &negativeOne, &one, "%.3f");
      ImGui::SliderScalar("Flaps", ImGuiDataType_Double, &controls.flap01, &zero, &one, "%.2f");
      ImGui::SliderScalar("Spoilers", ImGuiDataType_Double, &controls.spoiler01, &zero, &one, "%.2f");
      ImGui::Text("Gear %.0f | brakes %.1f", controls.gear01, controls.brake01);
      if (hasManeuverMode(sim.config().control_law))
        ImGui::Checkbox("Maneuver mode [M]", &controls.maneuver_mode);
      ImGui::Text("Gamepad: %s", gamepad ? "connected" : "none");
      ImGui::Separator();
      ImGui::Text("Camera N/E/D: %.1f  %.1f  %.1f", camera.eye.x, camera.eye.y, camera.eye.z);
      ImGui::TextWrapped(
          "Tab cycles camera (free/chase/close/orbit/cockpit). Free camera: WASD, R/F "
          "up/down, Shift fast, hold right mouse to look. Orbit: right mouse to orbit, "
          "wheel to zoom. Home frames the aircraft.");
      ImGui::TextWrapped(
          "Aircraft: W/S pitch; A/D bank; Q/E rudder; Shift/Ctrl throttle; G gear; F flaps; H airbrake; B "
          "brakes. Space or left mouse fires. F11 fullscreen, F1 hides the developer "
          "windows, Esc quits.");
    }
    ImGui::End();
  }

  // ---- Simulation tooling ----
  if (settings.showDevOverlay && !ui.multiplayer) {
    ImGui::SetNextWindowPos({14, 588}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Simulation", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::Checkbox("Pause simulation", &ui.paused);
      ImGui::SameLine();
      ImGui::Checkbox("Parking brake", &ui.parkingBrake);
      if (ImGui::Button("Reset on runway")) ui.resetParked = true;
      ImGui::SameLine();
      if (ImGui::Button("Reset airborne")) ui.resetAirborne = true;
      const auto min=ImGui::GetItemRectMin(), max=ImGui::GetItemRectMax();
      ui.resetAirborneX=(min.x+max.x)*.5f;
      ui.resetAirborneY=(min.y+max.y)*.5f;
      if (ui.botsAvailable && ImGui::Button("Fight bots [F5]")) ui.toggleDogfight = true;
      ImGui::Separator();
      if (ImGui::Button("Frame aircraft (Home)")) ui.frameAircraft = true;
    }
    ImGui::End();
  }

  // ---- Graphics settings ----
  if (!settings.showDevOverlay) return;
  ImGui::SetNextWindowPos({398, 14}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({360, 640}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Graphics", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
    // Any individual change below turns the preset into "Custom".
    const GraphicsSettings before = settings;
    int preset = static_cast<int>(settings.preset);
    if (ImGui::Combo("Quality preset", &preset, "Low\0Medium\0High\0Ultra\0Custom\0"))
      settings.applyPreset(static_cast<GraphicsPreset>(preset));
    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("VSync", &settings.vsync);
      static const int kSamples[] = {1, 2, 4, 8};
      int sampleIndex = settings.msaaSamples >= 8 ? 3 : settings.msaaSamples >= 4 ? 2 : settings.msaaSamples >= 2 ? 1 : 0;
      if (ImGui::Combo("Multisampling", &sampleIndex, "Off\0" "2x\0" "4x\0" "8x\0"))
        settings.msaaSamples = kSamples[sampleIndex];
      ImGui::Checkbox("Edge filter (FXAA)", &settings.fxaa);
      ImGui::Checkbox("Anisotropic filtering", &settings.anisotropic);
      ImGui::SliderInt("Texture resolution (next launch)",&settings.textureMaxSize,512,4096);
      ImGui::SliderFloat("Model LOD bias",&settings.lodBias,-2.f,2.f,"%.1f");
      ImGui::SliderFloat("Draw distance (km)", &settings.renderDistance, 40000.0f, 250000.0f, "%.0f m");
      ImGui::SliderFloat("Cockpit vertical FOV (deg)",&settings.cockpitFov,40.f,100.f,"%.0f");
      if (ImGui::Checkbox("Fullscreen", &settings.fullscreen)) ui.toggleFullscreen = true;
    }
    if (ImGui::CollapsingHeader("Time and exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::SliderFloat("Sun elevation (deg)", &settings.sky.sunElevationDeg, -8.0f, 89.0f, "%.1f");
      ImGui::SliderFloat("Sun azimuth (deg)", &settings.sky.sunAzimuthDeg, 0.0f, 360.0f, "%.1f");
      ImGui::Checkbox("Automatic exposure", &settings.sky.autoExposure);
      ImGui::SliderFloat("Exposure compensation (EV)", &settings.sky.exposureCompensation, -3.0f, 3.0f, "%+.1f");
      ImGui::Text("Sun %.0f klx | sky %.0f klx | exposure %.2f", stats.sunIlluminanceLux / 1000.f,
                  stats.skyIlluminanceLux / 1000.f, stats.exposure);
      ImGui::Checkbox("Glare",&settings.bloom);
      ImGui::SliderFloat("Glare amount",&settings.bloomStrength,0.f,.15f,"%.3f");
    }
    if (ImGui::CollapsingHeader("Air and weather", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::SliderFloat("Visibility (km)", &settings.weather.visibilityKm, 3.0f, 250.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Ground fog", &settings.weather.fogDensity, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Fog depth (m)", &settings.weather.fogHeight, 30.0f, 1200.0f, "%.0f");
      ImGui::SliderFloat("Rain", &settings.weather.precipitation, 0.0f, 1.0f, "%.2f");
    }
    if (ImGui::CollapsingHeader("Clouds", ImGuiTreeNodeFlags_DefaultOpen)) {
      int cloud = static_cast<int>(settings.clouds);
      if (ImGui::Combo("Cloud quality", &cloud, "Off\0Low\0Medium\0High\0"))
        settings.clouds = static_cast<CloudQuality>(cloud);
      ImGui::SliderFloat("Cumulus coverage", &settings.cloudCoverage, 0.f, 1.f, "%.2f");
      ImGui::SliderFloat("Cloud base (m)", &settings.cloudBase, 300.f, 6000.f, "%.0f");
      ImGui::SliderFloat("Cloud depth (m)", &settings.cloudThickness, 300.f, 4000.f, "%.0f");
      ImGui::SliderFloat("Cirrus coverage", &settings.cirrusCoverage, 0.f, 1.f, "%.2f");
      ImGui::Checkbox("Cloud shadows", &settings.cloudShadows);
      ImGui::Text("March target %u x %u", stats.cloudWidth, stats.cloudHeight);
    }
    if (ImGui::CollapsingHeader("Terrain and scenery", ImGuiTreeNodeFlags_DefaultOpen)) {
      int terrain = static_cast<int>(settings.terrain);
      if (ImGui::Combo("Terrain detail (next launch)", &terrain, "Low\0Medium\0High\0"))
        settings.terrain = static_cast<TerrainQuality>(terrain);
      ImGui::Checkbox("Lakes", &settings.water);
      ImGui::Checkbox("Relief shadows", &settings.terrainShadows);
      ImGui::Checkbox("Trees", &settings.vegetation);
      ImGui::SliderFloat("Tree distance (m)", &settings.sceneryDistance, 1000.f, 15000.f, "%.0f");
      ImGui::SliderInt("Tree density (per km2)", &settings.treeDensity, 50, 1500);
      ImGui::Text("Trees drawn %u in %u chunks | %d lakes", stats.treesDrawn, stats.treeChunks, stats.lakes);
    }
    if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
      int shadow = static_cast<int>(settings.shadows);
      if (ImGui::Combo("Quality", &shadow, "Off\0Low (2 x 1024)\0Medium (3 x 1536)\0High (3 x 2048)\0"))
        settings.shadows = static_cast<ShadowQuality>(shadow);
      ImGui::Text("Active: %u cascades of %u", stats.shadowCascades, stats.shadowMapSize);
      ImGui::SliderFloat("Distance (m)", &settings.shadowDistance, 300.0f, 4000.0f, "%.0f");
      ImGui::SliderFloat("Strength", &settings.shadowStrength, 0.0f, 1.0f, "%.2f");
    }
    if (ImGui::CollapsingHeader("Effects and overlays")) {
      int effects = static_cast<int>(settings.effects);
      if (ImGui::Combo("Effect quality", &effects, "Off\0Low\0Medium\0High\0"))
        settings.effects = static_cast<EffectsQuality>(effects);
      ImGui::Checkbox("Contrails", &settings.contrails);
      ImGui::Checkbox("Wing condensation", &settings.wingVapor);
      ImGui::SliderFloat("Relative humidity", &settings.relativeHumidity, 0.f, 1.f, "%.2f");
      ImGui::Checkbox("Engine heat", &settings.engineHeat);
      ImGui::Checkbox("Heat refraction", &settings.heatDistortion);
      ImGui::Separator();
      ImGui::Checkbox("HUD", &ui.hud.show);
      ImGui::SameLine();
      ImGui::Checkbox("Pitch ladder", &ui.hud.showPitchLadder);
      ImGui::Checkbox("Heading tape", &ui.hud.showHeading);
      ImGui::Checkbox("Flight-path marker", &ui.hud.showFlightPathMarker);
      ImGui::Checkbox("Gunsight", &ui.hud.showGunsight);
      ImGui::Checkbox("Player labels", &ui.hud.showLabels);
      ImGui::SliderFloat("Label range (m)", &ui.hud.labelMaxDistance, 500.0f, 20000.0f, "%.0f");
      ImGui::Separator();
      ImGui::Checkbox("Developer windows", &settings.showDevOverlay);
      ImGui::Checkbox("Debug grid", &settings.showDebugGrid);
      ImGui::Checkbox("Physics geometry", &settings.showPhysicsGeometry);
      if(settings.showPhysicsGeometry)ImGui::TextWrapped("Magenta: CG/hinges; cyan: aero reference; green: force sites; orange: thrust; white: wheel contacts; RGB: principal inertia axes.");
    }
    // Cost-related options no longer match the preset once one is edited.
    if (settings.preset == before.preset &&
        (settings.msaaSamples != before.msaaSamples || settings.fxaa != before.fxaa || settings.lodBias != before.lodBias ||
         settings.renderDistance != before.renderDistance || settings.bloom != before.bloom ||
         settings.clouds != before.clouds || settings.cloudShadows != before.cloudShadows ||
         settings.terrain != before.terrain || settings.terrainShadows != before.terrainShadows ||
         settings.water != before.water || settings.vegetation != before.vegetation ||
         settings.sceneryDistance != before.sceneryDistance || settings.treeDensity != before.treeDensity ||
         settings.shadows != before.shadows || settings.shadowDistance != before.shadowDistance ||
         settings.effects != before.effects || settings.heatDistortion != before.heatDistortion ||
         settings.textureMaxSize != before.textureMaxSize))
      settings.preset = GraphicsPreset::Custom;
    ImGui::Separator();
    if (ImGui::Button("Save settings")) ui.saveSettings = true;
    ImGui::SameLine();
    if (ImGui::Button("Reload defaults")) ui.resetSettings = true;
    ImGui::Text("Config: %s", settings.configPath.c_str());
  }
  ImGui::End();
}

}  // namespace ofs::client
