#include "ofs/geometry_debug.hpp"
#include "platform.hpp"
#include "renderer.hpp"
#include "input.hpp"
#include "mouse_aim.hpp"
#include "debug_ui.hpp"
#include "hud.hpp"
#include "sound.hpp"
#include "ofs/pilot.hpp"
#include "audio_device.hpp"
#include "weapon_visuals.hpp"
#include "log.hpp"
#include "camera.hpp"
#include "chat.hpp"
#include "settings.hpp"
#include "local_gun.hpp"
#include "ofs/fixed_step.hpp"
#include "ofs/ground_service.hpp"
#include "ofs/trim.hpp"
#include "ofs_version.hpp"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>
#include <filesystem>
#ifdef OFS_NETWORK_ENABLED
#include "ofs/net/client.hpp"
#include "ofs/net/server.hpp"
#include "ofs/net/cli.hpp"
#endif

namespace {
using ofs::client::CameraMode;
struct Options {
  bool smoke{}, gunSmoke{}, networkSmoke{}, combatSmoke{}, missileSmoke{}, dogfightSmoke{},
      airborne{}, afterburnerBench{}, map{}, noSound{};
  unsigned frames{}, seconds{};
  double ejectAt{-1};  // a scripted run fires the seat after this many seconds
  std::string screenshot, server, name{"pilot"}, asset, config{"graphics.cfg"}, soundCapture;
  unsigned port{27020}, bots{};
  int visualBench{};
  ofs::AircraftType aircraft{ofs::AircraftType::A320};
  std::string scenario;
  std::string flightDemo;
  double orbitYaw{}, orbitPitch{.25}, orbitDistance{};
  ofs::client::CameraMode camera{ofs::client::CameraMode::Pursuit};
  int width{0}, height{0};
};

Options parse(int argc, char** argv) {
  Options result;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--airborne") result.airborne = true;
    else if (arg == "--aircraft" && i + 1 < argc) result.aircraft = ofs::aircraftTypeFromName(argv[++i]);
    else if (arg == "--visual-scenario" && i + 1 < argc) result.scenario = argv[++i];
    else if (arg == "--flight-demo" && i + 1 < argc) result.flightDemo = argv[++i];
    else if (arg == "--orbit-yaw" && i + 1 < argc) result.orbitYaw = std::stod(argv[++i]);
    else if (arg == "--orbit-pitch" && i + 1 < argc) result.orbitPitch = std::stod(argv[++i]);
    else if (arg == "--orbit-distance" && i + 1 < argc) result.orbitDistance = std::stod(argv[++i]);
    else if (arg == "--gun-smoke") { result.gunSmoke=true; result.airborne=true; result.frames=120; }
    else if (arg == "--smoke-test") result.smoke = true;
    else if (arg == "--screenshot" && i + 1 < argc) result.screenshot = argv[++i];
    else if (arg == "--asset" && i + 1 < argc) result.asset = argv[++i];
    else if (arg == "--config" && i + 1 < argc) result.config = argv[++i];
    else if (arg == "--no-sound") result.noSound = true;
    else if (arg == "--sound-capture" && i + 1 < argc) result.soundCapture = argv[++i];
    else if (arg == "--width" && i + 1 < argc) result.width = std::atoi(argv[++i]);
    else if (arg == "--height" && i + 1 < argc) result.height = std::atoi(argv[++i]);
    else if (arg == "--camera" && i + 1 < argc) {
      const std::string_view name = argv[++i];
      if (name == "free") result.camera = CameraMode::Free;
      else if (name == "pursuit") result.camera = CameraMode::Pursuit;
      else if (name == "chase") result.camera = CameraMode::Chase;
      else if (name == "close-chase") result.camera = CameraMode::CloseChase;
      else if (name == "orbit") result.camera = CameraMode::Orbit;
      else if (name == "cockpit") result.camera = CameraMode::FirstPerson;
      else throw std::runtime_error("--camera expects free, pursuit, chase, close-chase, orbit or cockpit");
    }
    else if (arg == "--free-camera") result.camera = CameraMode::Free;
    else if (arg == "--pursuit") result.camera = CameraMode::Pursuit;
    else if (arg == "--map") result.map = true;
    else if (arg == "--chase") result.camera = CameraMode::Chase;
    else if (arg == "--close-chase") result.camera = CameraMode::CloseChase;
    else if (arg == "--orbit") result.camera = CameraMode::Orbit;
    else if (arg == "--cockpit") result.camera = CameraMode::FirstPerson;
    else if (arg == "--visual-bench" && i + 1 < argc) result.visualBench = std::atoi(argv[++i]);
    else if (arg == "--afterburner-bench" && i + 1 < argc) {
      result.visualBench=std::atoi(argv[++i]); result.afterburnerBench=true;
      result.aircraft=ofs::AircraftType::Typhoon; result.airborne=true;
    }
#ifdef OFS_NETWORK_ENABLED
    else if (arg == "--server" && i + 1 < argc) result.server = argv[++i];
    else if (arg == "--bots" && i + 1 < argc) result.bots = ofs::net::number(argv[++i], 0, 8);
    else if (arg == "--dogfight-smoke") { result.dogfightSmoke=true; result.seconds=15; }
    else if (arg == "--name" && i + 1 < argc) result.name = argv[++i];
    else if (arg == "--port" && i + 1 < argc) result.port = ofs::net::number(argv[++i], 1, 65535);
    else if (arg == "--seconds" && i + 1 < argc) result.seconds = ofs::net::number(argv[++i], 1, 86400);
    else if (arg == "--eject-at" && i + 1 < argc) result.ejectAt = std::stod(argv[++i]);
    else if (arg == "--missile-smoke") {
      result.missileSmoke = true;
      result.seconds = 20;
    } else if (arg == "--combat-smoke") {
      result.combatSmoke = true;
      result.seconds = 20;
    } else if (arg == "--network-smoke") {
      result.networkSmoke = true;
      result.seconds = 8;
    }
#endif
    else if (arg == "--frames" && i + 1 < argc) {
      const std::string_view number = argv[++i];
      const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), result.frames);
      if (error != std::errc{} || end != number.data() + number.size() || result.frames == 0)
        throw std::runtime_error("--frames requires a positive integer");
    } else {
      throw std::runtime_error(
          "Usage: ofs_client [--smoke-test|--gun-smoke] [--frames N] [--screenshot path.ppm] "
          "[--aircraft a320|su57|typhoon|sr71|jf17] [--asset path.glb] [--config path.cfg] [--airborne] [--width N] [--height N] "
          "[--map] [--free-camera|--pursuit|--chase|--close-chase|--orbit|--cockpit] [--visual-bench N] "
          "[--no-sound] [--sound-capture path.wav] "
          "[--bots 0..8] [--server host --name name]");
    }
  }
  if (result.bots) {
    if (!result.server.empty() || result.smoke || result.gunSmoke || result.dogfightSmoke ||
        !result.scenario.empty() || !result.flightDemo.empty() || result.visualBench)
      throw std::runtime_error("--bots requires ordinary local flight");
    result.aircraft = ofs::dogfightAircraftType(result.aircraft);
    result.airborne = true;
  }
  if (result.dogfightSmoke && (!result.server.empty() || result.smoke || result.gunSmoke ||
      !result.scenario.empty() || !result.flightDemo.empty() || result.visualBench))
    throw std::runtime_error("dogfight smoke requires ordinary local flight");
  if (result.smoke && !result.server.empty())
    throw std::runtime_error("offline smoke and --server cannot be combined");
  if ((result.networkSmoke || result.combatSmoke || result.missileSmoke) &&
      result.server.empty())
    throw std::runtime_error("--network-smoke requires --server");
  if (result.smoke && result.screenshot.empty()) result.screenshot = "smoke.ppm";
  if (result.visualBench < 0 || result.visualBench > 64)
    throw std::runtime_error("--visual-bench must be 0..64");
  if (!std::isfinite(result.orbitYaw) || !std::isfinite(result.orbitPitch) ||
      !std::isfinite(result.orbitDistance) || std::abs(result.orbitPitch)>1.4 ||
      result.orbitDistance<0 || result.orbitDistance>600)
    throw std::runtime_error("invalid orbit camera parameters");
  if (result.gunSmoke && (!result.server.empty() || !result.scenario.empty() || !result.flightDemo.empty()))
    throw std::runtime_error("gun smoke requires ordinary offline flight");
  if (!result.scenario.empty()) {
    if (!result.server.empty()) throw std::runtime_error("visual scenarios are offline fixtures only");
    const std::vector<std::string> names{"parked","surfaces","flaps","gear","flight","high-altitude","high-mach","exhaust","contrail","gun","impact","destruction","mixed","idle","military","afterburner","afterburner-multiple","afterburner-transition","vectoring","high-aoa","condensation","environment","forest","grass","clouds","above-clouds","lake","mountains","valley","ranges","village","farmland","fields","eject","blackout","damage","damage-heavy","breakup","missile","detonation","menu","controls","chat","airfield","apron","shelters","threshold","decoys","warning","service"};
    if (std::find(names.begin(), names.end(), result.scenario) == names.end())
      throw std::runtime_error("Unknown visual scenario");
    if (result.screenshot.empty() || !result.frames) throw std::runtime_error("visual scenarios require --frames and --screenshot");
  }
  if (!result.flightDemo.empty() && result.flightDemo!="taxi" && result.flightDemo!="takeoff" && result.flightDemo!="landing" && result.flightDemo!="stall" && result.flightDemo!="crash")
    throw std::runtime_error("--flight-demo expects taxi, takeoff, landing, stall or crash");
  if (!result.flightDemo.empty() && (!result.server.empty() || !result.scenario.empty()))
    throw std::runtime_error("flight demo requires an offline simulator");
  return result;
}

double validationPitchInput(const ofs::Simulator& sim,double target) {
  using namespace ofs;
  if(sim.state().fcs_enabled && sim.config().control_law==FlightControlLaw::VectorFighter)
    return clamp(2*(target-sim.instruments().pitch_deg*kDeg2Rad)/sim.config().max_pitch_rate-
        sim.controls().elevator_trim+sim.state().trim_reference,-1,1);
  const auto& cfg=sim.config();const auto a=sim.evalAero();const auto t=sim.evalThrust();
  const double cm=a.qbar>10 ? -t.moment_body.y/(a.qbar*cfg.wing_area*cfg.mac) : 0;
  const double de=(cm-cfg.cm0-cfg.cm_alpha*std::sin(a.alpha)+.1*sim.controls().flap01)/cfg.cm_de;
  const double feed=de<0 ? de/cfg.elev_min : -de/cfg.elev_max;
  const double command=feed-sim.controls().elevator_trim+1.8*(target-sim.instruments().pitch_deg*kDeg2Rad)-4*sim.state().omega_body.y;
  if(sim.state().fcs_enabled && cfg.control_law!=FlightControlLaw::Direct && a.vtas>30 && a.qbar>50) {
    const double authority=a.qbar*cfg.wing_area*cfg.mac*std::abs(cfg.cm_de*cfg.elev_min);
    if(cfg.aero_kind==AeroModelKind::AirlinerEngineering) {
      const double desiredRate=(command+sim.controls().elevator_trim-sim.equilibriumElevator())*cfg.response_time*authority/sim.massProperties().inertia.y+sim.state().omega_body.y;
      double bank,pitch,heading;eulerFromQuat(sim.state().att,bank,pitch,heading);
      const double neutral=std::cos(pitch)/std::max(.5,std::cos(bank));
      const double desired=sim.normalLoad()+desiredRate/.18;
      const double upper=sim.state().flap>.2?2:cfg.g_positive,lower=sim.state().flap>.2?0:cfg.g_negative;
      return clamp((desired-neutral)/(desired>=neutral?upper-neutral:neutral-lower),-1,1);
    }
    return clamp(((command+sim.controls().elevator_trim-sim.equilibriumElevator())*cfg.response_time*authority/sim.massProperties().inertia.y+sim.state().omega_body.y)/cfg.max_pitch_rate-sim.controls().elevator_trim+sim.state().trim_reference-(cfg.control_law==FlightControlLaw::Transport?.12:.08)*(sim.state().att.inverseRotate({0,0,1}).z-sim.normalLoad())/cfg.max_pitch_rate,-1,1);
  }
  return clamp(command,-1,1);
}

void reset(ofs::Simulator& sim, ofs::Controls& controls, ofs::State& previous,
           ofs::client::Camera& camera, ofs::FixedStepClock& clock, bool airborne) {
  ofs::State state;
  controls = {};
  if (airborne) {
    const auto trim = ofs::solveTrim(sim.config());
    if (!trim.converged) throw std::runtime_error("Airborne reset trim failed");
    state = trim.state;
    controls = trim.controls;
  } else {
    state.pos_ned.z = -(sim.config().gear_nose.z - .15);
  }
  sim.setState(state);
  sim.setControls(controls);
  previous = state;
  camera.frameAircraft(state.pos_ned);
  clock.reset();
  ofs::client::log("SIM", airborne ? "Reset airborne (steady trim)" : "Reset on runway");
}

// What a scripted run would have played, as a 16-bit stereo WAV.
bool writeWave(const std::string& path, const std::vector<float>& stereo) {
  std::ofstream file(path, std::ios::binary);
  const auto number = [&](std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) file.put(char(value >> (8 * i)));
  };
  const std::uint32_t bytes = std::uint32_t(stereo.size() * 2);
  file.write("RIFF", 4); number(36 + bytes, 4); file.write("WAVEfmt ", 8); number(16, 4); number(1, 2); number(2, 2);
  number(ofs::client::kSoundRate, 4); number(ofs::client::kSoundRate * 4, 4); number(4, 2); number(16, 2);
  file.write("data", 4); number(bytes, 4);
  for (const float sample : stereo) number(std::uint16_t(std::int16_t(std::lround(std::clamp(sample, -1.f, 1.f) * 32767))), 2);
  return bool(file);
}

void pushKey(SDL_Window* window, SDL_Scancode scancode, bool down) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.windowID = SDL_GetWindowID(window);
  e.key.which = 0x4f4653; // Tag scripted keys so desktop input cannot alter smoke.
  e.key.scancode = scancode;
  e.key.down = down;
  e.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
  SDL_PushEvent(&e);
}

