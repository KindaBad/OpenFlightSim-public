// Games on the round Earth (docs/WHOLE_EARTH_PLAN.md, phase 6): the map in
// the handshake, weapon positions relative to the viewer, the map in a LAN
// lobby's answer, and a game played in a theatre. The last needs the terrain
// pack and is skipped without it, as in CI.
#include "ofs/earth.hpp"
#include "ofs/earth_terrain.hpp"
#include "ofs/net/client.hpp"
#include "ofs/net/discovery.hpp"
#include "ofs/net/server.hpp"
#include "ofs/net/weapons_protocol.hpp"
#include "ofs/theatres.hpp"
#include "ofs/trim.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace ofs;
using namespace ofs::net;

namespace {
void check(bool ok, const std::string &what) {
  if (!ok)
    throw std::runtime_error(what);
}

// Hello and Welcome carry the map; nonsense maps are refused.
void handshake() {
  const GameMap alps{MapKind::Earth, "alps", 0x1234567890abcdefull, "LSGS"};
  Message hello;
  hello.type = Type::Hello;
  hello.text = "pilot";
  hello.map = alps;
  Message out;
  std::string reason;
  check(decode(encode(hello), out, reason) && out.map.sameGame(alps) && out.map.airport == "LSGS", "hello carries the map");
  Message welcome;
  welcome.type = Type::Welcome;
  welcome.snapshotHz = 24;
  const auto trim = solveTrim(a320Config());
  welcome.aircraft = {7, 0, trim.state, trim.controls, {100, 0}, AircraftType::A320};
  welcome.map = alps;
  welcome.map.airport.clear();
  check(decode(encode(welcome), out, reason) && out.map.sameGame(alps), "welcome carries the map");
  hello.map = {};
  check(decode(encode(hello), out, reason) && out.map.kind == MapKind::Valley && out.map.theatre.empty(), "the valley");
  for (const GameMap &bad : {GameMap{MapKind::Valley, "alps", 0, ""}, GameMap{MapKind::Valley, "", 5, ""},
                            GameMap{MapKind::Earth, "", 5, ""}, GameMap{MapKind::Earth, "alps", 0, ""},
                            GameMap{MapKind::Earth, "al ps", 5, ""}, GameMap{MapKind::Earth, "alps", 5, "LS-GS"}}) {
    hello.map = bad;
    check(!decode(encode(hello), out, reason), "a malformed map is refused");
  }
  hello.map = alps;
  auto bytes = encode(hello);
  bytes[bytes.size() - (1 + 4) - 8 - (1 + 4) - 1] = 2;  // the kind byte, before theatre, pack and airport
  check(!decode(bytes, out, reason), "an unknown map kind is refused");
  // Who may join where.
  const GameMap valley{}, gulf{MapKind::Earth, "gulf", alps.pack, ""}, other{MapKind::Earth, "alps", 99, ""};
  check(mapMismatch(valley, valley).empty() && mapMismatch(alps, alps).empty(), "same map");
  check(mapMismatch(alps, GameMap{MapKind::Earth, "alps", alps.pack, "LSZH"}).empty(), "the airport is the client's choice");
  check(mapMismatch(valley, alps).find("valley") != std::string::npos, "an Earth client is told the game is in the valley");
  check(mapMismatch(alps, valley).find("alps") != std::string::npos, "a valley client is told the game is on the Earth");
  check(mapMismatch(alps, gulf).find("alps") != std::string::npos, "another theatre");
  check(mapMismatch(alps, other).find("data") != std::string::npos, "another pack");
  std::puts("earthnet.handshake PASS");
}

// Missile and radar positions keep their resolution far from the game's origin.
void relative() {
  const Vec3 viewer{2.6e6 + 123.4, -1.9e6 - 77.7, -9000.25};
  check(weaponReference(viewer).x == 2600000 && weaponReference(viewer).y == -1901000 && weaponReference(viewer).z == -10000,
        "the reference is the kilometre below the viewer");
  WeaponMessage m;
  m.type = Type::MissileSpawn;
  m.tick = 10;
  m.sequence = 1;
  m.reference = weaponReference(viewer);
  MissileNetState missile;
  missile.id = 4;
  missile.owner = {2, 1};
  missile.type = weapons::WeaponType::Infrared;
  missile.model = weapons::missileModel(missile.type, AircraftType::Typhoon);
  missile.position = viewer + Vec3{12345.678, -8765.4321, 250.0625};
  missile.velocity = {300, 20, -5};
  missile.attitude = Quat{};
  m.missiles = {missile};
  WeaponMessage back;
  check(decodeWeapon(encodeWeapon(m), back), "missile far from the origin round trip");
  check((back.missiles[0].position - missile.position).norm() < .1, "missile position to an eighth of a metre");
  m.reference = viewer;
  bool refused = false;
  try {
    encodeWeapon(m);
  } catch (const std::invalid_argument &) {
    refused = true;
  }
  check(refused, "a reference off the kilometre grid is a sender's error");

  WeaponMessage radar;
  radar.type = Type::RadarState;
  radar.tick = 12;
  radar.sequence = 2;
  radar.reference = weaponReference(viewer);
  radar.radar.stations = {weapons::WeaponType::Infrared};
  weapons::Track track;
  track.entity = {9, 1};
  track.position = viewer + Vec3{-60000.3, 41000.7, 2000.2};
  track.velocity = {200, 0, 0};
  track.quality = 1;
  track.lastDetection = 12 * tickSeconds;
  radar.radar.tracks = {track};
  check(decodeWeapon(encodeWeapon(radar), back) && (back.radar.tracks[0].position - track.position).norm() < .6,
        "a radar track far from the origin, to half a metre");

  // The sender leaves out what lies beyond reach of the viewer's reference.
  WeaponReplicationSender sender;
  Missile near, far;
  near.id = 1;
  near.owner = {7, 1};
  near.type = weapons::WeaponType::Nuclear;
  near.model = weapons::MissileModel::DevInfrared;
  near.state.position = viewer + Vec3{1000, 0, 0};
  near.state.attitude = Quat{};
  far = near;
  far.id = 2;
  far.state.position = viewer + Vec3{900000, 0, 0};
  const std::vector<Missile> missiles{near, far};
  const auto packets = sender.build(12, 7, viewer, RadarNetState{}, missiles, {});
  std::size_t seen = 0;
  for (const auto &packet : packets) {
    WeaponMessage decoded;
    check(decodeWeapon(packet.bytes, decoded), "the sender's packets decode");
    for (const auto &n : decoded.missiles) {
      check(n.id == 1 && (n.position - near.state.position).norm() < .1, "only the missile in reach, where it is");
      ++seen;
    }
  }
  check(seen == 1, "one missile sent");
  std::puts("earthnet.relative PASS");
}

// A lobby on the Earth names its map after the version; one in the valley is
// byte for byte as before, so older launchers list only valley games.
void lobby() {
  LobbyInfo valley;
  valley.name = "Friday night";
  valley.version = "0.7.0";
  valley.port = 27020;
  valley.protocol = protocolVersion;
  valley.players = 2;
  valley.maxPlayers = 16;
  LobbyInfo earth = valley;
  earth.map = "earth:alps";
  const auto plain = encodeLobby(valley), mapped = encodeLobby(earth);
  check(mapped.size() == plain.size() + 1 + earth.map.size() && std::equal(plain.begin(), plain.end(), mapped.begin()),
        "the map follows the version");
  LobbyInfo decoded;
  check(decodeLobby(mapped, decoded) && decoded.map == "earth:alps" && decoded.name == valley.name, "an Earth lobby decodes");
  check(decodeLobby(plain, decoded) && decoded.map.empty(), "a valley lobby has no map");
  auto empty = plain;
  empty.push_back(0);
  check(!decodeLobby(empty, decoded), "an empty map is refused");
  auto extra = mapped;
  extra.push_back('x');
  check(!decodeLobby(extra, decoded), "anything after the map is refused");
  std::puts("earthnet.lobby PASS");
}

// A game in the western Alps: a client with the same map and pack starts at
// the airport it asked for; one in the valley is turned away with the reason.
void game(const std::filesystem::path &packPath) {
  const EarthTerrain terrain(packPath);
  const Theatre &alps = *findTheatre("alps");
  const EarthWorld earth(alps.latitude * kDeg2Rad, alps.longitude * kDeg2Rad, &terrain);
  useWorld(earth);
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 0;
  config.airborne = false;
  useEarthTheatre(config, alps, terrain, earth);
  check(config.starts.size() == alps.airports.size(), "a start at every airport");
  Server server(config);
  LobbyInfo info = server.lobby();
  check(info.map == "earth:alps", "the game says where it is played");

  const GameMap map{MapKind::Earth, "alps", terrain.contentHash(), "LSZH"};
  Client pilot("pilot", AircraftType::Typhoon);
  pilot.setMap(map);
  pilot.prediction().simulator() = Simulator(aircraftDefinition(AircraftType::Typhoon).flight);
  pilot.connect("127.0.0.1", server.port());
  Client stranger("stranger", AircraftType::A320);
  stranger.connect("127.0.0.1", server.port());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  while (std::chrono::steady_clock::now() < deadline && (!pilot.ready() || stranger.status() == "handshaking" ||
                                                         stranger.status() == "connecting")) {
    server.poll();
    server.step();
    pilot.poll(tickSeconds);
    stranger.poll(tickSeconds);
    if (pilot.ready())
      pilot.predict(pilot.prediction().simulator().controls());
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  check(pilot.ready(), "the pilot with the Alps joins");
  check(!stranger.ready() && stranger.status().find("alps") != std::string::npos,
        "the valley client is told the game is on the Earth: " + stranger.status());
  // The pilot stands on Zurich's longest runway, on the ground.
  const auto &state = server.world().players().begin()->second.sim.state();
  const EarthRunway &runway = *theatreRunway(terrain, *findTheatreAirport(alps, "LSZH"));
  const Vec3 at = earth.toEcef(state.pos_ned) - runway.centre;
  const double along = at.dot(runway.along), across = (at - runway.along * along - runway.up * at.dot(runway.up)).norm();
  std::printf("pilot %.0f m along and %.1f m off Zurich's runway, %.2f m above it\n", along, across,
              earth.altitude(state.pos_ned) - earth.terrain(state.pos_ned).altitude);
  check(std::abs(along) < runway.length * .5 && across < 3, "on the runway's centre line");
  check(std::abs(earth.altitude(state.pos_ned) - earth.terrain(state.pos_ned).altitude) < 4, "on the ground");
  pilot.disconnect();
  stranger.disconnect();
  server.shutdown();

  // An Earth game needs its starts, and is free flight.
  bool refused = false;
  try {
    ServerConfig bots = config;
    bots.bots = 2;
    Server invalid(bots);
  } catch (const std::invalid_argument &) {
    refused = true;
  }
  check(refused, "no bots on the Earth yet");
  std::puts("earthnet.game PASS");
}
}  // namespace

int main(int argc, char **argv) {
  try {
    const std::string suite = argc > 1 ? argv[1] : "handshake";
    if (suite == "handshake")
      handshake();
    else if (suite == "relative")
      relative();
    else if (suite == "lobby")
      lobby();
    else if (suite == "game") {
      const std::filesystem::path pack = argc > 2 ? argv[2] : "";
      if (!std::filesystem::is_regular_file(pack)) {
        std::printf("[NOT RUN] no Earth terrain pack at '%s'\n", pack.string().c_str());
        return 77;
      }
      game(pack);
    } else
      throw std::runtime_error("unknown suite " + suite);
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "earthnet FAIL: %s\n", e.what());
    return 1;
  }
}