// Scripted event sequence for the graphical smoke test. Updated flight keys
// so the existing regression still covers free camera, relative mouse look,
// focus release, resize, fullscreen, minimize/restore and an ImGui click.
void smokeEvents(unsigned frame, SDL_Window* window, const ofs::client::UiSettings& ui) {
  if (frame == 1) {
    if (!SDL_RaiseWindow(window)) throw std::runtime_error(SDL_GetError());
    SDL_WarpMouseInWindow(window, 950, 500);
  }
  if (frame == 5) pushKey(window, SDL_SCANCODE_W, true);
  if (frame == 6) {
    SDL_WarpMouseInWindow(window, 950, 500);
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(window);
    e.motion.x = 950;
    e.motion.y = 500;
    SDL_PushEvent(&e);
  }
  if (frame == 8 || frame == 20) {
    SDL_Event e{};
    e.type = frame == 8 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = SDL_GetWindowID(window);
    e.button.button = SDL_BUTTON_RIGHT;
    e.button.down = frame == 8;
    e.button.x = 950;
    e.button.y = 500;
    SDL_PushEvent(&e);
  }
  if (frame == 12) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(window);
    e.motion.x = 950;
    e.motion.y = 500;
    e.motion.xrel = 20;
    e.motion.yrel = -10;
    SDL_PushEvent(&e);
  }
  if (frame == 20) pushKey(window, SDL_SCANCODE_W, false);
  if (frame == 22) { pushKey(window, SDL_SCANCODE_TAB, true); pushKey(window, SDL_SCANCODE_TAB, false); }
  if (frame == 25) pushKey(window, SDL_SCANCODE_S, true);
  if (frame == 30) pushKey(window, SDL_SCANCODE_S, false);
  if (frame == 35 && !SDL_SetWindowSize(window, 1100, 760))
    throw std::runtime_error(SDL_GetError());
  if (frame == 40 || frame == 42) {
    // Exercise the camera-mode cycle, new in M3.5.
    pushKey(window, SDL_SCANCODE_TAB, true);
    pushKey(window, SDL_SCANCODE_TAB, false);
  }
  if (frame == 45 || frame == 60) {
    pushKey(window, SDL_SCANCODE_F11, true);
    pushKey(window, SDL_SCANCODE_F11, false);
  }
  if (frame == 70) {
    pushKey(window, SDL_SCANCODE_D, true);
    SDL_Event e{};
    e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    e.window.windowID = SDL_GetWindowID(window);
    SDL_PushEvent(&e);
  }
  if (frame == 95) {
    if (!SDL_RaiseWindow(window)) throw std::runtime_error(SDL_GetError());
    SDL_WarpMouseInWindow(window, ui.resetAirborneX, ui.resetAirborneY);
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(window);
    e.motion.x = ui.resetAirborneX;
    e.motion.y = ui.resetAirborneY;
    SDL_PushEvent(&e);
  }
  if (frame == 96 || frame == 97) {
    SDL_Event e{};
    e.type = frame == 96 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = SDL_GetWindowID(window);
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = frame == 96;
    e.button.x = ui.resetAirborneX;
    e.button.y = ui.resetAirborneY;
    SDL_PushEvent(&e);
  }
  // Window-manager transitions are asynchronous; finish each before advancing
  // the fixed-frame smoke sequence or its screenshot can fall in a minimized frame.
  if (frame == 120 && (!SDL_MinimizeWindow(window) || !SDL_SyncWindow(window)))
    throw std::runtime_error("Smoke minimize failed: " + std::string(SDL_GetError()));
  if (frame == 130) {
    if (!SDL_RestoreWindow(window) || !SDL_SyncWindow(window) || !SDL_RaiseWindow(window) ||
        (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED))
      throw std::runtime_error("Smoke restore failed: " + std::string(SDL_GetError()));
  }
  if (frame == 145) {
    pushKey(window, SDL_SCANCODE_HOME, true);
    pushKey(window, SDL_SCANCODE_HOME, false);
  }
  if (frame == 180) {
    SDL_Event e{};
    e.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&e);
  }
}
}  // namespace

int main(int argc, char** argv) {
  using namespace ofs;
  using namespace ofs::client;
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
      std::printf("OpenFlightSim %s (%s)\n", OFS_VERSION, OFS_COMMIT);
      return 0;
    }
    Options options = parse(argc, argv);
    GraphicsSettings graphics;
    graphics.load(options.config);

    // The window size has to be known before the window exists, so the
    // request travels through PlatformOptions. The saved graphics settings
    // provide the default when the command line does not.
    PlatformOptions platformOptions;
    if (options.width > 0) platformOptions.width = options.width;
    if (options.height > 0) platformOptions.height = options.height;
    else if (options.width > 0) platformOptions.height = options.width * 9 / 16;
    else {
      platformOptions.width = graphics.windowWidth;
      platformOptions.height = graphics.windowHeight;
    }
    Platform platform(platformOptions);
    if (graphics.fullscreen) platform.toggleFullscreen();
    if (options.smoke) graphics.showDevOverlay = true;
    if (!options.scenario.empty() || !options.flightDemo.empty() || options.visualBench > 0) graphics.showDevOverlay = false;
    UiContext uiContext(platform.window(), options.smoke);
    if (options.smoke) ImGui::GetStyle().FramePadding = {4,3};

    Renderer renderer(platform, graphics);
    Input input;
    auto definition = aircraftDefinition(options.aircraft);
    Simulator offlineSim(definition.flight);
    LocalGun localGun(options.aircraft);
    renderer.setAircraftType(options.aircraft);
#ifdef OFS_NETWORK_ENABLED
    std::unique_ptr<ofs::net::Client> network;
    std::unique_ptr<ofs::net::Server> dogfight;
    FixedStepClock serverClock;
    if (options.bots) {
      ofs::net::ServerConfig config;
      config.bind = "127.0.0.1"; config.port = 0; config.bots = options.bots;
      dogfight = std::make_unique<ofs::net::Server>(config);
    }
    if (!options.server.empty() || dogfight) {
      network = std::make_unique<ofs::net::Client>(options.name, options.aircraft);
      network->prediction().simulator() = Simulator(definition.flight);
    }
    const auto simulation = [&]() -> Simulator& {
      return network ? network->prediction().simulator() : offlineSim;
    };
#else
    const auto simulation = [&]() -> Simulator& { return offlineSim; };
#endif
    Controls controls;
    State previous;
    Camera camera;
    camera.orbitDistance = definition.visual.radius * 2.3;
    camera.orbitYaw = options.orbitYaw; camera.orbitPitch = options.orbitPitch;
    if (options.orbitDistance>0) camera.orbitDistance = options.orbitDistance;
    FixedStepClock clock;
    UiSettings ui;
#ifdef OFS_NETWORK_ENABLED
    ui.multiplayer = bool(network);
    ui.botsAvailable = true;
    ui.dogfight = bool(dogfight);
#endif
    ui.hud.show = graphics.showHud;
    ui.hud.showLabels = graphics.showPlayerLabels;
    ui.hud.labelMaxDistance = graphics.playerLabelMaxDistance;
    ui.hud.showMinimap = graphics.showMinimap;

    // Resolve the canonical per-type paths beside the executable, in the
    // checkout or at the configured source root. Required asset failures are
    // reported before entering the loop; --asset overrides only the local type.
    auto assetPathFor = [&](const AircraftDefinition& d) {
      const std::filesystem::path relative(d.modelAsset);
      const char* base = SDL_GetBasePath();
      const std::filesystem::path roots[]{std::filesystem::path(base ? base : ""),
        std::filesystem::path(base ? base : "").parent_path().parent_path(),
        std::filesystem::current_path(), std::filesystem::path(OFS_SOURCE_ROOT)};
      for (const auto& root : roots) if (std::filesystem::is_regular_file(root / relative) ||
          std::filesystem::is_regular_file((root / relative).string() + ".ofspack"))
        return (root / relative).string();
      return relative.string();
    };
    // Decoding runs on worker threads while this thread keeps answering the
    // window system; a window silent for a few seconds is reported as hung.
    {
      std::vector<std::future<Renderer::AircraftSource>> pending;
      for (const auto& d : aircraftDefinitions())
        pending.push_back(renderer.prepareAircraft(
            d.type == options.aircraft && !options.asset.empty() ? options.asset : assetPathFor(d), d.type));
      for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto status = "Loading aircraft " + std::to_string(i + 1) + " of " + std::to_string(pending.size());
        do {
          SDL_PumpEvents();
          if (SDL_HasEvent(SDL_EVENT_QUIT)) {
            log("CORE", "Closed while loading");
            return 0;
          }
          renderer.loadingFrame(status);
        } while (pending[i].wait_for(std::chrono::milliseconds(15)) != std::future_status::ready);
        auto source = pending[i].get();
        const auto path = source.path;
        if (!renderer.finishAircraft(std::move(source))) throw std::runtime_error("Required aircraft asset failed: " + path);
      }
    }
    std::string assetName = "(no model)";
    assetName = renderer.aircraftName();

    graphics.windowWidth = platformOptions.width;
    graphics.windowHeight = platformOptions.height;

    reset(simulation(), controls, previous, camera, clock, options.airborne);
    ui.parkingBrake = !options.airborne;
    if (options.flightDemo=="landing" || options.flightDemo=="stall") {
      const bool landing=options.flightDemo=="landing";
      TrimRequest request;request.tas=landing?85:110;request.altitude=landing?100:2500;
      request.flap01=request.gear01=landing?1:0;request.gamma=landing?-2.5*kDeg2Rad:0;
      const auto trim=solveTrim(simulation().config(),request);
      if(!trim.converged) throw std::runtime_error("flight demo initial trim failed");
      auto state=trim.state;controls=trim.controls;
      if(landing) state.pos_ned.x=-2300; // approach the finite runway, rather than landing beyond its end
      if(!landing){state.att=quatFromEuler(0,32*kDeg2Rad,0);state.vel_ned={90,0,0};}
      simulation().setState(state);simulation().setControls(controls);previous=state;ui.parkingBrake=false;
    }
    if(options.flightDemo=="crash") {
      State entry;entry.pos_ned={-200,0,-40};entry.vel_ned={55,0,28};
      entry.att=quatFromEuler(12*kDeg2Rad,-10*kDeg2Rad,0);entry.fcs_enabled=false;
      simulation().setState(entry);controls={};controls.gear01=0;simulation().setControls(controls);
      previous=simulation().state();ui.parkingBrake=false;
    }
    if (!options.scenario.empty()) {
      ui.paused = true;
      State fixture;
      fixture.pos_ned.z = -(simulation().config().gear_nose.z - .15);
      controls = {};
      controls.gear01 = 1;
      const bool onGround = options.scenario == "parked" || options.scenario == "surfaces" || options.scenario == "flaps" ||
          options.scenario == "mixed" || options.scenario == "apron" || options.scenario == "shelters" ||
          options.scenario == "threshold" || options.scenario == "service";
      if (!onGround) {
        fixture.pos_ned.z = options.scenario == "contrail" ? -8500 : -1200;
        fixture.vel_ned = {options.aircraft == AircraftType::Typhoon ? 200.0 : 140.0,0,0};
        controls.gear01 = 0;
      }
      if(options.scenario=="environment") {fixture.pos_ned={6000,1800,-1100};fixture.vel_ned={};}
      // The airfield from above, and from three places on it: a stand in front
      // of the terminal, the mouth of a shelter and the southern threshold.
      if(options.scenario=="airfield") {fixture.pos_ned={150,-260,-420};fixture.vel_ned={};}
      if(options.scenario=="apron" || options.scenario=="service") {fixture.pos_ned.x=330;fixture.pos_ned.y=-318;}
      if(options.scenario=="shelters") {fixture.pos_ned.x=-700;fixture.pos_ned.y=-436;fixture.att=quatFromEuler(0,0,kPi*.5);}
      if(options.scenario=="threshold") fixture.pos_ned.x=-1270;
      if(options.scenario=="forest") {fixture.pos_ned={-625,580,-26};fixture.vel_ned={};}
      if(options.scenario=="grass") {fixture.pos_ned={-350,165,-6};fixture.vel_ned={};}
      if(options.scenario=="clouds") {fixture.pos_ned={-2000,3200,-double(graphics.cloudBase+graphics.cloudThickness*.4f)};fixture.vel_ned={};}
      if(options.scenario=="above-clouds") {fixture.pos_ned={-2000,3200,-double(graphics.cloudBase+graphics.cloudThickness+1500)};fixture.vel_ned={};}
      // Low over the largest lake near the field, and level with the peaks of the northern range.
      if(options.scenario=="lake") {fixture.pos_ned={-9400,2400,-250};fixture.vel_ned={};}
      if(options.scenario=="mountains") {fixture.pos_ned={3500,-15000,-1900};fixture.vel_ned={};}
      // Down in the valley south of the field, and among the ranges to the west.
      if(options.scenario=="valley") {fixture.pos_ned={-16000,3500,-420};fixture.vel_ned={};}
      // Over the farmland north-west of the field, from height and from low down.
      if(options.scenario=="farmland") {fixture.pos_ned={3800,-2500,-650};fixture.vel_ned={};}
      if(options.scenario=="fields") {fixture.pos_ned={5200,-2200,-170};fixture.vel_ned={};}
      if(options.scenario=="village") {fixture.pos_ned={1990,3850,-70};fixture.vel_ned={};}
      if(options.scenario=="ranges") {fixture.pos_ned={-4000,-11000,-1500};fixture.vel_ned={};fixture.att=quatFromEuler(0,0,-kPi*.5);}
      if (options.scenario == "gear") controls.gear01 = 1;
      if (options.scenario == "surfaces") {
        controls.elevator_stick = .8; controls.aileron_stick = .9; controls.rudder_pedal = .8;
        controls.steering = .7; controls.spoiler01 = .85;
      }
      if (options.scenario == "flaps") controls.flap01 = 1;
      fixture.n1[0] = fixture.n1[1] = .95;
      controls.throttle[0] = controls.throttle[1] = .95;
      if (options.scenario=="high-altitude" || options.scenario=="high-mach" ||
          (options.aircraft==AircraftType::SR71 && options.scenario=="contrail")) {
        const bool blackbird=options.aircraft==AircraftType::SR71;
        const double height=blackbird?25000:11000;
        const double mach=blackbird?950/isaAtAltitude(height).sound:options.aircraft==AircraftType::A320?.80:1.5;
        fixture.pos_ned.z=-height;fixture.vel_ned={mach*isaAtAltitude(height).sound,0,0};
        fixture.inlet_spike[0]=fixture.inlet_spike[1]=inletSpikeTarget(mach);
        fixture.afterburner[0]=fixture.afterburner[1]=.65;
      }
      if (options.scenario=="idle") fixture.n1[0]=fixture.n1[1]=controls.throttle[0]=controls.throttle[1]=0;
      if (options.scenario=="military") fixture.n1[0]=fixture.n1[1]=controls.throttle[0]=controls.throttle[1]=.85;
      if (options.scenario.starts_with("afterburner")) {
        fixture.n1[0]=fixture.n1[1]=controls.throttle[0]=controls.throttle[1]=1;
        fixture.afterburner[0]=fixture.afterburner[1]=1;
      }
      if(options.scenario=="vectoring" && options.aircraft==AircraftType::Su57){fixture.nozzle_angle[0]=-10*kDeg2Rad;fixture.nozzle_angle[1]=8*kDeg2Rad;fixture.afterburner[0]=fixture.afterburner[1]=1;}
      if(options.scenario=="high-aoa"){fixture.att=quatFromEuler(15*kDeg2Rad,35*kDeg2Rad,0);fixture.vel_ned={120,0,0};fixture.actuators_initialized=true;fixture.canard=-.4;fixture.elevator=-.15;fixture.afterburner[0]=fixture.afterburner[1]=1;}
      if(options.scenario=="condensation") {
        fixture.pos_ned.z=-1500;fixture.vel_ned={180,0,0};
        fixture.att=quatFromEuler(25*kDeg2Rad,25*kDeg2Rad,0);
        controls.maneuver_mode=hasManeuverMode(simulation().config().control_law);
      }
      if(options.scenario.starts_with("damage")) {
        // One of everything: a holed wing, a torn fin and a shot-out engine,
        // or the same aircraft a moment before it comes apart.
        const bool heavy=options.scenario=="damage-heavy";
        applyPartDamage(simulation().config(),fixture,DamagePart::LeftWing,heavy?100:60);
        applyPartDamage(simulation().config(),fixture,DamagePart::RightWing,heavy?75:20);
        applyPartDamage(simulation().config(),fixture,DamagePart::Tail,heavy?80:45);
        applyPartDamage(simulation().config(),fixture,DamagePart::RightEngine,100);
        if(heavy) applyPartDamage(simulation().config(),fixture,DamagePart::LeftEngine,30);
        fixture.afterburner[0]=heavy?0:.8;fixture.afterburner[1]=0;fixture.n1[1]=0;
      }
      simulation().setState(fixture); simulation().setControls(controls); previous = fixture;
      ui.parkingBrake = fixture.pos_ned.z > -10;
    }
#ifdef OFS_NETWORK_ENABLED
    // Asset uploads may exceed the server's five-second Hello deadline. Start
    // transport after initialization so a loaded client can poll immediately.
    if (network)
      network->connect(dogfight ? "127.0.0.1" : options.server,
                       dogfight ? dogfight->port() : static_cast<std::uint16_t>(options.port));
#endif
    const Vec3 initialCamera = camera.position;
    const double initialYaw = camera.yaw;
    MouseAim mouseAim;
    // Scripted runs drive the controls themselves and must not depend on a
    // saved control preference.
    const bool automated = options.smoke || options.gunSmoke || options.networkSmoke || options.combatSmoke ||
        options.missileSmoke || options.dogfightSmoke || options.afterburnerBench || options.visualBench > 0 ||
        options.frames > 0 || options.seconds > 0 || !options.screenshot.empty() || !options.scenario.empty() ||
        !options.flightDemo.empty();
    // Sound. A scripted run opens no sound card; it can record what it would
    // have played instead.
    SoundMixer mixer;
    SoundDirector sound(mixer);
    std::unique_ptr<AudioDevice> speakers;
    if (!automated && !options.noSound) speakers = std::make_unique<AudioDevice>(mixer);
    ui.soundStatus = speakers ? speakers->description() : "off for this run";
    std::vector<float> capturedSound;
    std::vector<SoundFrame::Aircraft> soundAircraft;
    std::vector<SoundFrame::Missile> soundMissiles;
    std::vector<SoundFrame::Threat> soundThreats;
    // Since the last tick for own rounds striking, and the last crunch of a
    // hard landing, so that neither becomes a buzz.
    double hitCueClock = 1, crunchClock = 1;
    bool menuWasOpen = false, mapWasOpen = options.map, triggerWasHeld = false, wasCrashed = false;
    unsigned frame = 0, fullscreenSwitches = 0;
    bool sawFlightInput = false, sawMouseLook = false, sawFocusRelease = false;
    bool sawCameraMove = false, sawCameraTurn = false, sawAirborneReset = false;
    bool sawCameraCycle = false;
    bool fullMap = options.map;  // N toggles; --map starts with it open
    std::uint64_t totalTicks = 0, rateTicks = 0;
    double rateElapsed = 0, measuredTicks = 0;
    auto last = std::chrono::steady_clock::now();
    const auto started = last;
#ifdef OFS_NETWORK_ENABLED
    [[maybe_unused]] bool networkReady = false;
    [[maybe_unused]] unsigned remoteFrames = 0, combatFrames = 0;
    unsigned missileFrames = 0, radarFrames = 0, missileDetonations = 0;
    ofs::net::Tick missileSmokeAction{};
    [[maybe_unused]] std::size_t maximumRemotes=0;
    [[maybe_unused]] bool observedRemoteAfterburner=false;
    struct PendingMissileMiss { std::uint64_t id{}; double remaining{}; };
    std::vector<std::uint64_t> missileHits;
    std::vector<PendingMissileMiss> pendingMissileMisses;
    std::string missileOutcome;
    double missileOutcomeSeconds{};
    std::uint64_t missileOutcomeId{};
#endif
    CameraMode cameraMode = options.smoke ? CameraMode::Free : options.camera;
    bool running = true;
    std::vector<RemoteAircraft> remotes;
    CombatVisuals combat;
#ifdef OFS_NETWORK_ENABLED
    bool missileSelected = false, missileFireHeld = false;
    // Brief HUD cues: own rounds striking, a kill, and being hit.
    double hitMarkerSeconds = 0, killMarkerSeconds = 0, damageFlashSeconds = 0;
    DamagePart lastDamagedPart = DamagePart::Fuselage;
    StoreDisplay storeDisplay;
    std::vector<HudFrame::Station> hudStations;
    std::vector<Vec3> hudMissiles;
    std::set<std::uint64_t> presentedMissiles;
    unsigned dogfightSmokeStage=0, dogfightStarts=0;
    bool dogfightReturnedSolo=false;
    std::uint64_t dogfightSmokeShots=0;
#endif
    // Countermeasures. Each key releases one decoy and repeats while held; a
    // solo flight carries its own stock, which only makes the light show.
    double flareClock = 0, chaffClock = 0;
    unsigned decoysReleased = 0;
    int soloFlares = int(weapons::decoyCapacity(options.aircraft)), soloChaff = soloFlares;
    std::vector<CombatVisuals::Decoy> releasedDecoys;
    std::vector<HudFrame::Threat> threats;
    // The pilot's own most recently launched missile still in flight: position,
    // velocity and age, for the missile view.
    struct OwnMissile { std::uint64_t id{}; Vec3 position, velocity; double age{}; };
    std::optional<OwnMissile> ownMissile;
    // The missile the view last rode, and how long the view still holds where
    // it ended, to watch what it did.
    OwnMissile riddenMissile;
    double missileViewLinger = 0;
    // Ctrl at idle brakes; the airbrake goes back to where the pilot had it.
    bool brakeHeld = false;
    double airbrakeBefore = 0;
    // Seconds stood on the ground toward a turn-round, and left to say it is done.
    double standing = 0, servicedSeconds = 0;
    // The pilot: how much load they have taken, and whether they are still
    // aboard. The eject key has to be held; a solo pilot who has gone is given
    // a new aircraft after a few seconds, a shared game does that itself.
    PilotStrain pilot;
    Ejections ejections;
    constexpr double kEjectHoldSeconds = 1, kSoloRespawnSeconds = 6;
    double ejectHold = 0, ejectAsked = 0, soloEjected = -1;
    bool ownEjected = false, respawnAirborne = options.airborne;
    ChatLog chatLog;
    std::vector<HudFrame::Score> scores;
    double uiClock = 0;  // seconds of wall time, for chat ages
    int benchCount = 1;
    double benchTimer = 0;
    double benchSeconds=0, benchCpu=0, benchGpu=0, benchPrep=0;
    unsigned benchSamples=0;
    bool demoLanded=false;

    while (running) {
      ++frame;
      if (options.gunSmoke && (frame==2 || frame==62)) {
        SDL_Event key{}; key.type=frame==2 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        key.key.scancode=SDL_SCANCODE_SPACE; key.key.which=0x4f4653;
        key.key.down=frame==2; SDL_PushEvent(&key);
      }
      if (options.smoke) smokeEvents(frame, platform.window(), ui);
      if(options.smoke) {
        // Exercise attachment recreation and both cloud resolutions alongside
        // the existing resize/fullscreen/minimize smoke sequence.
        if(frame==38)graphics.clouds=CloudQuality::Off;
        if(frame==55)graphics.clouds=CloudQuality::Low;
        if(frame==75)graphics.clouds=CloudQuality::High;
        if(frame==90)graphics.cloudCoverage=0;
        if(frame==105)graphics.clouds=CloudQuality::Medium;
        if(frame==135)graphics.cloudCoverage=.48f;
      }
      const auto now = std::chrono::steady_clock::now();
      const double realElapsed = std::chrono::duration<double>(now - last).count();
      last = now;
      // Only smoke mode substitutes time, so its assertions stay repeatable.
      const double elapsed = (options.smoke || options.gunSmoke) ? 1.0 / 60.0 : realElapsed;

#ifdef OFS_NETWORK_ENABLED
      if (options.dogfightSmoke) {
        const double time=std::chrono::duration<double>(now-started).count();
        if ((dogfightSmokeStage==0 && frame>1) ||
            (dogfightSmokeStage==1 && network && network->ready() && time>5) ||
            (dogfightSmokeStage==2 && !network && time>6)) {
          pushKey(platform.window(),SDL_SCANCODE_F5,true);
          pushKey(platform.window(),SDL_SCANCODE_F5,false);
          ++dogfightSmokeStage;
        }
      }
      if (ui.toggleDogfight) {
        ui.toggleDogfight = false;
        if (dogfight) {
          dogfightSmokeShots+=dogfight->world().combat().stats().shots;
          network->disconnect();
          network.reset();
          dogfight.reset();
          options.bots = 0;
          offlineSim = Simulator(definition.flight);
          reset(offlineSim, controls, previous, camera, clock, true);
          dogfightReturnedSolo=true;
        } else if (!network) {
          options.aircraft = dogfightAircraftType(options.aircraft);
          definition = aircraftDefinition(options.aircraft);
          renderer.setAircraftType(options.aircraft);
          ofs::net::ServerConfig config;
          config.bind = "127.0.0.1"; config.port = 0; config.bots = 2;
          dogfight = std::make_unique<ofs::net::Server>(config);
          ++dogfightStarts;
          network = std::make_unique<ofs::net::Client>(options.name, options.aircraft);
          network->prediction().simulator() = Simulator(definition.flight);
          reset(simulation(), controls, previous, camera, clock, true);
          network->connect("127.0.0.1", dogfight->port());
          options.bots = config.bots;
          camera.orbitDistance = definition.visual.radius * 2.3;
          cameraMode = CameraMode::Pursuit;
        }
        ui.multiplayer = bool(network);
        ui.dogfight = bool(dogfight);
        ui.paused = ui.parkingBrake = ui.resetAirborne = ui.resetParked = false;
        networkReady = false;
        missileSelected = missileFireHeld = false;
        missileHits.clear();
        pendingMissileMisses.clear();
        missileOutcome.clear();
        missileOutcomeSeconds = 0;
        missileOutcomeId = 0;
        localGun = LocalGun(options.aircraft);
        soloFlares = soloChaff = int(weapons::decoyCapacity(options.aircraft));
        standing = servicedSeconds = 0;
        pilot.reset(); ejections.clear();
        soloEjected = -1; ejectHold = ejectAsked = 0; ownEjected = false;
        renderer.clearEffects();
        sound.reset();
        clock.reset(); serverClock.reset();
      }
      if (dogfight) {
        dogfight->poll();
        // The local authority must retain wall time even on slow renderers.
        // Dropping server ticks while the network presentation clock advances
        // causes legitimate input to be rejected as too far in the future.
        for (double remaining=realElapsed; remaining>0;) {
          const double slice=std::min(remaining,.1);
          serverClock.advance(slice, [&](double) { dogfight->step(); });
          remaining-=slice;
        }
      }
      if (network) {
        network->poll(realElapsed);
        if (network->ready() && !networkReady) {
          networkReady = true;
          controls = simulation().controls();
          previous = simulation().state();
          camera.frameAircraft(simulation().state().pos_ned);
          ui.parkingBrake = false;
          clock.reset();
          sound.reset();
        }
      }
#endif
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if ((options.smoke || options.networkSmoke || options.combatSmoke ||
             options.missileSmoke || options.dogfightSmoke || !options.scenario.empty() ||
             options.visualBench > 0) &&
            (event.type == SDL_EVENT_KEY_DOWN ||
             event.type == SDL_EVENT_KEY_UP) &&
            event.key.which != 0x4f4653)
          continue;
        // Tab cycles the game camera when the UI has no keyboard focus.
        // Feeding that same key into ImGui activates widget navigation and
        // captures subsequent flight keys.
        const bool cameraKey=(event.type==SDL_EVENT_KEY_DOWN || event.type==SDL_EVENT_KEY_UP) &&
          event.key.scancode==SDL_SCANCODE_TAB && !ImGui::GetIO().WantCaptureKeyboard;
        // While mouse aim holds the pointer, the hidden cursor must not hover or
        // click the interface.
        const bool pointerEvent=event.type==SDL_EVENT_MOUSE_MOTION || event.type==SDL_EVENT_MOUSE_BUTTON_DOWN ||
          event.type==SDL_EVENT_MOUSE_BUTTON_UP || event.type==SDL_EVENT_MOUSE_WHEEL;
        if(!cameraKey && !(input.aiming() && pointerEvent)) ImGui_ImplSDL3_ProcessEvent(&event);
        input.event(event, platform.window());
        if (options.smoke && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.button.button == SDL_BUTTON_RIGHT) {
          std::fprintf(stderr,
                       "[SMOKE] Relative mouse: capture=%d focus=%d mousefocus=%d looking=%d "
                       "IO-pos=%.1f,%.1f error=%s\n",
                       ImGui::GetIO().WantCaptureMouse,
                       bool(SDL_GetWindowFlags(platform.window()) & SDL_WINDOW_INPUT_FOCUS),
                       SDL_GetMouseFocus() == platform.window(), input.looking(),
                       ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y,
                       input.looking() ? "" : SDL_GetError());
        }
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
          running = false;
        if (event.type == SDL_EVENT_MOUSE_MOTION && input.aiming()) {
          if (input.looking() && cameraMode != CameraMode::FirstPerson)
            mouseAim.look(event.motion.xrel, event.motion.yrel, graphics.mouseAimSensitivity);
          else
            mouseAim.move(event.motion.xrel, event.motion.yrel, graphics.mouseAimSensitivity);
        } else if (event.type == SDL_EVENT_MOUSE_MOTION && input.looking()) {
          if (cameraMode == CameraMode::Orbit)
            camera.lookOrbit(event.motion.xrel, event.motion.yrel);
          else
            camera.look(event.motion.xrel, event.motion.yrel);
          sawMouseLook = true;
        }
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) sawFocusRelease = true;
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
          // Esc backs out one step at a time: the chat box, then an open
          // window, then the menu itself. Scripted runs have no menu to open.
          if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            if (ui.chatOpen) ui.chatOpen = false;
            else if (automated) running = false;
            else if (ui.menuOpen && (ui.showSettings || ui.showControls)) ui.showSettings = ui.showControls = false;
            else ui.menuOpen = !ui.menuOpen;
          }
          if (event.key.scancode == SDL_SCANCODE_F11) {
            platform.toggleFullscreen();
            ++fullscreenSwitches;
            graphics.fullscreen = !graphics.fullscreen;
          }
          if (!ImGui::GetIO().WantCaptureKeyboard) {
            if (event.key.scancode == SDL_SCANCODE_TAB) {
              cameraMode = input.cycleCamera(cameraMode);
              sawCameraCycle = true;
            }
            // The switches a pilot can hear being thrown.
            for (const SDL_Scancode key : {SDL_SCANCODE_TAB, SDL_SCANCODE_V, SDL_SCANCODE_X, SDL_SCANCODE_M,
                                           SDL_SCANCODE_F4, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_P, SDL_SCANCODE_T,
                                           SDL_SCANCODE_Y})
              if (event.key.scancode == key) sound.cue(SoundKind::UiClick);
            if (event.key.scancode == SDL_SCANCODE_HOME) camera.frameAircraft(simulation().state().pos_ned);
            if (event.key.scancode == SDL_SCANCODE_G)
              controls.gear01 = controls.gear01 > .5 ? 0 : 1;
            if (event.key.scancode == SDL_SCANCODE_F1)
              graphics.showDevOverlay = !graphics.showDevOverlay;
            if (event.key.scancode == SDL_SCANCODE_P && !ui.multiplayer) ui.paused = !ui.paused;
            if (event.key.scancode == SDL_SCANCODE_F2 && !ui.multiplayer) ui.resetAirborne = true;
            if (event.key.scancode == SDL_SCANCODE_F3 && !ui.multiplayer) ui.resetParked = true;
            if (event.key.scancode == SDL_SCANCODE_BACKSPACE) ui.parkingBrake = !ui.parkingBrake;
            if (event.key.scancode == SDL_SCANCODE_V)
              cameraMode = cameraMode == CameraMode::FirstPerson ? CameraMode::Pursuit : CameraMode::FirstPerson;
            if (event.key.scancode == SDL_SCANCODE_N) fullMap = !fullMap;
#ifdef OFS_NETWORK_ENABLED
            // Chat opens on the slash, or on Enter as in most games.
            if ((event.key.scancode == SDL_SCANCODE_SLASH || event.key.scancode == SDL_SCANCODE_RETURN ||
                 event.key.scancode == SDL_SCANCODE_KP_ENTER) && network && network->ready() && !automated &&
                !ui.menuOpen) {
              ui.chatOpen = ui.chatFocus = true;
              ui.chatBuffer.fill(0);
            }
#endif
            if (event.key.scancode == SDL_SCANCODE_F4) ui.hud.show = !ui.hud.show;
            if (event.key.scancode == SDL_SCANCODE_X && !automated) {
              graphics.mouseAim = !graphics.mouseAim;
              ui.saveSettings = true;
            }
            if (event.key.scancode == SDL_SCANCODE_F5 && ui.botsAvailable &&
                (!ui.multiplayer || ui.dogfight)) ui.toggleDogfight = true;
            if (event.key.scancode == SDL_SCANCODE_F && cameraMode != CameraMode::Free)
              controls.flap01 = controls.flap01 >= .99 ? 0 : std::min(1., controls.flap01 + .25);
            if (event.key.scancode == SDL_SCANCODE_M && hasManeuverMode(simulation().config().control_law))
              controls.maneuver_mode = !controls.maneuver_mode;
            if (event.key.scancode == SDL_SCANCODE_H) {
              // While the brake holds the airbrake out, H changes where it returns to.
              double& airbrake = brakeHeld ? airbrakeBefore : controls.spoiler01;
              airbrake = airbrake > .5 ? 0 : 1;
            }
          }
        }
      }
      if (!running) break;
      if (!renderer.resize(platform.window())) {
        input.release(platform.window());
#ifdef OFS_NETWORK_ENABLED
        if (network && network->ready()) {
          network->setFiring(false);
          controls.elevator_stick = controls.aileron_stick = controls.rudder_pedal = 0;
          controls.steering = 0;
          clock.advance(elapsed, [&](double) { network->predict(controls); });
        }
#endif
        // Nothing is drawn while the window is minimised, and nothing is heard
        // either unless the pilot asked for sound in the background.
        if (!graphics.soundInBackground) {
          SoundScene quiet = sound.scene();
          quiet.master = 0;
          mixer.setScene(quiet);
        }
        bgfx::frame();
        SDL_Delay(10);
        continue;
      }
      {
        // Mouse aim flies from the aircraft views only, and hands the pointer
        // back whenever there is an interface to click or nothing to fly.
        const bool flightView = cameraMode == CameraMode::Pursuit || cameraMode == CameraMode::Chase ||
            cameraMode == CameraMode::CloseChase || cameraMode == CameraMode::FirstPerson;
#ifdef OFS_NETWORK_ENABLED
        const bool flying = network ? network->ready() && network->life().alive()
                                    : !aircraftCrashed(simulation().state());
#else
        const bool flying = !aircraftCrashed(simulation().state());
#endif
        const bool aboard = flying && soloEjected < 0;
        const bool wasAiming = input.aiming();
        input.setAiming(platform.window(), graphics.mouseAim && !automated && flightView && aboard &&
            !graphics.showDevOverlay && !ui.paused && !ui.menuOpen &&
            (SDL_GetWindowFlags(platform.window()) & SDL_WINDOW_INPUT_FOCUS));
        if (input.aiming() && !wasAiming) ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        mouseAim.sync(input.aiming(), simulation().state());
        if (!input.looking() || cameraMode == CameraMode::FirstPerson) mouseAim.clearLook();
        if (mouseAim.active && cameraMode == CameraMode::FirstPerson)
          mouseAim.confine(simulation().state(), std::min(.45, .4 * graphics.cockpitFov * kDeg2Rad));
      }
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();

      const bool captureKeyboard = ImGui::GetIO().WantCaptureKeyboard;
      // An unconscious pilot holds nothing: not the stick, not the trigger.
      const bool conscious = !pilot.incapacitated();
      controls.brake01 = ui.parkingBrake ? 1 : 0;
      if (options.scenario.empty() && options.flightDemo.empty()) {
        input.update(controls, std::min(elapsed, .1), captureKeyboard || cameraMode == CameraMode::Free);
        // Ctrl held with the throttle at idle: wheel brakes, and the airbrake out.
        const bool braking = input.braking();
        if (braking) controls.brake01 = 1;
        if (braking && !brakeHeld) airbrakeBefore = controls.spoiler01;
        if (braking) controls.spoiler01 = 1;
        else if (brakeHeld) controls.spoiler01 = airbrakeBefore;
        brakeHeld = braking;
      }
      // The instructor takes only the axes the keys and the gamepad left neutral.
      if (mouseAim.active && conscious) applyMouseAim(controls, mouseAimCommand(simulation(), mouseAim.direction()));
      if (!conscious) controls.elevator_stick = controls.aileron_stick = controls.rudder_pedal = 0;
      if (soloEjected >= 0) {
        // Nobody is aboard: the controls centre and the engines are left at idle.
        controls.elevator_stick = controls.aileron_stick = controls.rudder_pedal = controls.steering = 0;
        controls.throttle[0] = controls.throttle[1] = 0;
      }
      if (cameraMode == CameraMode::Free) {
        input.freeCamera(camera, std::min(realElapsed, .1), captureKeyboard);
      }
      if (cameraMode == CameraMode::Orbit) camera.zoomOrbit(input.orbitZoom());
      {
        // R drops a flare and C a bundle of chaff, again every third of a
        // second for as long as the key is held.
        const bool flying = !captureKeyboard && cameraMode != CameraMode::Free && !ui.paused && !ui.menuOpen;
        const auto wanted = [&](SDL_Scancode key, double& clock) {
          clock = std::max(0., clock - realElapsed);
          if (!flying || !input.key(key)) { clock = 0; return false; }
          if (clock > 0) return false;
          clock = .33;
          return true;
        };
        for (const auto type : {weapons::DecoyType::Flare, weapons::DecoyType::Chaff}) {
          const bool flare = type == weapons::DecoyType::Flare;
          if (!wanted(flare ? SDL_SCANCODE_R : SDL_SCANCODE_C, flare ? flareClock : chaffClock)) continue;
#ifdef OFS_NETWORK_ENABLED
          if (network) {
            network->weaponAction(flare ? ofs::net::WeaponActionKind::Flare : ofs::net::WeaponActionKind::Chaff);
            continue;
          }
#endif
          int& stock = flare ? soloFlares : soloChaff;
          if (stock <= 0 || aircraftCrashed(simulation().state())) continue;
          --stock;
          const auto decoy = weapons::releaseDecoy(type, {}, simulation().config(), simulation().state(), decoysReleased++);
          releasedDecoys.push_back({type, decoy.position, decoy.velocity});
        }
      }
      {
        // J, held for a second, fires the seat.
        bool aboard = !aircraftCrashed(simulation().state()) && soloEjected < 0;
#ifdef OFS_NETWORK_ENABLED
        if (network) aboard = network->ready() && network->life().alive();
#endif
        ejectAsked = std::max(0., ejectAsked - realElapsed);
        const bool asking = aboard && ejectAsked <= 0 && !automated && !captureKeyboard && cameraMode != CameraMode::Free &&
            !ui.paused && !ui.menuOpen && input.key(SDL_SCANCODE_J);
        ejectHold = asking ? ejectHold + realElapsed : 0;
        if (aboard && options.ejectAt >= 0 && std::chrono::duration<double>(now - started).count() >= options.ejectAt) {
          options.ejectAt = -1;
          ejectHold = kEjectHoldSeconds;
        }
        if (ejectHold >= kEjectHoldSeconds) {
          ejectHold = 0;
#ifdef OFS_NETWORK_ENABLED
          if (network) {
            // The game decides; asked again only if nothing comes of it.
            network->weaponAction(ofs::net::WeaponActionKind::Eject);
            ejectAsked = 2;
          } else
#endif
          {
            ejections.eject(0, true, simulation().state(), options.aircraft);
            sound.at(SoundKind::Eject, simulation().state().pos_ned, 1, true);
            soloEjected = 0;
            log("SIM", "Pilot ejected");
          }
        }
      }
#ifdef OFS_NETWORK_ENABLED
      if (network) {
        using ofs::net::WeaponActionKind;
        if (input.pressed(SDL_SCANCODE_T, captureKeyboard))
          network->weaponAction(WeaponActionKind::NextTarget);
        if (input.pressed(SDL_SCANCODE_Y, captureKeyboard))
          network->weaponAction(WeaponActionKind::PreviousTarget);
        if (input.pressed(SDL_SCANCODE_L, captureKeyboard)) {
          // With the heat seeker up, L breaks its lock so it moves to the next
          // target; otherwise it locks the radar onto whatever is ahead.
          const auto &radar = network->radar();
          const bool heatSeeker = missileSelected && radar.weapon == weapons::WeaponType::Infrared;
          network->weaponAction(heatSeeker || radar.locked.id ? WeaponActionKind::Unlock
                                                               : WeaponActionKind::Lock);
        }
        if (input.pressed(SDL_SCANCODE_1, captureKeyboard)) {
          missileSelected = false;
          sound.cue(SoundKind::WeaponSelect, 1, .9f);
        }
        if (input.pressed(SDL_SCANCODE_2, captureKeyboard)) {
          missileSelected = true;
          network->weaponAction(WeaponActionKind::SelectIR);
          sound.cue(SoundKind::WeaponSelect);
        }
        if (input.pressed(SDL_SCANCODE_3, captureKeyboard)) {
          missileSelected = true;
          network->weaponAction(WeaponActionKind::SelectRadar);
          sound.cue(SoundKind::WeaponSelect, 1, 1.1f);
        }
        const bool fire = conscious &&
            input.firing(captureKeyboard, ImGui::GetIO().WantCaptureMouse);
        const bool missileFire =
            conscious && !captureKeyboard && input.key(SDL_SCANCODE_SPACE);
        if (missileSelected && missileFire && !missileFireHeld) {
          const auto &radar = network->radar();
          for (unsigned i = 0; i < radar.stations.size(); ++i)
            if (radar.stations[i] == radar.weapon) {
              network->weaponAction(WeaponActionKind::Launch, i);
              break;
            }
        }
        if (options.missileSmoke && network->ready()) {
          missileSelected = true;
          const auto &radar = network->radar();
          radarFrames += !radar.tracks.empty();
          missileFrames += !network->missiles().empty();
          const auto tick = network->prediction().tick();
          if (tick >= missileSmokeAction + 60) {
            missileSmokeAction = tick;
            if (!radar.selected.id)
              network->weaponAction(WeaponActionKind::NextTarget);
            else if (!radar.locked.id)
              network->weaponAction(WeaponActionKind::Lock);
            else {
              const auto type =
                  std::count(radar.stations.begin(), radar.stations.end(),
                             weapons::WeaponType::Infrared) > 0
                      ? weapons::WeaponType::Infrared
                      : weapons::WeaponType::ActiveRadar;
              if (radar.weapon != type)
                network->weaponAction(type == weapons::WeaponType::Infrared
                                          ? WeaponActionKind::SelectIR
                                          : WeaponActionKind::SelectRadar);
              else if (radar.seekerReady)
                for (unsigned i = 0; i < radar.stations.size(); ++i)
                  if (radar.stations[i] == type) {
                    network->weaponAction(WeaponActionKind::Launch, i);
                    break;
                  }
            }
          }
        }
        missileFireHeld = missileFire;
        network->setFiring((!missileSelected && fire) || options.networkSmoke ||
                           options.combatSmoke);
      }
#endif
      sawCameraMove = sawCameraMove || (camera.position - initialCamera).norm() > 1;
      sawCameraTurn = sawCameraTurn || std::abs(camera.yaw - initialYaw) > .01;
      sawFlightInput = sawFlightInput || controls.elevator_stick > .9;

      if (!ui.multiplayer && (ui.resetParked || ui.resetAirborne)) {
        sawAirborneReset = sawAirborneReset || ui.resetAirborne;
        reset(simulation(), controls, previous, camera, clock, ui.resetAirborne);
        respawnAirborne = ui.resetAirborne;
        pilot.reset(); ejections.clear();
        soloEjected = -1; ejectHold = 0;
        localGun.reset();
        soloFlares = soloChaff = int(weapons::decoyCapacity(options.aircraft));
        standing = servicedSeconds = 0;
        renderer.clearEffects();
        sound.reset();
        ui.parkingBrake = !ui.resetAirborne;
        ui.paused = false;
        ui.resetParked = ui.resetAirborne = false;
      }
      if (ui.saveSettings) {
        ui.saveSettings = false;
        graphics.showHud = ui.hud.show;
        graphics.showPlayerLabels = ui.hud.showLabels;
        graphics.playerLabelMaxDistance = ui.hud.labelMaxDistance;
        graphics.showMinimap = ui.hud.showMinimap;
        graphics.save();
      }
      if (ui.resetSettings) {
        ui.resetSettings = false;
        GraphicsSettings defaults;
        defaults.configPath = graphics.configPath;
        graphics = defaults;
      }
      if (ui.toggleFullscreen) platform.toggleFullscreen();
      ui.toggleFullscreen = false;
      if (ui.frameAircraft) { camera.frameAircraft(simulation().state().pos_ned); ui.frameAircraft = false; }

      // A solo flight waits while the menu is up; a shared one cannot.
      const bool held = ui.paused || (ui.menuOpen && !ui.multiplayer);
      if (ui.quit) running = false;
      // ---- The pilot: load taken, and time under a parachute ----
      {
        bool aboard = !aircraftCrashed(simulation().state()) && soloEjected < 0;
#ifdef OFS_NETWORK_ENABLED
        if (network) aboard = network->ready() && network->life().alive();
        if (network && aboard && ownEjected) {
          // Back in a new aircraft.
          ownEjected = false;
          ejections.clearOwn();
          camera.smoothingPrimed = false;
        }
#endif
        // Scripted runs fly themselves and are not asked to mind the load.
        if (!aboard || automated) pilot.reset();
        else if (!held) pilot.update(simulation().instruments().g_load, std::min(elapsed, .1));
        const double pilotDt = !options.scenario.empty() ? 1. / 60 : held ? 0. : std::min(elapsed, .1);
        if (soloEjected >= 0) {
          soloEjected += pilotDt;
          if (soloEjected >= kSoloRespawnSeconds) (respawnAirborne ? ui.resetAirborne : ui.resetParked) = true;
        }
        ejections.update(pilotDt, simulation().weather().wind_ned);
      }
      uiClock += realElapsed;
      std::vector<Simulator::GroundImpact> frameImpacts;
      unsigned steps = 0;
      if (!held)
        steps = clock.advance(options.flightDemo.empty()?elapsed:1./60., [&](double dt) {
          previous = simulation().state();
#ifdef OFS_NETWORK_ENABLED
          if (network) {
            if (options.networkSmoke) {
              controls.aileron_stick =
                  std::chrono::duration<double>(now - started).count() < 3
                      ? (options.name == "A" ? .06 : -.06)
                      : 0;
              if (definition.flight.afterburner_thrust_each>0)
                controls.throttle[0]=controls.throttle[1]=1;
            }
            network->predict(controls);
          } else
#endif
          {
            if (!options.flightDemo.empty() && options.flightDemo!="crash") {
              const double t=simulation().state().time;
              const auto n=simulation().instruments();
              if(options.flightDemo=="landing") {
                if(simulation().debugFrame().contact_normal_force>1000) demoLanded=true;
                if(n.agl<12 && !demoLanded) {
                  TrimRequest flare;flare.tas=std::max(30.,n.tas);flare.altitude=std::max(20.,n.alt_msl);flare.flap01=flare.gear01=1;
                  const auto trim=solveTrim(simulation().config(),flare);
                  const double alpha=trim.converged?2*std::atan2(trim.state.att.y,trim.state.att.w):n.alpha_deg*kDeg2Rad;
                  controls.elevator_stick=validationPitchInput(simulation(),alpha+std::asin(-.8/std::max(n.tas,20.))+.02*(-.8-n.vs));
                  controls.throttle[0]=controls.throttle[1]=.2;
                }
                if(demoLanded){controls.elevator_stick=-controls.elevator_trim;controls.brake01=controls.spoiler01=1;controls.throttle[0]=controls.throttle[1]=0;}
              } else if(options.flightDemo=="stall") {
                controls.throttle[0]=controls.throttle[1]=1;
                controls.elevator_stick=t<1?-.2:validationPitchInput(simulation(),2*kDeg2Rad);
              } else {
              const bool taxi=options.flightDemo=="taxi";
              controls.brake01=t<3 || (taxi && t>15) ? 1 : 0;
              controls.throttle[0]=controls.throttle[1]=t<3 || (taxi && t>15) ? 0 : taxi?.28:1;
              controls.steering=taxi && t>7 && t<10 ? .15 : 0;
              if (!taxi) {
                controls.flap01=n.agl<8 ? .35 : std::max(0.,controls.flap01-dt/10);
                const bool reconnaissance=simulation().config().control_law==FlightControlLaw::Delta;
                if(n.tas>(reconnaissance?90:75)) controls.elevator_stick=validationPitchInput(simulation(),(reconnaissance?10:12)*kDeg2Rad);
                if(n.agl>8) controls.gear01=0;
              }
              }
            }
            localGun.step(simulation().state(), dt, options.scenario.empty() && conscious && soloEjected < 0 &&
                input.firing(captureKeyboard, ImGui::GetIO().WantCaptureMouse));
            simulation().setControls(controls);
            simulation().step(dt);
            const auto& impact=simulation().groundImpact();
            if(impact.closingSpeed>4 || (impact.bodyContact && impact.scrapeSpeed>8)) {
              if(frameImpacts.empty())frameImpacts.push_back(impact);
              else if(impact.damage>frameImpacts.front().damage || impact.closingSpeed>frameImpacts.front().closingSpeed)
                frameImpacts.front()=impact;
            }
          }
        });
      totalTicks += steps;
      rateTicks += steps;
      rateElapsed += realElapsed;
      if (rateElapsed >= .5) {
        measuredTicks = static_cast<double>(rateTicks) / rateElapsed;
        rateTicks = 0;
        rateElapsed = 0;
      }


      State aircraft = interpolate(previous, simulation().state(), held ? 1.0 : clock.alpha());
      remotes.clear();
#ifdef OFS_NETWORK_ENABLED
      if (network && network->ready()) {
        aircraft = network->displayState();
        for (const auto& [id, track] : network->remotes()) {
          (void)id;
          if (!track.alive()) continue;
          RemoteAircraft remote;
          const auto sample = track.sampleAircraft(network->stats().renderTick);
          remote.state = sample.state;
          remote.controls = sample.controls;
          remote.type = sample.type;
          // Known by the pilot's name; by the aircraft until the server has said.
          remote.name = network->pilot(id).empty() ? std::string(aircraftDefinition(sample.type).key) : network->pilot(id);
          remote.entity = id;
          remote.health = sample.life.health;
          remote.alive = sample.life.alive();
          remote.kills = sample.life.kills;
          remote.deaths = sample.life.deaths;
          remotes.push_back(remote);
        }
        if (!remotes.empty()) ++remoteFrames;
        // Chat, and everyone in the game for the scoreboard, own row first.
        for (auto& line : network->takeChat()) {
          chatLog.add({std::move(line.name), std::move(line.text), uiClock, line.from == 0, line.from == network->entity()});
          sound.cue(SoundKind::ChatBlip);
        }
        if (ui.chatSubmit) {
          ui.chatSubmit = false;
          ui.chatOpen = false;
          network->chat(chatText(ui.chatBuffer.data(), ofs::net::maxChatText));
        }
        scores.clear();
        scores.push_back({network->pilot(network->entity()).empty() ? network->name() : network->pilot(network->entity()),
                          options.aircraft, network->life().kills, network->life().deaths, true, network->life().alive()});
        for (const auto& [id, track] : network->remotes()) {
          if (!track.size()) continue;
          const auto latest = track.sampleAircraft(network->stats().renderTick);
          scores.push_back({network->pilot(id).empty() ? std::string(aircraftDefinition(latest.type).key) : network->pilot(id),
                            latest.type, latest.life.kills, latest.life.deaths, false, latest.life.alive()});
        }
        std::stable_sort(scores.begin() + 1, scores.end(), [](const auto& a, const auto& b) { return a.kills > b.kills; });
      } else {
        ui.chatOpen = ui.chatSubmit = false;
        scores.clear();
      }
#endif
      combat = {};
      soundMissiles.clear();
      combat.decoys = std::move(releasedDecoys);
      releasedDecoys.clear();
      threats.clear();
      ownMissile.reset();
      for (const auto& event : localGun.takeEvents()) {
        if (event.shot) combat.shots.push_back({event.position,event.velocity,event.lifetime,true,event.projectile,aircraft.vel_ned});
        else combat.hits.push_back({event.position,false,event.projectile});
      }
      combat.groundImpacts=std::move(frameImpacts);
      combat.localHealth=airframeIntegrity(aircraft)*100;
#ifdef OFS_NETWORK_ENABLED
      if (options.networkSmoke) {
        maximumRemotes=std::max(maximumRemotes,remotes.size());
        for (const auto& remote:remotes)
          if (remote.type==AircraftType::Typhoon && remote.state.afterburner[0]>.98 && remote.state.afterburner[1]>.98)
            observedRemoteAfterburner=true;
      }
#endif
      // Combat events are the only source of combat visuals. Positions and
      // generations come straight from the server; nothing is re-derived.
#ifdef OFS_NETWORK_ENABLED
      if (network && network->ready()) {
        // Where an aircraft is flying, so effects on it are carried along with it.
        const auto velocityOf = [&](ofs::net::EntityId entity) {
          if (entity == network->entity()) return aircraft.vel_ned;
          for (const auto& remote : remotes) if (remote.entity == entity) return remote.state.vel_ned;
          return Vec3{};
        };
        // The aircraft as it is drawn this frame, and its type, when it is drawn at all.
        const auto shownAircraft = [&](ofs::net::EntityId entity, AircraftType& type) -> const State* {
          if (entity == network->entity()) { type = options.aircraft; return &aircraft; }
          for (const auto& remote : remotes) if (remote.entity == entity) { type = remote.type; return &remote.state; }
          return nullptr;
        };
        for (const auto& event : network->takeVisualEvents()) {
          switch (event.kind) {
            case ofs::net::CombatKind::Shot: {
              // The round keeps its true velocity and leaves the gun that is seen.
              AircraftType type{};
              const State* shown = shownAircraft(event.owner, type);
              combat.shots.push_back({shown ? shotOrigin(event.position, *shown, type) : event.position, event.velocity,
                                      event.lifetime, event.owner == network->entity(), event.projectile, velocityOf(event.owner)});
              break;
            }
            case ofs::net::CombatKind::Flare:
            case ofs::net::CombatKind::Chaff: {
              const auto decoy = event.kind == ofs::net::CombatKind::Flare ? weapons::DecoyType::Flare : weapons::DecoyType::Chaff;
              AircraftType type{};
              const State* shown = shownAircraft(event.owner, type);
              combat.decoys.push_back({decoy, shown ? decoyOrigin(decoy, event.position, *shown, type, unsigned(event.projectile))
                                                    : event.position, event.velocity});
              break;
            }
            case ofs::net::CombatKind::Serviced:
              if (event.target == network->entity()) { servicedSeconds = 4; standing = 0; sound.cue(SoundKind::Serviced); }
              break;
            case ofs::net::CombatKind::Hit: {
              // Sparks land on the aircraft as it is drawn: ahead of the
              // server's for this pilot, behind it for everyone else.
              const Vec3 carried = velocityOf(event.target);
              const double shownTick = event.target == network->entity() ? double(network->prediction().tick())
                                                                         : network->stats().renderTick;
              combat.hits.push_back({hitOrigin(event.position, carried, (shownTick - double(event.tick)) * ofs::net::tickSeconds),
                                     event.target==network->entity(),event.projectile,carried});
              if (event.owner == network->entity() && event.target != network->entity()) {
                hitMarkerSeconds = .25;
                if (hitCueClock > .07) { sound.cue(SoundKind::HitMarker); hitCueClock = 0; }
              }
              if (event.target == network->entity() && event.owner != network->entity()) {
                damageFlashSeconds = .6;
                lastDamagedPart = event.region;
              }
              if (event.owner == network->entity() &&
                  (event.projectile & (std::uint64_t{1} << 63))) {
                const auto pending = std::find_if(
                    pendingMissileMisses.begin(), pendingMissileMisses.end(),
                    [&](const auto &miss) { return miss.id == event.projectile; });
                const bool alreadyTerminated = pending != pendingMissileMisses.end();
                if (alreadyTerminated)
                  pendingMissileMisses.erase(pending);
                else if (std::find(missileHits.begin(), missileHits.end(),
                                   event.projectile) == missileHits.end())
                  missileHits.push_back(event.projectile);
                if (event.projectile >= missileOutcomeId) {
                  missileOutcome = "MISSILE HIT";
                  missileOutcomeSeconds = 3;
                  missileOutcomeId = event.projectile;
                }
              }
              break;
            }
            case ofs::net::CombatKind::Destroyed: {
              // The wings and fin are thrown clear from where the aircraft was last seen.
              CombatVisuals::Destruction destruction{event.position,event.velocity};
              const auto track = network->remotes().find(event.target);
              if (event.target == network->entity()) {
                destruction.airframe = true;
                destruction.type = options.aircraft;
                destruction.state = aircraft;
              } else if (track != network->remotes().end() && track->second.size()) {
                const auto last = track->second.sampleAircraft(network->stats().renderTick);
                destruction.airframe = true;
                destruction.entity = event.target;
                destruction.type = last.type;
                destruction.state = last.state;
              }
              if (destruction.airframe && destruction.velocity.norm2() == 0) destruction.velocity = destruction.state.vel_ned;
              // It blows up where it is seen, not where the server last had it.
              if (destruction.airframe && (destruction.state.pos_ned - event.position).norm() <= kPresentationReach)
                destruction.position = destruction.state.pos_ned;
              combat.destructions.push_back(destruction);
              if (event.owner == network->entity() && event.target != network->entity()) {
                killMarkerSeconds = 1.2;
                sound.cue(SoundKind::KillChime);
              }
              break;
            }
            case ofs::net::CombatKind::Ejected: {
              // The seat leaves the aircraft as it is seen, when it is seen.
              const bool self = event.target == network->entity();
              AircraftType type{};
              if (const State* shown = shownAircraft(event.target, type)) {
                ejections.eject(event.target, self, *shown, type);
                sound.at(SoundKind::Eject, shown->pos_ned, 1, self);
              } else {
                ejections.eject(event.target, self, event.position, event.velocity);
                sound.at(SoundKind::Eject, event.position);
              }
              ownEjected = ownEjected || self;
              if (self) log("SIM", "Pilot ejected");
              break;
            }
            case ofs::net::CombatKind::Respawn: break;
          }
        }
        // Missiles in flight. The pilot's own are led forward for their first
        // moments so they leave the pylon they were seen hanging on.
        const ofs::net::EntityId self = network->entity();
        std::map<ofs::net::EntityId, std::array<unsigned, 3>> launched;
        std::set<std::uint64_t> presented;
        std::vector<double> sampledTicks;
        const auto flying = network->missilePresentation(&sampledTicks);
        threats.clear();
        for (std::size_t i = 0; i < flying.size(); ++i) {
          const auto &missile = flying[i];
          const auto &d = weapons::missileDefinition(missile.type);
          presented.insert(missile.id);
          // A missile fired at this pilot, for the warning.
          if (missile.target.id == self && missile.target.generation == network->life().generation && network->life().alive())
            threats.push_back({missile.position, missile.velocity, missile.type,
                               missile.seeker == weapons::SeekerPhase::Decoyed});
          if (!presentedMissiles.contains(missile.id) && missile.age < 1)
            ++launched[missile.owner.id][std::size_t(missile.type)];
          Vec3 position = missile.position;
          if (missile.owner.id == self)
            position += launchLead(
                aircraft.vel_ned,
                (double(network->prediction().tick()) - sampledTicks[i]) * ofs::net::tickSeconds,
                missile.age);
          if (missile.owner.id == self && (!ownMissile || missile.age < ownMissile->age))
            ownMissile = OwnMissile{missile.id, position, missile.velocity, missile.age};
          combat.missiles.push_back(
              {missile.id, position, missile.velocity, missile.attitude, d.length, d.diameter, missile.age,
               missile.motor == weapons::MotorPhase::Boost || missile.motor == weapons::MotorPhase::Sustain});
          combat.stores.push_back({position, missile.attitude, missile.type, false,
                                   missile.motor == weapons::MotorPhase::Boost || missile.motor == weapons::MotorPhase::Sustain});
          soundMissiles.push_back({missile.id, position, missile.velocity, combat.missiles.back().powered,
                                   missile.owner.id == self, missile.age});
        }
        presentedMissiles = std::move(presented);
        // Stores on the pylons of every armed aircraft close enough to see.
        const auto hang = [&](ofs::net::EntityId entity, std::uint32_t generation, const State &state,
                              AircraftType type, std::uint8_t mounted, bool local) {
          weapons::Inventory loadout;
          loadout.reset(type);
          if (loadout.stations.empty()) return;
          const std::uint8_t shown = storeDisplay.update(entity, generation, loadout, mounted,
                                                         launched[entity], elapsed);
          const DamageView damage = damageView(state, 100);
          const auto geometry = damageGeometry(type);
          for (std::size_t i = 0; i < loadout.stations.size() && i < 8; ++i) {
            const auto &station = loadout.stations[i];
            // A pylon out on a wing that has been torn away went with it.
            const double span = (std::abs(station.position.y) - geometry.wingRoot) / (geometry.wingTip - geometry.wingRoot);
            if (span > wingRemaining(damage[station.position.y < 0 ? DamagePart::LeftWing : DamagePart::RightWing]) - .05)
              continue;
            const Vec3 position = stationPosition(state, type, station.position);
            combat.pylons.push_back({position, state.att, type, std::uint8_t(i), station.mounted, local});
            if (shown & (1u << i))
              combat.stores.push_back({position, state.att, station.mounted, local});
          }
        };
        if (network->life().alive()) {
          std::uint8_t mounted = 0;
          const auto &stations = network->radar().stations;
          for (std::size_t i = 0; i < stations.size() && i < 8; ++i)
            if (stations[i] != weapons::WeaponType::None) mounted |= std::uint8_t(1u << i);
          hang(self, network->life().generation, aircraft, options.aircraft, mounted, true);
        }
        for (const auto &remote : remotes) {
          if (!remote.alive || (remote.state.pos_ned - camera.eye).norm() > 2500) continue;
          const auto &loadouts = network->radar().loadouts;
          const auto loadout = std::find_if(loadouts.begin(), loadouts.end(), [&](const auto &entry) {
            return entry.entity.id == remote.entity;
          });
          if (loadout != loadouts.end())
            hang(remote.entity, loadout->entity.generation, remote.state, remote.type, loadout->mounted, false);
        }
        storeDisplay.endFrame();
        for (const auto &event : network->takeMissileTerminations()) {
          if (event.missile.owner.id == network->entity()) {
            const auto hit = std::find(missileHits.begin(), missileHits.end(),
                                       event.missile.id);
            if (hit != missileHits.end())
              missileHits.erase(hit);
            else
              pendingMissileMisses.push_back({event.missile.id, .75});
          }
        }
        for (const auto &event : network->takeMissileDetonations()) {
          combat.missileDetonations.push_back(event.missile.position);
          ++missileDetonations;
        }
        for (auto miss = pendingMissileMisses.begin();
             miss != pendingMissileMisses.end();) {
          miss->remaining -= elapsed;
          if (miss->remaining <= 0) {
            if (miss->id >= missileOutcomeId) {
              missileOutcome = "MISSILE MISSED";
              missileOutcomeSeconds = 3;
              missileOutcomeId = miss->id;
            }
            miss = pendingMissileMisses.erase(miss);
          } else {
            ++miss;
          }
        }
        combat.localDestroyed = !network->life().alive();
        combat.localHealth = network->life().health;
        combat.localRespawnSeconds =
            network->life().respawnTick > network->stats().serverTick
                ? double(network->life().respawnTick - network->stats().serverTick) / 120.0
                : 0.0;
      }
#endif
      // ---- Turn-round: standing on the ground repairs, refuels and rearms ----
      // A shared game does the work on the server; here the wait is counted
      // down from what this client can see, and a solo flight serves itself.
      double serviceProgress = -1;
      {
        const auto& config = simulation().config();
        const int decoyStock = int(weapons::decoyCapacity(options.aircraft));
        bool needs = needsRepair(config, aircraft), solo = true;
#ifdef OFS_NETWORK_ENABLED
        solo = !network;
        if (network && network->ready()) {
          const auto& radar = network->radar();
          needs = network->life().alive() &&
              (needs || network->life().health < 100 || (definition.gun && network->life().ammo < definition.gun->ammo) ||
               std::count(radar.stations.begin(), radar.stations.end(), weapons::WeaponType::None) > 0 ||
               (!radar.stations.empty() && (radar.flares < decoyStock || radar.chaff < decoyStock)));
        }
#endif
        if (solo)
          needs = needs || (definition.gun && localGun.ammo() < definition.gun->ammo) || soloFlares < decoyStock ||
                  soloChaff < decoyStock;
        if (!held) standing = standingOnGround(config, aircraft) ? standing + elapsed : 0;
        if (standing > 0 && needs) {
          serviceProgress = std::min(1., standing / serviceSeconds);
          if (solo && standing >= serviceSeconds) {
            auto mended = simulation().state();
            repairAndRefuel(config, mended);
            simulation().setState(mended);
            previous = simulation().state();
            localGun.reset();
            soloFlares = soloChaff = decoyStock;
            servicedSeconds = 4;
            standing = 0;
            serviceProgress = -1;
            sound.cue(SoundKind::Serviced);
            log("SIM", "Repaired, refuelled and rearmed on the ground");
          }
        }
        servicedSeconds = std::max(0., servicedSeconds - realElapsed);
      }
      if(graphics.showPhysicsGeometry)
        for(const auto& line:geometryDebugLines(simulation(),options.aircraft))
          combat.lines.push_back({line.start,line.end,line.color,true});
      // Visual benchmark: synthetic aircraft in a ring, for the performance
      // report. It touches neither the network nor the simulation.
      if (options.visualBench > 0) {
        if (options.afterburnerBench) aircraft.afterburner[0]=aircraft.afterburner[1]=1;
        benchTimer += realElapsed;
        if (benchCount < options.visualBench && benchTimer > 0.4) {
          ++benchCount;
          benchTimer = 0;
        }
        remotes.clear();
        for (int i = 0; i < benchCount-1; ++i) {
          RemoteAircraft remote;
          const double angle = 2.0 * kPi * i / std::max(1, benchCount);
          const double distance = !options.afterburnerBench && i%5==4 ? 2500.0 : 220.0 + 45.0 * (i % 8);
          remote.state.pos_ned = aircraft.pos_ned + Vec3{distance * std::cos(angle), distance * std::sin(angle), -20.0 - 8.0 * (i % 5)};
          remote.state.vel_ned = Vec3{0, 0, 0};
          remote.state.att = Quat{};
          remote.entity = static_cast<std::uint64_t>(i + 1);
          remote.name = "bench" + std::to_string(i);
          remote.type = aircraftDefinitions()[i % aircraftDefinitions().size()].type;
          if (options.visualBench==2) remote.type=options.aircraft;
          if (options.afterburnerBench) {
            remote.type=options.aircraft;
            remote.state.afterburner[0]=remote.state.afterburner[1]=1;
            remote.state.n1[0]=remote.state.n1[1]=1;
            remote.controls.throttle[0]=remote.controls.throttle[1]=1;
          }
          remote.health = 100;
          remotes.push_back(remote);
        }
        if (benchTimer > 4.0 && benchCount >= options.visualBench)
          running = false;
      }
      if (!options.scenario.empty()) {
        // Deterministic visual fixtures exercise the same pose/effects code as
        // gameplay; they do not claim to be authoritative combat recordings.
        const double fixtureTime = frame / 60.0;
        if (options.scenario == "gear") controls.gear01 = frame < 20 ? 1 : 0;
        if (options.scenario == "flaps") controls.flap01 = frame < 20 ? 0 : 1;
        if (options.scenario == "contrail" || options.scenario == "condensation" || options.scenario == "high-aoa" ||
            options.scenario == "breakup" || options.scenario == "missile" || options.scenario == "detonation" ||
            options.scenario == "decoys" || options.scenario == "warning" || options.scenario == "eject" ||
            options.scenario == "blackout") {
          aircraft.pos_ned.x += fixtureTime * aircraft.vel_ned.x;
        }
        // The seat going, the parachute opening and the aircraft flying on;
        // and the view closing in under a hard turn.
        if (options.scenario == "eject" && frame == 12) {
          ejections.eject(0, true, aircraft, options.aircraft);
          sound.at(SoundKind::Eject, aircraft.pos_ned, 1, true);
          soloEjected = 0;
        }
        if (options.scenario == "blackout") pilot.strain = std::min(.86, .3 + fixtureTime * .4);
        if (options.scenario == "decoys" || options.scenario == "warning") {
          // A string of flares with a bundle of chaff among them, left behind
          // as the aircraft flies on.
          if (frame % 9 == 3 && frame < 60) {
            const auto type = frame % 27 == 12 ? weapons::DecoyType::Chaff : weapons::DecoyType::Flare;
            const auto decoy = weapons::releaseDecoy(type, {}, simulation().config(), aircraft, frame / 9);
            combat.decoys.push_back({type, decoy.position, decoy.velocity});
          }
        }
        if (options.scenario == "warning") {
          // A heat seeker closing from the right rear quarter, and a radar
          // missile that has gone after chaff.
          threats.push_back({aircraft.pos_ned + aircraft.att.rotate({-1100, 650, 120}),
                             aircraft.vel_ned + aircraft.att.rotate({520, -300, -50}), weapons::WeaponType::Infrared, false});
          threats.push_back({aircraft.pos_ned + aircraft.att.rotate({900, -2100, -300}),
                             aircraft.vel_ned + aircraft.att.rotate({-200, 600, 80}), weapons::WeaponType::ActiveRadar, true});
        }
        if (options.scenario == "service") {
          // Stopped on a stand with a wing shot through, part-way through the wait.
          applyPartDamage(simulation().config(), aircraft, DamagePart::RightWing, 45);
          serviceProgress = frame < 60 ? .62 : -1;
          servicedSeconds = frame < 60 ? 0 : 3;
        }
        if (options.scenario == "missile" || options.scenario == "detonation") {
          // One missile of each kind flying in formation off the right wing,
          // motors burning, so the airframes, plumes and trails can be judged.
          for (unsigned i = 0; i < 2; ++i) {
            const auto type = i ? weapons::WeaponType::ActiveRadar : weapons::WeaponType::Infrared;
            const auto& d = weapons::missileDefinition(type);
            const Vec3 position = aircraft.pos_ned + aircraft.att.rotate({4. + 3 * i, 7. + 3.5 * i, -1.5 + .8 * i});
            combat.missiles.push_back({1000 + i, position, aircraft.vel_ned, aircraft.att, d.length, d.diameter, fixtureTime, true});
            soundMissiles.push_back({1000 + i, position, aircraft.vel_ned, true, true, fixtureTime});
            combat.stores.push_back({position, aircraft.att, type, false, true});
          }
          if (options.scenario == "detonation" && frame == 36)
            combat.missileDetonations.push_back(aircraft.pos_ned + aircraft.att.rotate({70, 26, -4}));
        }
        // The menu and its reference window, for a look at them without a keyboard.
        if (options.scenario == "menu" || options.scenario == "controls") {
          ui.menuOpen = true;
          ui.showControls = options.scenario == "controls";
        }
        if (options.scenario == "chat") {
          // A conversation and the box it is typed in, as a multiplayer pilot sees them.
          if (frame == 1) {
            chatLog.add({"", "Maya joined the game", uiClock, true, false});
            chatLog.add({"Maya", "anyone up for a 2 v 2?", uiClock, false, false});
            chatLog.add({"pilot", "in. give me a minute to climb", uiClock, false, true});
            chatLog.add({"", "Maya shot down Bandit 3", uiClock, true, false});
            std::snprintf(ui.chatBuffer.data(), ui.chatBuffer.size(), "on your six");
          }
          ui.chatOpen = true;
        }
        if (options.scenario == "breakup") {
          // A wing is shot through, snaps, and the aircraft then blows up.
          if (frame >= 20) applyPartDamage(simulation().config(), aircraft, DamagePart::LeftWing, frame >= 40 ? 100 : 60);
          if (frame >= 40) applyPartDamage(simulation().config(), aircraft, DamagePart::RightEngine, 100);
          if (frame >= 55) applyPartDamage(simulation().config(), aircraft, DamagePart::Tail, 100);
          combat.localHealth = frame >= 40 ? 40 : frame >= 20 ? 75 : 100;
          const unsigned finale = 80;
          if (frame == finale) {
            CombatVisuals::Destruction destruction{aircraft.pos_ned, aircraft.vel_ned, true, 0, options.aircraft, aircraft};
            combat.destructions.push_back(destruction);
          }
          if (frame >= finale) combat.localDestroyed = true;
        }
        if (options.scenario == "mixed") {
          const std::array<Vec3,3> offsets{{{12,30,0},{-4,-26,0},{-10,-60,0}}};
          unsigned index=0;
          for(const auto& selected:aircraftDefinitions())if(selected.type!=options.aircraft) {
            RemoteAircraft remote;remote.type=selected.type;remote.entity=index+1;
            remote.name=std::string(selected.displayName);remote.state.pos_ned=aircraft.pos_ned+offsets[index++];
            remote.controls.gear01=1;remotes.push_back(remote);
          }
        }
        if (options.scenario=="afterburner-multiple") {
          for (unsigned i=1;i<=3;++i) {
            RemoteAircraft remote;remote.entity=i;remote.type=options.aircraft;
            remote.state=aircraft;remote.controls=controls;
            remote.state.pos_ned+=Vec3{double(i*6),double(i*14),-double(i*2)};
            remotes.push_back(remote);
          }
        }
        if (options.scenario=="afterburner-transition") {
          const double target=frame<90?0.:frame<210?1.:0.;
          static double level=0;level+=(target-level)*(1.-std::exp(-1./(60*.16)));
          aircraft.afterburner[0]=aircraft.afterburner[1]=level;
        }
        if (options.scenario == "gun" && definition.gun && frame % 6 == 0)
          combat.shots.push_back({aircraft.pos_ned+aircraft.att.rotate(definition.gun->muzzle-loadedCg(definition.flight,aircraft)), aircraft.att.rotate({definition.gun->muzzleVelocity,0,0}),definition.gun->lifetime,true});
        if (options.scenario == "impact" && frame % 18 == 0)
          combat.hits.push_back({aircraft.pos_ned+Vec3{2,4,-1},false});
        if (options.scenario == "destruction" && frame == 60)
          combat.destructions.push_back({aircraft.pos_ned,aircraft.vel_ned*.15});
        if (options.scenario == "destruction" && frame >=60) combat.localDestroyed=true;
        if (options.scenario == "damage") combat.localHealth=55;
        if (options.scenario == "damage-heavy") combat.localHealth=20;
      }
      if ((options.networkSmoke || options.combatSmoke) && !remotes.empty()) {
        const Vec3 center = (aircraft.pos_ned + remotes.front().state.pos_ned) * .5;
        cameraMode = CameraMode::Free;
        camera.frameAircraft(center);
        const double distance=std::max(100.0,(aircraft.pos_ned-remotes.front().state.pos_ned).norm()*.6);
        camera.position = center + Vec3{-distance, -distance, -distance*.4};
        camera.yaw=kPi*.25;
        camera.pitch=-std::atan(.4/std::sqrt(2.0));
      }

      // Resolve the camera, then render and overlay from the same pose.
      const bool firstFrame = frame == 1;
      const double renderDt = !options.scenario.empty() || !options.flightDemo.empty() ? 1.0/60.0 : std::min(realElapsed, .1);
      const Vec3 aimView = mouseAim.viewDirection();
      camera.dynamicFov = graphics.dynamicFov;
      if (options.scenario.empty() && options.flightDemo.empty() && !options.smoke) applyRealTime(graphics.sky);
      camera.update(cameraMode, aircraft, renderDt, firstFrame, options.aircraft, mouseAim.active ? &aimView : nullptr);
      if(cameraMode==CameraMode::FirstPerson)camera.fov=graphics.cockpitFov;
      // A pilot who has left the aircraft is followed down instead of it.
      const EjectedPilot* outside = cameraMode != CameraMode::Free ? ejections.own() : nullptr;
      if (outside)
        camera.watch(outside->position, std::atan2(outside->forward.y, outside->forward.x), 21, 4.5, renderDt);
      // Holding U rides along with the missile the pilot last launched.
      // When it ends the view stays there a second, on the explosion.
      const bool missileKey = !outside && cameraMode != CameraMode::Free && !automated &&
          !ImGui::GetIO().WantCaptureKeyboard && !ui.menuOpen && input.key(SDL_SCANCODE_U);
      const bool riddenFlying = std::any_of(combat.missiles.begin(), combat.missiles.end(),
                                            [&](const auto& missile) { return missile.id == riddenMissile.id; });
      if (!missileKey) missileViewLinger = 0;
      else if (!riddenFlying && missileViewLinger > 0) missileViewLinger -= renderDt;
      else if (ownMissile) { riddenMissile = *ownMissile; missileViewLinger = 1; }
      const bool missileView = missileKey && (ownMissile || missileViewLinger > 0) && missileViewLinger > 0;
      if (missileView) camera.ride(riddenMissile.position, riddenMissile.velocity);
      const CameraMode viewMode = outside || missileView ? CameraMode::Chase : cameraMode;
      combat.pilots = ejections.pilots();
      if (simulation().origin().rebaseIfNeeded(camera.eye)) log("RENDER", "Render origin rebased");
      combat.gunPointValid = definition.gun.has_value() && cameraMode != CameraMode::Free && cameraMode != CameraMode::Orbit;
      if (combat.gunPointValid)
        combat.gunPoint = aircraft.pos_ned + aircraft.att.rotate(definition.gun->muzzle-loadedCg(definition.flight,aircraft) + definition.gun->direction*1000);
      renderer.render(camera, aircraft, controls, remotes, combat, simulation().origin().origin_ned, held && options.scenario.empty() ? 0. : renderDt, simulation().instruments().g_load, simulation().weather());
      if (options.visualBench>0 && benchCount==options.visualBench && benchTimer>1) {
        const auto& stats=renderer.stats(); ++benchSamples; benchSeconds+=realElapsed;
        benchCpu+=stats.cpuFrameMs; benchGpu+=stats.gpuFrameMs; benchPrep+=stats.preparationMs;
      }
#ifdef OFS_NETWORK_ENABLED
      if (options.combatSmoke && renderer.stats().activeParticles>0) ++combatFrames;
#endif

      HudFrame hud;
      hud.local = &aircraft;
      hud.controls = &controls;
      hud.instruments = simulation().instruments();
      hud.cameraMode = viewMode;
      hud.type = options.aircraft;
      hud.parkingBrake = ui.parkingBrake;
      hud.paused = ui.paused;
      hud.remotes = remotes;
      hud.gunPointValid = combat.gunPointValid;
      hud.gunPoint = combat.gunPoint;
      hud.mouseAimEnabled = graphics.mouseAim;
      hud.mouseAim = mouseAim.active;
      hud.mouseAimPoint = camera.eye + mouseAim.direction() * 4000;
      hud.nosePoint = camera.eye + aircraft.att.rotate({4000, 0, 0});
      hud.fullMap = fullMap;
      hud.alive = !aircraftCrashed(aircraft);
      hud.visionLoss = pilot.vision();
      hud.redOut = pilot.red;
      hud.unconscious = pilot.incapacitated();
      hud.ejectHold = ejectHold / kEjectHoldSeconds;
      if (soloEjected >= 0) {
        hud.alive = false;
        hud.ejected = true;
        hud.respawnSeconds = std::max(0., kSoloRespawnSeconds - soloEjected);
        hud.respawnSpan = kSoloRespawnSeconds;
      }
      hud.health = airframeIntegrity(aircraft)*100;
      hud.damage = damageView(aircraft, combat.localHealth);
      hud.now = uiClock;
      hud.menuOpen = ui.menuOpen;
      hud.ammo = localGun.ammo();
      hud.gunReady = localGun.ready();
      hud.braking = controls.brake01 > .5 && !ui.parkingBrake;
      hud.serviceProgress = serviceProgress;
      hud.serviced = servicedSeconds;
      if (weapons::decoyCapacity(options.aircraft)) { hud.flares = soloFlares; hud.chaff = soloChaff; }
      hud.threats = threats;
      hud.firing = input.firing(captureKeyboard, ImGui::GetIO().WantCaptureMouse);
#ifdef OFS_NETWORK_ENABLED
      if (network && network->ready()) {
        hud.multiplayer = true;
        hud.dogfight = bool(dogfight);
        const auto &radar = network->radar();
        hud.radarTracks = radar.tracks;
        hud.selectedTarget = radar.selected;
        hud.lockedTarget = radar.locked;
        for (const auto &[id, missile] : network->missiles()) {
          (void)id;
          if (missile.owner.id == network->entity()) {
            ++hud.activeMissiles;
            hud.missileSpeed = missile.velocity.norm();
            hud.missileAge = missile.age;
          }
        }
        hud.missileWeapon = radar.weapon;
        if (!radar.stations.empty()) { hud.flares = radar.flares; hud.chaff = radar.chaff; }
        hud.seekerReady = radar.seekerReady;
        hud.seekerTarget = radar.seekerTarget;
        hud.lockProgress = radar.lockProgress;
        hudStations.clear();
        weapons::Inventory loadout;
        loadout.reset(options.aircraft);
        for (std::size_t i = 0; i < loadout.stations.size(); ++i)
          hudStations.push_back({loadout.stations[i].mounted,
                                 i < radar.stations.size() && radar.stations[i] != weapons::WeaponType::None});
        hud.stations = hudStations;
        hudMissiles.clear();
        for (const auto &missile : combat.missiles)
          if (network->missiles().contains(missile.id) && network->missiles().at(missile.id).owner.id == network->entity())
            hudMissiles.push_back(missile.position);
        hud.ownMissiles = hudMissiles;
        hud.envelope = radar.envelope;
        hud.missileSelected = missileSelected;
        if (missileOutcomeSeconds > 0) {
          missileOutcomeSeconds -= elapsed;
          if (missileOutcomeSeconds <= 0)
            missileOutcome.clear();
        }
        hud.missileOutcome = missileOutcome;
        for (const auto weapon : radar.stations) {
          hud.irCount += weapon == weapons::WeaponType::Infrared;
          hud.radarCount += weapon == weapons::WeaponType::ActiveRadar;
        }
        hud.alive = network->life().alive();
        hud.ejected = ownEjected && !hud.alive;
        hud.health = network->life().health;
        hud.damagedPart = lastDamagedPart;
        hud.hitMarker = hitMarkerSeconds;
        hud.killMarker = killMarkerSeconds;
        hud.damageFlash = damageFlashSeconds;
        hitMarkerSeconds = std::max(0., hitMarkerSeconds - elapsed);
        killMarkerSeconds = std::max(0., killMarkerSeconds - elapsed);
        damageFlashSeconds = std::max(0., damageFlashSeconds - elapsed);
        hud.scores = scores;
        hud.showScores = !captureKeyboard && input.key(SDL_SCANCODE_K);
        hud.pingMs = network->stats().pingMs;
        hud.ammo = network->life().ammo;
        hud.gunReady = network->stats().serverTick >= network->life().readyTick;
        hud.respawnSeconds = combat.localRespawnSeconds;
      }
#endif
#ifdef OFS_NETWORK_ENABLED
      if (network && !network->ready()) {
        // Never a silent, frozen aircraft: say what the connection is doing.
        const std::string& status = network->status();
        const bool joining = status == "connecting" || status == "handshaking";
        hud.bannerProblem = !joining;
        hud.bannerTitle = joining ? "JOINING GAME" : "NOT CONNECTED";
        hud.bannerDetail = status == "connecting" ? "Contacting " + (dogfight ? std::string("the local game") : options.server) + " ..."
            : status == "handshaking" ? "Waiting for the host ..."
            : status == "protocol version mismatch" ? "The host is running a different version of the game.   Esc for the menu"
            : status == "server full" || status == "world full" ? "That game is full.   Esc for the menu"
            : (status.empty() || status == "offline" ? std::string("The game could not be reached.") : status) + "   Esc for the menu";
      }
#endif
      const auto chatLines = chatLog.visible(uiClock, ui.chatOpen, ui.chatOpen ? 12 : 6);
      hud.chat = chatLines;
      hud.chatOpen = ui.chatOpen;
      drawHud(hud, ui.hud, renderer);

      // ---- Sound: this frame as it is heard from the camera ----
      {
        const Vec3 gunPosition = definition.gun
            ? aircraft.pos_ned + aircraft.att.rotate(definition.gun->muzzle - loadedCg(definition.flight, aircraft))
            : aircraft.pos_ned;
        for (const auto& shot : combat.shots)
          sound.at(SoundKind::Gun, shot.ownAircraft ? gunPosition : shot.position, 1, shot.ownAircraft);
        for (const auto& hit : combat.hits)
          sound.at(hit.ownAircraft ? SoundKind::HitTaken : SoundKind::Impact, hit.ownAircraft ? aircraft.pos_ned : hit.position,
                   1, hit.ownAircraft);
        // An aircraft that goes into the ground is heard to hit it; one that
        // blows up in the air is not.
        const auto ending = [](const Vec3& position) {
          return groundHeightNed(position.x, position.y) - position.z < 15 ? SoundKind::Crash : SoundKind::Explosion;
        };
        for (const auto& destruction : combat.destructions)
          sound.at(ending(destruction.position), destruction.position, 1, destruction.airframe && !destruction.entity);
        for (const auto& position : renderer.wreckImpacts()) sound.at(SoundKind::Crash, position);
        for (const auto& part : renderer.partsLost()) sound.at(SoundKind::PartBreak, part.position, 1, part.own);
        for (const auto& out : ejections.pilots())
          if (out.opened) sound.at(SoundKind::Parachute, out.position, 1, out.own);
        for (const auto& position : combat.missileDetonations) sound.at(SoundKind::Detonation, position);
        for (const auto& decoy : combat.decoys)
          sound.at(decoy.type == weapons::DecoyType::Flare ? SoundKind::Flare : SoundKind::Chaff, decoy.position, 1,
                   (decoy.position - aircraft.pos_ned).norm() < 40);
        // A hard arrival is one crunch; sliding on afterwards is a scrape.
        crunchClock += realElapsed;
        for (const auto& impact : combat.groundImpacts) {
          if (impact.bodyContact && !impact.water) sound.scrape(float(std::clamp(impact.scrapeSpeed / 60, 0., 1.)));
          if (impact.closingSpeed > 4 && (impact.damage > 0 || impact.bodyContact) && crunchClock > .3) {
            sound.at(impact.water ? SoundKind::Splash : SoundKind::Crunch, aircraft.pos_ned, float(std::clamp(.3 + impact.closingSpeed / 25, .3, 1.2)), true);
            crunchClock = 0;
          }
        }
        // A solo crash has no server to announce it.
        const bool crashed = aircraftCrashed(aircraft);
        if (crashed && !wasCrashed && !hud.multiplayer) sound.at(ending(aircraft.pos_ned), aircraft.pos_ned, 1, soloEjected < 0);
        wasCrashed = crashed;
        hitCueClock += realElapsed;
        if (hud.firing && !triggerWasHeld && !hud.missileSelected && definition.gun && hud.ammo == 0 && hud.alive)
          sound.cue(SoundKind::DryFire);
        triggerWasHeld = hud.firing;
        if (ui.menuOpen != menuWasOpen) sound.cue(ui.menuOpen ? SoundKind::UiOpen : SoundKind::UiClose);
        if (fullMap != mapWasOpen) sound.cue(fullMap ? SoundKind::UiOpen : SoundKind::UiClose, .8f, 1.25f);
        menuWasOpen = ui.menuOpen; mapWasOpen = fullMap;
        if (ImGui::GetIO().WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) sound.cue(SoundKind::UiClick);

        SoundFrame heard;
        heard.dt = options.soundCapture.empty() ? std::min(realElapsed, .25) : renderDt;
        heard.held = held && options.scenario.empty();
        heard.focused = !speakers || (SDL_GetWindowFlags(platform.window()) & SDL_WINDOW_INPUT_FOCUS);
        heard.eye = camera.eye;
        heard.forward = camera.target - camera.eye;
        heard.up = camera.renderOrientation().rotate({0, 0, -1});
        heard.attached = cameraMode != CameraMode::Free && !outside;
        heard.cockpit = viewMode == CameraMode::FirstPerson;
        heard.type = options.aircraft;
        heard.alive = hud.alive && !combat.localDestroyed;
        heard.instruments = hud.instruments;
        const auto forces = simulation().debugFrame();
        heard.qbar = forces.qbar;
        heard.onWheels = forces.contact_normal_force > 0;
        heard.gearCommand = controls.gear01;
        heard.own = &aircraft;
#ifdef OFS_NETWORK_ENABLED
        // Nothing is flying until the game has been joined.
        if (network && !network->ready()) heard.own = nullptr;
#endif
        // Under a parachute there is no aircraft to hear from the inside.
        if (hud.ejected) heard.own = nullptr;
        heard.strain = hud.visionLoss;
        heard.unconscious = hud.unconscious;
        soundAircraft.clear();
        for (const auto& remote : remotes)
          if (remote.alive) soundAircraft.push_back({&remote.state, remote.type, remote.entity});
        heard.others = soundAircraft;
        heard.missiles = soundMissiles;
        soundThreats.clear();
        for (const auto& threat : threats) soundThreats.push_back({threat.position, threat.velocity, threat.decoyed});
        heard.threats = soundThreats;
        heard.missileSelected = hud.missileSelected;
        heard.weapon = hud.missileWeapon;
        heard.seekerTracking = hud.seekerTarget.id != 0;
        heard.seekerReady = hud.seekerReady;
        heard.lockProgress = hud.lockProgress;
        heard.radarLocked = hud.lockedTarget.id != 0;
        heard.health = hud.health;
        heard.enabled = graphics.sound;
        heard.muteUnfocused = !graphics.soundInBackground;
        heard.master = graphics.soundVolume;
        heard.volume = {graphics.engineVolume, graphics.weaponVolume, graphics.airframeVolume, graphics.cockpitVolume};
        sound.update(heard);
        if (!options.soundCapture.empty()) {
          const std::size_t count = std::size_t(std::lround(renderDt * kSoundRate));
          capturedSound.resize(capturedSound.size() + count * 2);
          mixer.render(capturedSound.data() + capturedSound.size() - count * 2, count);
        }
      }

      debugUi(simulation(), controls, camera, clock, steps, realElapsed, measuredTicks, renderer, assetName,
              remotes.size() + 1, input.connected(), ui, graphics);
      renderer.applySettings(graphics, platform);

#ifdef OFS_NETWORK_ENABLED
      if (network && graphics.showDevOverlay) {
        ImGui::SetNextWindowPos({760, 14}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Network diagnostics")) {
          const auto& n = network->stats();
          const auto& p = network->prediction().stats();
          ImGui::Text("%s | entity %llu", network->status().c_str(),
                      (unsigned long long)network->entity());
          ImGui::Text("Server tick %llu | RTT %d ms", (unsigned long long)n.serverTick, n.pingMs);
          ImGui::Text("Snapshots %.1f Hz | inputs %.1f Hz", n.snapshotHz, n.inputHz);
          ImGui::Text("Pending %zu | corrections %llu", network->prediction().pending().size(),
                      (unsigned long long)p.reconciliations);
          ImGui::Text("Prediction error %.4f m | peak %.4f m", p.error, p.maxError);
          ImGui::Text("Messages in/out %llu/%llu", (unsigned long long)n.received,
                      (unsigned long long)n.sent);
          ImGui::Text("GNS wire in/out %.0f/%.0f B/s", n.wireIn, n.wireOut);
          ImGui::Text("Payload bytes %llu/%llu", (unsigned long long)n.bytesIn,
                      (unsigned long long)n.bytesOut);
          const auto& life = network->life();
          ImGui::Separator();
          ImGui::Text("Health %.0f / 100 | ammo %u", life.health, life.ammo);
          ImGui::Text("Gun %s | life %u | kills/deaths %u/%u",
                      !life.alive() ? "disabled"
                                    : (n.serverTick >= life.readyTick ? "ready" : "cooldown"),
                      life.generation, life.kills, life.deaths);
          if (!life.alive())
            ImGui::Text("DESTROYED | respawn in %.1f s",
                        double(life.respawnTick > n.serverTick ? life.respawnTick - n.serverTick : 0) / 120);
          ImGui::Text("Shots observed %llu | hits received %llu | visual rounds %zu",
                      (unsigned long long)n.shots, (unsigned long long)n.hitsReceived,
                      network->visualCount());
          ImGui::Text("Last hit target %llu region %u", (unsigned long long)network->latestHit().target,
                      unsigned(network->latestHit().region));
          ImGui::TextUnformatted("Gun fire: Space / left mouse / right trigger");
          ImGui::TextUnformatted("Weapons: 1 gun / 2 IR / 3 active radar");
          ImGui::TextUnformatted(
              "Radar: T/Y target / L lock + missiles");
          ImGui::Text("Remote %zu | samples %zu | render tick %.1f", network->remotes().size(),
                      n.historySamples, n.renderTick);
        }
        ImGui::End();
      }
#endif
      ImGui::Render();
      renderer.ui();

      const unsigned screenshotFrame = options.smoke ? 150u :
          (options.frames ? (options.frames > 2 ? options.frames - 2u : 1u) : 90u);
      bool captureMissile = false;
#ifdef OFS_NETWORK_ENABLED
      captureMissile = options.missileSmoke && missileFrames > 5 &&
                       std::any_of(network->missiles().begin(), network->missiles().end(),
                                   [](const auto &pair) { return pair.second.age > .4; }) &&
                       network->radar().locked.id &&
                       !renderer.screenshotWritten();
#endif
      if ((options.missileSmoke
               ? captureMissile
                       : ((options.networkSmoke || options.combatSmoke || options.dogfightSmoke)
                      ? (std::chrono::duration<double>(now - started).count() >
                             (options.dogfightSmoke ? 8 : 6) &&
                         !renderer.screenshotWritten())
                      : frame == screenshotFrame)) &&
          !options.screenshot.empty())
        renderer.screenshot(options.screenshot);
      bgfx::frame();
      if (options.frames && frame >= options.frames) running = false;
      if (options.seconds && now - started >= std::chrono::seconds(options.seconds)) running = false;
      if (options.smoke && frame > 220) throw std::runtime_error("Smoke event loop did not quit");
    }
    input.release(platform.window());
    speakers.reset();
    if (!options.soundCapture.empty()) {
      if (!writeWave(options.soundCapture, capturedSound)) throw std::runtime_error("Cannot write " + options.soundCapture);
      std::fprintf(stderr, "[SOUND] CAPTURE %.2f s peak=%.3f path=%s\n", double(capturedSound.size()) / (2. * kSoundRate),
                   double(mixer.peak()), options.soundCapture.c_str());
    }

    if (options.smoke) {
      std::fprintf(stderr,
                   "[CORE] SMOKE checks: move=%d turn=%d controls=%d look=%d focus=%d "
                   "fullscreen=%u UI-reset=%d camcycle=%d altitude=%.1f screenshot=%d ticks=%llu\n",
                   sawCameraMove, sawCameraTurn, sawFlightInput, sawMouseLook, sawFocusRelease,
                   fullscreenSwitches, sawAirborneReset, sawCameraCycle,
                   simulation().instruments().alt_msl, renderer.screenshotWritten(),
                   (unsigned long long)totalTicks);
      const auto flight = simulation().instruments();
      const auto reference=solveTrim(simulation().config());
      double referenceRoll,referencePitch,referenceHeading;
      eulerFromQuat(reference.state.att,referenceRoll,referencePitch,referenceHeading);
      if (!reference.converged || !sawCameraMove || !sawCameraTurn || !sawAirborneReset || !sawCameraCycle ||
          std::abs(flight.alt_msl - 1000) > 1 || std::abs(flight.tas - 110) > .1 ||
          std::abs(flight.pitch_deg - referencePitch*kRad2Deg) > .1 || !sawFlightInput || !sawMouseLook ||
          !sawFocusRelease || fullscreenSwitches != 2 || totalTicks < 240 ||
          !renderer.screenshotWritten() || !std::isfinite(simulation().state().pos_ned.norm()))
        throw std::runtime_error("Graphical smoke assertion failed");
      std::fprintf(stderr,
                   "[CORE] SMOKE PASS: camera/mouse/flight controls, focus release, resize, "
                   "2 fullscreen switches, minimize/restore, ImGui airborne reset, camera "
                   "cycle, %llu ticks, screenshot, quit\n",
                   (unsigned long long)totalTicks);
    }
    if (options.visualBench > 0) {
      const auto& s = renderer.stats();
      std::fprintf(stderr,
                   "[VISUAL] BENCH aircraft=%d fps=%.1f cpuFrameMs=%.2f gpuFrameMs=%.2f "
                   "draws=%u tris=%u particles=%u lod=%s prepMs=%.3f tiers=%u/%u/%u/%u\n",
                   benchCount, benchSamples/std::max(benchSeconds,.001), benchCpu/std::max(benchSamples,1u),
                   benchGpu/std::max(benchSamples,1u), s.drawCalls, s.triangles,
                   s.activeParticles, s.lodTier.c_str(),benchPrep/std::max(benchSamples,1u),s.lodCounts[0],s.lodCounts[1],s.lodCounts[2],s.lodCounts[3]);
    }
    if (options.gunSmoke) {
      if (!definition.gun || localGun.ammo()>=definition.gun->ammo || renderer.stats().particlePeak<3)
        throw std::runtime_error("solo input/gun/tracer graphical smoke failed");
      std::fprintf(stderr,"[COMBAT] SOLO GRAPHICAL PASS ammo=%u/%u peakParticles=%zu\n",
          localGun.ammo(),definition.gun->ammo,renderer.stats().particlePeak);
    }
    if (!options.flightDemo.empty()) {
      const auto n=simulation().instruments();
      std::fprintf(stderr,"[VALIDATION] PHYSICS DEMO %s time=%.3f TAS=%.3f altitude=%.3f x=%.3f AB=%.3f/%.3f integrity=%.3f crashed=%d\n",
        options.flightDemo.c_str(),simulation().state().time,n.tas,n.alt_msl,simulation().state().pos_ned.x,simulation().state().afterburner[0],simulation().state().afterburner[1],airframeIntegrity(simulation().state()),aircraftCrashed(simulation().state()));
    }
#ifdef OFS_NETWORK_ENABLED
    if (options.missileSmoke) {
      std::fprintf(stderr,
                   "[M4] diagnostic ready=%d radar=%u missiles=%u "
                   "detonations=%u selected=%llu lock=%llu snapshots=%llu\n",
                   network->ready(), radarFrames, missileFrames,
                   missileDetonations,
                   (unsigned long long)network->radar().selected.id,
                   (unsigned long long)network->radar().locked.id,
                   (unsigned long long)network->stats().snapshots);
      if (!network->ready() || radarFrames < 30 || missileFrames < 10 ||
          missileDetonations < 1 || !renderer.screenshotWritten())
        throw std::runtime_error("graphical missile smoke failed");
      std::fprintf(stderr,
                   "[M4] GRAPHICAL PASS radarFrames=%u missileFrames=%u "
                   "detonations=%u particlePeak=%zu hits=%llu\n",
                   radarFrames, missileFrames, missileDetonations,
                   renderer.stats().particlePeak,
                   (unsigned long long)network->stats().hitsReceived);
    }
    if (options.combatSmoke) {
      const auto& n = network->stats();
      const auto& life = network->life();
      if (!network->ready() || remoteFrames < 30 || combatFrames < 10 || n.shots < 32 || n.hitsReceived < 4 ||
          n.destructions < 2 || n.respawns < 2 || life.generation == 0 ||
          !ofs::net::finiteState(simulation().state()))
        throw std::runtime_error("graphical combat smoke failed: ready=" + std::to_string(network->ready()) +
            " remoteFrames=" + std::to_string(remoteFrames) + " effectFrames=" + std::to_string(combatFrames) +
            " shots=" + std::to_string(n.shots) + " hits=" + std::to_string(n.hitsReceived) +
            " destructions=" + std::to_string(n.destructions) + " respawns=" + std::to_string(n.respawns) +
            " generation=" + std::to_string(life.generation));
      std::fprintf(stderr,
                   "[COMBAT] GRAPHICAL PASS name=%s remoteFrames=%u shots=%llu "
                   "hitsReceived=%llu destroyed=%llu respawns=%llu life=%u health=%.0f "
                   "kills=%u deaths=%u\n",
                   options.name.c_str(), remoteFrames, (unsigned long long)n.shots,
                   (unsigned long long)n.hitsReceived, (unsigned long long)n.destructions,
                   (unsigned long long)n.respawns, life.generation, life.health, life.kills,
                   life.deaths);
      std::fprintf(stderr, "[COMBAT] Rendered effect frames=%u\n",combatFrames);
    }
    if (dogfight) {
      std::fprintf(stderr,"[DOGFIGHT] connection=%s serverTick=%llu invalid=%llu bots=%zu remotes=%zu\n",
          network->status().c_str(), (unsigned long long)dogfight->world().tick(),
          (unsigned long long)dogfight->stats().invalid,
          dogfight->world().botCount(),network->remotes().size());
      if ((options.seconds || options.frames || options.dogfightSmoke) &&
          (!network->ready() || remoteFrames == 0 || dogfight->world().botCount() != options.bots))
        throw std::runtime_error("local dogfight did not connect or render opponents");
      const auto &stats = dogfight->world().combat().stats();
      std::fprintf(stderr, "[DOGFIGHT] bots=%zu remoteFrames=%u shots=%llu hits=%llu kills=%llu respawns=%llu\n",
          dogfight->world().botCount(), remoteFrames, (unsigned long long)stats.shots,
          (unsigned long long)stats.hits, (unsigned long long)stats.kills,
          (unsigned long long)stats.respawns);
      if (options.dogfightSmoke) {
        if (dogfightStarts!=2 || !dogfightReturnedSolo || dogfightSmokeStage!=3 ||
            options.aircraft!=AircraftType::Su57 ||
            stats.shots+dogfightSmokeShots==0 || dogfight->stats().invalid!=0)
          throw std::runtime_error("F5 dogfight start/stop/restart smoke failed");
        std::fprintf(stderr,"[DOGFIGHT] F5 START / SOLO RETURN / RESTART PASS\n");
      }
    }
    if (options.dogfightSmoke && !dogfight)
      throw std::runtime_error("dogfight smoke did not start opponents");
    if (options.networkSmoke) {
      if (!network->ready() || remoteFrames < 30 || !network->stats().snapshots ||
          !ofs::net::finiteState(simulation().state()))
        throw std::runtime_error("graphical network smoke failed");
      std::fprintf(stderr,
                   "[NET] GRAPHICAL PASS name=%s id=%llu remoteFrames=%u snapshots=%llu "
                   "roll=%.2f tick=%llu\n",
                   options.name.c_str(), (unsigned long long)network->entity(), remoteFrames,
                   (unsigned long long)network->stats().snapshots, simulation().instruments().roll_deg,
                   (unsigned long long)network->stats().serverTick);
      const auto& state=simulation().state();
      std::fprintf(stderr,"[NET] ENGINE localType=%u afterburner=%.3f/%.3f remotes=%zu maximumRemotes=%zu remoteAfterburner=%d\n",
                   static_cast<unsigned>(options.aircraft),state.afterburner[0],state.afterburner[1],network->remotes().size(),maximumRemotes,observedRemoteAfterburner);
    }
#endif
    log("CORE", "Client exiting cleanly");
    return 0;
  } catch (const std::exception& error) {
    log("CORE", error.what());
    return 1;
  }
}
