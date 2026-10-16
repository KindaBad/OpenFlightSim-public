// The round Earth on a smooth planet: geodesy, the local axes, and aircraft,
// bombs and sight lines far from the game's anchor. docs/WHOLE_EARTH_PLAN.md.
#include "scenario.hpp"
#include "ofs/aircraft_definition.hpp"
#include "ofs/earth.hpp"
#include "ofs/earth_terrain.hpp"
#include "ofs/theatres.hpp"
#include "ofs/water.hpp"
#include <bit>
#include <chrono>
#include <memory>
#include "ofs/weapons.hpp"
#include <cstdio>
#include <string>
#include <vector>

using namespace ofs;
using namespace scenario;

namespace {
constexpr double kDeg = kDeg2Rad;
std::string packPath;

double distance(const Vec3& a, const Vec3& b) { return (a - b).norm(); }

void geodesy() {
  const Vec3 equator = ecefFromGeodetic({0, 0, 0});
  check(distance(equator, {kEarthEquatorialRadius, 0, 0}) < 1e-6, "0N 0E on the equator");
  const double polar = kEarthEquatorialRadius * (1 - kEarthFlattening);
  check(distance(ecefFromGeodetic({90 * kDeg, 0, 0}), {0, 0, polar}) < 1e-6, "north pole at the polar radius");
  double worst = 0, worstAngle = 0;
  for (int i = 0; i < 2000; ++i) {
    const Geodetic g{(-90 + 180 * ((i * 7919) % 2000) / 1999.) * kDeg, (-180 + 359.9 * ((i * 104729) % 2000) / 1999.) * kDeg,
                     -400 + 40000 * ((i * 31) % 97) / 96.};
    const Geodetic back = geodeticFromEcef(ecefFromGeodetic(g));
    worst = std::max(worst, std::abs(back.altitude - g.altitude));
    worstAngle = std::max(worstAngle, std::abs(back.latitude - g.latitude));
    if (std::abs(g.latitude) < 89.9 * kDeg)
      worstAngle = std::max(worstAngle, std::abs(std::remainder(back.longitude - g.longitude, 2 * kPi)));
  }
  std::printf("geodesy round trip: altitude %.3g m, angle %.3g rad\n", worst, worstAngle);
  check(worst < 1e-6 && worstAngle < 1e-12, "geodetic round trip");
}

void frame() {
  const EarthWorld earth(47 * kDeg, 13 * kDeg);
  check(std::abs(earth.altitude({0, 0, 0})) < 1e-6, "anchor at sea level");
  check(distance(earth.down({0, 0, 0}), {0, 0, 1}) < 1e-12, "down is +z at the anchor");
  check(distance(earth.toLocal({0, 0, -1000}, Vec3{3, -4, 5}), {3, -4, 5}) < 1e-9, "local axes are the game axes at the anchor");
  check(std::abs(earth.altitude({0, 0, -1000}) - 1000) < 1e-6, "altitude straight above the anchor");
  // 100 km north the vertical has turned by about 0.9 degrees, toward the anchor.
  const Vec3 north = earth.atAltitude({100000, 0, 0}, 0);
  const Geodetic g = earth.geodetic(north);
  check(std::abs(g.altitude) < 1e-6 && std::abs(g.longitude - 13 * kDeg) < 1e-9, "point placed at sea level due north");
  check(north.z > 700 && north.z < 900, "the sea falls away below the anchor's horizon");
  const double tilt = std::acos(earth.down(north).dot({0, 0, 1})) / kDeg;
  std::printf("100 km north: sea %.1f m below the anchor plane, vertical turned %.3f deg\n", north.z, tilt);
  check(tilt > .85 && tilt < .95 && earth.down(north).x < 0, "vertical turns with distance");
  // Local axes and attitudes round trip far away.
  const Vec3 far = earth.position({-33 * kDeg, 151 * kDeg, 9000});
  const Vec3 v{12, -7, 3};
  check(distance(earth.fromLocal(far, earth.toLocal(far, v)), v) < 1e-9, "local vector round trip");
  const Quat q = quatFromEuler(.3, -.2, 2.1);
  const Quat r = earth.toLocal(far, earth.fromLocal(far, q));
  check(std::abs(std::abs(q.w * r.w + q.x * r.x + q.y * r.y + q.z * r.z) - 1) < 1e-12, "attitude round trip");
  check(std::abs(earth.upward(far, earth.up(far)) - 1) < 1e-12 && distance(earth.up(far), -earth.down(far)) < 1e-15,
        "up and down");
}

// A trimmed aircraft: heading in degrees, placed at a latitude and longitude.
Simulator placed(const EarthWorld& earth, const AircraftConfig& config, double latitude, double longitude,
                 double heading, double altitude, double speed, Simulator::GroundModel ground = Simulator::GroundModel::Terrain) {
  TrimRequest request;
  request.tas = speed;
  request.altitude = altitude;
  const auto trim = solveTrim(config, request);
  check(trim.converged, "trim");
  const Quat turn = quatFromEuler(0, 0, heading * kDeg);
  State s = trim.state;
  s.pos_ned = earth.position({latitude * kDeg, longitude * kDeg, altitude});
  s.vel_ned = earth.fromLocal(s.pos_ned, turn.rotate(trim.state.vel_ned));
  s.att = earth.fromLocal(s.pos_ned, turn * trim.state.att);
  Simulator sim(config, ground);
  sim.setState(s);
  sim.setControls(trim.controls);
  return sim;
}

// Holds altitude and wings level with the ordinary controls.
Controls holding(const Simulator& sim, double altitude, double trimPitch) {
  Controls c = sim.controls();
  const auto n = sim.instruments();
  const double pitch = trimPitch + std::clamp(.002 * (altitude - n.alt_msl) - .01 * n.vs, -.08, .08);
  c.elevator_stick = pitchInput(sim, pitch);
  c.aileron_stick = std::clamp(-.04 * n.roll_deg - .5 * sim.state().omega_body.x, -.5, .5);
  return c;
}

// Near the anchor the round Earth and the valley agree.
void near() {
  const EarthWorld earth(0, 0);
  for (const auto& d : aircraftDefinitions()) {
    if (d.flight.engine_count == 0) continue;
    useWorld(earth);
    auto round = placed(earth, d.flight, 0, 0, 0, 3000, 200, Simulator::GroundModel::FlatRunway);
    advance(round, 1200);
    static const FlatWorld valley;
    useWorld(valley);
    TrimRequest request;
    request.tas = 200;
    request.altitude = 3000;
    const auto trim = solveTrim(d.flight, request);
    Simulator flat(d.flight, Simulator::GroundModel::FlatRunway);
    flat.setState(trim.state);
    flat.setControls(trim.controls);
    advance(flat, 1200);
    useWorld(earth);
    const double apart = distance(round.state().pos_ned, flat.state().pos_ned);
    const double climb = round.instruments().alt_msl - flat.instruments().alt_msl;
    std::printf("%-8s 10 s near the anchor: %.2f m apart, altitude %+.2f m, pitch %+.3f deg\n", std::string(d.key).c_str(),
                apart, climb, round.instruments().pitch_deg - flat.instruments().pitch_deg);
    check(apart < 3 && std::abs(climb) < 1.5, "round and flat agree near the anchor");
  }
}

// Wings level and altitude held, an aircraft flies a great circle: it stays in
// the plane through the centre of the Earth that it started in.
void cruise() {
  const EarthWorld earth(30 * kDeg, 10 * kDeg);
  useWorld(earth);
  auto sim = placed(earth, a320Config(), 30, 10, 60, 3000, 200, Simulator::GroundModel::FlatRunway);
  const double trimPitch = sim.instruments().pitch_deg * kDeg;
  const Vec3 start = earth.toEcef(sim.state().pos_ned);
  const Vec3 plane = start.cross(earth.toEcef(sim.state().pos_ned + sim.state().vel_ned) - earth.toEcef(sim.state().pos_ned)).normalized();
  double low = 1e9, high = -1e9, offPlane = 0, roll = 0;
  auto m = run(sim, 120 * 3000, [&](int, const Simulator& s) { return holding(s, 3000, trimPitch); },
               [&](int tick, const Simulator& s) {
                 if (tick < 120 * 120) return;
                 const auto n = s.instruments();
                 low = std::min(low, n.alt_msl);
                 high = std::max(high, n.alt_msl);
                 roll = std::max(roll, std::abs(n.roll_deg));
                 offPlane = std::max(offPlane, std::abs(earth.toEcef(s.state().pos_ned).dot(plane)));
               });
  const Vec3 end = earth.toEcef(sim.state().pos_ned);
  const double travelled = std::acos(std::clamp(start.normalized().dot(end.normalized()), -1., 1.)) * 6371000;
  const Geodetic g = earth.geodetic(sim.state().pos_ned);
  std::printf("cruise %.0f km to %.3fN %.3fE: altitude %.1f..%.1f m, %.0f m off the great circle, roll %.2f deg\n",
              travelled / 1000, g.latitude / kDeg, g.longitude / kDeg, low, high, offPlane, roll);
  check(m.min_speed > 150 && travelled > 500000, "a long flight");
  check(low > 2950 && high < 3050 && roll < 2, "altitude and wings level held all the way");
  check(offPlane < travelled * .002, "a great circle");
}

// Far from the anchor gravity still points at the local ground.
void drop() {
  const EarthWorld earth(0, 0);
  useWorld(earth);
  for (const Geodetic at : {Geodetic{10 * kDeg, 20 * kDeg, 1500}, Geodetic{-60 * kDeg, 140 * kDeg, 1500},
                            Geodetic{89 * kDeg, -100 * kDeg, 1500}}) {
    Vec3 impact;
    double seconds = 0;
    const bool hit = weapons::bombImpact(weapons::bombDefinition(weapons::WeaponType::Bomb500), earth.position(at),
                                         {}, Weather{}, impact, seconds);
    const Geodetic g = earth.geodetic(impact);
    const double drift = distance(earth.toEcef(impact), ecefFromGeodetic({at.latitude, at.longitude, g.altitude}));
    std::printf("bomb dropped at %.0fN %.0fE: lands %.3f m from below, at %.2f m, after %.1f s\n", at.latitude / kDeg,
                at.longitude / kDeg, drift, g.altitude, seconds);
    check(hit && drift < .05 && std::abs(g.altitude) < 1 && seconds > 15 && seconds < 25, "falls straight down");
  }
}

// An aircraft parked far away stands on its wheels as it does in the valley.
void parked() {
  const EarthWorld earth(0, 0);
  for (const auto& d : aircraftDefinitions()) {
    double rest[2]{};
    for (int round = 0; round < 2; ++round) {
      static const FlatWorld valley;
      if (round) useWorld(earth); else useWorld(valley);
      State s;
      Simulator sim(d.flight, Simulator::GroundModel::FlatRunway);
      if (round) {
        s.pos_ned = earth.position({-41 * kDeg, 174 * kDeg, d.flight.gear_nose.z - .15});
        s.att = earth.fromLocal(s.pos_ned, quatFromEuler(0, 0, 1.2));
      } else {
        s.pos_ned.z = -(d.flight.gear_nose.z - .15);
        s.att = quatFromEuler(0, 0, 1.2);
      }
      sim.setState(s);
      Controls c;
      c.brake01 = 1;
      sim.setControls(c);
      advance(sim, 120 * 20);
      const auto n = sim.instruments();
      check(n.tas < .05 && std::abs(n.vs) < .01 && std::abs(n.roll_deg) < .5, "stands still");
      check(std::abs(std::remainder(n.hdg_deg - 1.2 / kDeg, 360.)) < .5 && !aircraftCrashed(sim.state()), "on its heading, undamaged");
      rest[round] = n.alt_msl;
    }
    std::printf("%-8s parked at 41S 174E: %.4f m, in the valley %.4f m\n", std::string(d.key).c_str(), rest[1], rest[0]);
    check(std::abs(rest[1] - rest[0]) < .01, "the same height as in the valley");
  }
}

// The planet's bulge hides what is beyond the horizon.
void sight() {
  const EarthWorld earth(20 * kDeg, 40 * kDeg);
  useWorld(earth);
  const auto at = [&](double north, double altitude) { return earth.position({(20 + north / 111.2) * kDeg, 40 * kDeg, altitude}); };
  check(weapons::lineOfSight(at(0, 10), at(5, 10)), "5 km along the surface at 10 m: seen");
  check(!weapons::lineOfSight(at(0, 10), at(50, 10)), "50 km along the surface at 10 m: hidden by the curve");
  check(weapons::lineOfSight(at(0, 10000), at(300, 10000)), "300 km apart at 10 km: seen");
  check(!weapons::lineOfSight(at(0, 10000), at(800, 10000)), "800 km apart at 10 km: hidden");
}

// The instruments read the local horizon wherever the aircraft is.
void instruments() {
  const EarthWorld earth(0, 0);
  useWorld(earth);
  for (const double heading : {0., 90., 225.}) {
    auto sim = placed(earth, a320Config(), 55, -120, heading, 4000, 180, Simulator::GroundModel::FlatRunway);
    const auto n = sim.instruments();
    std::printf("at 55N 120W heading %.0f: reads %.3f deg, altitude %.3f m, roll %.4f, climb %.4f m/s\n", heading, n.hdg_deg,
                n.alt_msl, n.roll_deg, n.vs);
    check(std::abs(std::remainder(n.hdg_deg - heading, 360.)) < 1e-6 && std::abs(n.alt_msl - 4000) < 1e-6 &&
              std::abs(n.roll_deg) < 1e-6 && std::abs(n.vs) < 1e-6,
          "heading, altitude, wings level and climb read locally");
  }
}

// ---- With the terrain pack (scripts/build_earth_terrain.py) ----------------
std::unique_ptr<EarthTerrain> pack;

struct Place {
  const char* name;
  double latitude, longitude;
};
double groundAt(const EarthWorld& earth, double latitude, double longitude) {
  return earth.terrain(earth.position({latitude * kDeg, longitude * kDeg, 0})).altitude;
}
double waterAt(const EarthWorld& earth, double latitude, double longitude) {
  return earth.waterAltitude(earth.position({latitude * kDeg, longitude * kDeg, 0}));
}

void places() {
  const EarthWorld earth(46 * kDeg, 8 * kDeg, pack.get());
  std::printf("%zu tiles, %zu runways\n", pack->tileCount(), pack->runways().size());
  check(pack->tileCount() == 73710 && pack->runways().size() > 10000, "the whole planet and its runways");
  const auto report = [&](const char* name, double lat, double lon) {
    const double ground = groundAt(earth, lat, lon), water = waterAt(earth, lat, lon);
    std::printf("  %-14s ground %7.1f m, water %s\n", name, ground,
                water > kNoWater + 1 ? (std::to_string(int(std::lround(water))) + " m").c_str() : "none");
    return std::pair{ground, water};
  };
  auto [blanc, blancWater] = report("Mont Blanc", 45.8326, 6.8652);
  check(blanc > 4000 && blancWater < kNoWater + 1, "the Alps are high and dry");
  auto [everest, everestWater] = report("Everest", 27.9881, 86.9250);
  check(everest > 8000, "Everest");
  auto [vienna, viennaWater] = report("Vienna", 48.2082, 16.3738);
  check(vienna > 100 && vienna < 400 && viennaWater < kNoWater + 1, "Vienna in the Danube basin");
  auto [atlantic, atlanticWater] = report("Atlantic", 40, -30);
  check(atlantic < -10 && atlanticWater == 0, "the open sea");
  auto [gulf, gulfWater] = report("Persian Gulf", 27, 51);
  check(gulf < 0 && gulfWater == 0, "the Persian Gulf");
  auto [geneva, genevaWater] = report("Lake Geneva", 46.45, 6.55);
  check(std::abs(genevaWater - 376) < 10 && geneva < genevaWater - 1, "Lake Geneva");
  auto [caspian, caspianWater] = report("Caspian Sea", 42, 50.5);
  check(std::abs(caspianWater + 28) < 3 && caspian < caspianWater, "the Caspian below sea level");
  auto [dead, deadWater] = report("Dead Sea", 31.5, 35.5);
  check(deadWater < -400 && dead < deadWater, "the Dead Sea");
  auto [qattara, qattaraWater] = report("Qattara", 29.6, 27.0);
  check(qattara < 0 && qattaraWater < kNoWater + 1, "the Qattara Depression is dry below sea level");
}

// No seams: along a meridian through tile edges and the edge of a cube face.
void seams() {
  const EarthWorld earth(45 * kDeg, 10 * kDeg, pack.get());
  double previous = groundAt(earth, 43, 10), worst = 0, worstAt = 0;
  const double metre = 1 / 111132.;
  const auto start = std::chrono::steady_clock::now();
  int count = 0;
  for (double lat = 43 + metre; lat < 47; lat += metre, ++count) {
    const double ground = groundAt(earth, lat, 10);
    if (std::abs(ground - previous) > worst) worst = std::abs(ground - previous), worstAt = lat;
    previous = ground;
  }
  const double each = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / count;
  std::printf("%d one-metre steps from 43N to 47N along 10E: steepest %.2f m at %.4fN, %.2f us a sample\n", count, worst,
              worstAt, each);
  check(worst < 2, "no seams or cliffs between samples a metre apart");
}

const EarthRunway& nearestRunway(double latitude, double longitude) {
  const Vec3 at = ecefFromGeodetic({latitude * kDeg, longitude * kDeg, 0});
  const EarthRunway* best = nullptr;
  for (const auto& r : pack->runways())
    if (!best || (r.centre - at).norm() < (best->centre - at).norm()) best = &r;
  return *best;
}

// Vienna's runway 11/29 is level across and straight along.
void runway() {
  const EarthRunway& r = nearestRunway(48.1103, 16.5697);
  const EarthWorld earth(48.11 * kDeg, 16.57 * kDeg, pack.get());
  const Vec3 across = r.along.cross(r.up).normalized();
  double worstAlong = 0, worstAcross = 0;
  for (double t = -r.length * .5; t <= r.length * .5; t += 10) {
    const double expected = r.altitude[0] + (r.altitude[1] - r.altitude[0]) * (t / r.length + .5);
    for (const double side : {-r.width * .5, 0., r.width * .5}) {
      const Vec3 game = earth.fromEcef(r.centre + r.along * t + across * side);
      const double ground = earth.terrain(game).altitude;
      worstAlong = std::max(worstAlong, std::abs(ground - expected));
      if (side) worstAcross = std::max(worstAcross, std::abs(ground - earth.terrain(earth.fromEcef(r.centre + r.along * t)).altitude));
    }
  }
  std::printf("runway at 48.11N 16.57E: %.0f m by %.0f m, %.1f to %.1f m; off its profile by %.3f m, across by %.3f m\n",
              r.length, r.width, r.altitude[0], r.altitude[1], worstAlong, worstAcross);
  check(r.length > 3000 && worstAlong < .01 && worstAcross < .01, "a level, straight runway");
}

// An aircraft stands and taxis on that runway.
void taxi() {
  const EarthRunway& r = nearestRunway(48.1103, 16.5697);
  const EarthWorld earth(48.11 * kDeg, 16.57 * kDeg, pack.get());
  useWorld(earth);
  for (const auto& d : aircraftDefinitions()) {
    Vec3 position = earth.fromEcef(r.centre - r.along * (r.length * .5 - 300));
    position = earth.atAltitude(position, earth.terrain(position).altitude + d.flight.gear_nose.z - .15);
    const Vec3 along = earth.toLocal(position, earth.fromEcef(r.centre + r.along) - earth.fromEcef(r.centre));
    State s;
    s.pos_ned = position;
    s.att = earth.fromLocal(position, quatFromEuler(0, 0, std::atan2(along.y, along.x)));
    Simulator sim(d.flight);
    sim.setState(s);
    Controls c;
    c.brake01 = 1;
    sim.setControls(c);
    advance(sim, 120 * 15);
    const double parked = sim.instruments().tas;
    c.brake01 = 0;
    c.throttle[0] = c.throttle[1] = .35;
    double bounce = 0, height = 0;
    run(sim, 120 * 25, [&](int, const Simulator&) { return c; }, [&](int, const Simulator& a) {
      bounce = std::max(bounce, std::abs(a.instruments().vs));
      height = std::max(height, a.instruments().agl);
    });
    const double speed = sim.instruments().tas;
    std::printf("%-8s on the Vienna runway: parked %.3f m/s, taxied to %.1f m/s, bounce %.3f m/s, height %.2f m\n",
                std::string(d.key).c_str(), parked, speed, bounce, height);
    check(parked < .05 && speed > 3 && bounce < .6 && height < 8 && !aircraftCrashed(sim.state()), "stands and taxis");
  }
}

// An aircraft that settles onto the sea far from the anchor goes into the water.
void ditch() {
  const EarthWorld earth(46 * kDeg, 8 * kDeg, pack.get());
  useWorld(earth);
  const auto& d = aircraftDefinitions()[0];
  State s;
  s.pos_ned = earth.position({40 * kDeg, -30 * kDeg, 3});
  s.vel_ned = earth.fromLocal(s.pos_ned, {60, 0, 2});
  s.att = earth.fromLocal(s.pos_ned, quatFromEuler(0, .05, 0));
  Simulator sim(d.flight);
  sim.setState(s);
  Controls c;
  c.gear01 = 0;
  sim.setControls(c);
  advance(sim, 120 * 10);
  std::printf("ditched at 40N 30W: water %d, speed %.1f m/s, altitude %.2f m\n", sim.groundImpact().water ? 1 : 0,
              sim.instruments().tas, sim.instruments().alt_msl);
  check(sim.groundImpact().water && sim.instruments().tas < 30, "the sea takes it");
}

// Two loads of the pack, read in different orders, give the same ground.
void determinism() {
  const EarthTerrain other{std::filesystem::path(packPath)};
  const EarthWorld a(0, 0, pack.get()), b(0, 0, &other);
  double checksum = 0;
  for (int i = 0; i < 4000; ++i) {
    const Geodetic g{(-80 + 160 * ((i * 7919) % 4000) / 3999.) * kDeg, (-180 + 360 * ((i * 104729) % 4000) / 4000.) * kDeg, 0};
    const int j = 3999 - i;
    const Geodetic h{(-80 + 160 * ((j * 7919) % 4000) / 3999.) * kDeg, (-180 + 360 * ((j * 104729) % 4000) / 4000.) * kDeg, 0};
    const double first = a.terrain(a.position(g)).altitude;
    b.terrain(b.position(h));  // the other copy is read in another order
    check(std::bit_cast<std::uint64_t>(first) == std::bit_cast<std::uint64_t>(b.terrain(b.position(g)).altitude),
          "same ground from either copy");
    checksum += first;
  }
  std::printf("4000 points over the planet agree bit for bit (sum %.3f m)\n", checksum);
}

// A physics step over the Alps, with the real terrain.
void speed() {
  const EarthWorld earth(46 * kDeg, 8 * kDeg, pack.get());
  useWorld(earth);
  TrimRequest request;
  request.tas = 200;
  request.altitude = 5000;
  const auto trim = solveTrim(a320Config(), request);
  Simulator sim(a320Config());
  sim.setState(trim.state);
  sim.setControls(trim.controls);
  const auto start = std::chrono::steady_clock::now();
  advance(sim, 12000);
  const double each = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 12000;
  std::printf("A320 over the Alps: %.1f us a step\n", each);
  check(each < 400, "fast enough for a server");
}
// GLO-90 detail over the western Alps (region packs beside the pack).
void detail() {
  EarthTerrain fine{std::filesystem::path(packPath)};
  const Theatre& alps = *findTheatre("alps");
  const auto wanted = theatreRegions(alps);
  check(wanted == std::vector<std::string>{"N40E000", "N40E010", "N50E000", "N50E010"}, "the Alps' regions");
  const auto loaded = loadEarthDetail(fine, packPath, &alps);
  check(loaded == wanted, "every region of the theatre is loaded");
  check(fine.contentHash() != pack->contentHash(), "the regions change the content hash");
  const EarthWorld base(46 * kDeg, 7 * kDeg, pack.get()), earth(46 * kDeg, 7 * kDeg, &fine);
  // Peaks no longer averaged down by 460 m cells.
  const auto summit = [&](const EarthWorld& world, double lat, double lon) {
    double best = -1e9;
    for (double dy = -.01; dy <= .01; dy += .0005)
      for (double dx = -.014; dx <= .014; dx += .0007) best = std::max(best, groundAt(world, lat + dy, lon + dx));
    return best;
  };
  const double blanc = summit(earth, 45.8326, 6.8652), blancBase = summit(base, 45.8326, 6.8652);
  const double matterhorn = summit(earth, 45.9763, 7.6586), matterhornBase = summit(base, 45.9763, 7.6586);
  std::printf("Mont Blanc %.0f m (base %.0f m, true 4,806 m); Matterhorn %.0f m (base %.0f m, true 4,478 m)\n", blanc, blancBase,
              matterhorn, matterhornBase);
  check(blanc > 4700 && blanc < 4850 && blanc > blancBase + 50, "Mont Blanc stands near its height");
  // GLO-90's 90 m cells blunt the Matterhorn's point to 4,316 m.
  check(matterhorn > 4230 && matterhorn < 4480 && matterhorn > matterhornBase + 200, "the Matterhorn stands up");
  // Lake Geneva keeps its water above its bed.
  check(groundAt(earth, 46.45, 6.55) < waterAt(earth, 46.45, 6.55) - 1, "Lake Geneva");
  // No cliffs: a metre at a time across tile and region edges, and out of the
  // detail into land no region covers (east of 20E for this theatre).
  const double metre = 1 / 111132.;
  const auto walk = [&](const char* name, double lat0, double lon0, double lat1, double lon1, double limit) {
    const double steps = std::hypot(lat1 - lat0, (lon1 - lon0) * std::cos(lat0 * kDeg)) / metre;
    double previous = groundAt(earth, lat0, lon0), worst = 0;
    for (int k = 1; k <= int(steps); ++k) {
      const double t = k / steps, ground = groundAt(earth, lat0 + (lat1 - lat0) * t, lon0 + (lon1 - lon0) * t);
      worst = std::max(worst, std::abs(ground - previous));
      previous = ground;
    }
    std::printf("  %-40s %7.0f one-metre steps, steepest %.2f m\n", name, steps, worst);
    check(worst < limit, name);
  };
  walk("43N to 47N along 10E (a region edge)", 43, 10, 47, 10, 2.5);
  walk("46.5N 19.4E to 20.6E, out of the detail", 46.5, 19.4, 46.5, 20.6, 2.5);
  walk("49.4N to 50.6N along 8.3E (a region edge)", 49.4, 8.3, 50.6, 8.3, 2.5);
  walk("40.4N to 39.6N along 15.9E, out into hills", 40.4, 15.9, 39.6, 15.9, 2.5);
  // A physics step over the detail.
  {
    useWorld(earth);
    TrimRequest request;
    request.tas = 200;
    request.altitude = 5000;
    const auto trim = solveTrim(a320Config(), request);
    Simulator sim(a320Config());
    sim.setState(trim.state);
    sim.setControls(trim.controls);
    const auto start = std::chrono::steady_clock::now();
    advance(sim, 6000);
    const double each = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 6000;
    std::printf("A320 over the Alps' detail: %.1f us a step\n", each);
    check(each < 400, "fast enough for a server");
  }
  // Zurich's runway is still level.
  const EarthRunway& r = nearestRunway(47.4581, 8.5481);
  const EarthWorld zurich(47.46 * kDeg, 8.55 * kDeg, &fine);
  double worst = 0;
  for (double t = -r.length * .5; t <= r.length * .5; t += 10) {
    const double expected = r.altitude[0] + (r.altitude[1] - r.altitude[0]) * (t / r.length + .5);
    worst = std::max(worst, std::abs(zurich.terrain(zurich.fromEcef(r.centre + r.along * t)).altitude - expected));
  }
  std::printf("Zurich runway off its profile by %.3f m\n", worst);
  check(worst < .01, "a level runway in the detail");
  // Two loads agree bit for bit.
  EarthTerrain again{std::filesystem::path(packPath)};
  loadEarthDetail(again, packPath, &alps);
  check(again.contentHash() == fine.contentHash(), "the same hash from another load");
  const EarthWorld other(46 * kDeg, 7 * kDeg, &again);
  for (int i = 0; i < 2000; ++i) {
    const double lat = 43 + 6 * ((i * 7919) % 2000) / 1999., lon = 4 + 12 * ((i * 104729) % 2000) / 1999.;
    check(std::bit_cast<std::uint64_t>(groundAt(earth, lat, lon)) == std::bit_cast<std::uint64_t>(groundAt(other, lat, lon)),
          "same ground from either load");
  }
}

// Every theatre's airports have a runway in the pack to start on, and lie
// within reach of the theatre's anchor.
void theatreStarts() {
  check(ofs::theatres().size() == 3, "three theatres");
  for (const auto& theatre : ofs::theatres()) {
    check(findTheatre(theatre.id) == &theatre && !theatre.airports.empty(), "a theatre is found by its id");
    const Vec3 anchor = ecefFromGeodetic({theatre.latitude * kDeg, theatre.longitude * kDeg, 0});
    for (const auto& airport : theatre.airports) {
      const EarthRunway* runway = theatreRunway(*pack, airport);
      check(runway && runway->length > 1500, "every theatre airport has a runway");
      const RunwayStart start = runwayStart(*runway);
      const double away = (start.point - anchor).norm();
      std::printf("  %-9s %-14s %5.0f m runway, %4.0f km from the anchor\n", std::string(theatre.id).c_str(),
                  std::string(airport.name).c_str(), runway->length, away / 1000);
      check(away < 450000, "within the theatre");
      // The start lies on the runway, 300 m in from its end.
      check(std::abs((start.point - runway->centre).dot(runway->along) + runway->length * .5 - 300) < 1e-6, "300 m in");
      // And an aircraft put there stands on it, level.
      const EarthWorld earth(theatre.latitude * kDeg, theatre.longitude * kDeg, pack.get());
      const Vec3 at = earth.fromEcef(start.point);
      const auto ground = earth.terrain(at);
      check(ground.normal.dot(earth.up(at)) > .999, "level ground at the start");
    }
  }
  check(!findTheatre("moon"), "no such theatre");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string suite = argc > 1 ? argv[1] : "geodesy";
    if (suite == "geodesy") geodesy();
    else if (suite == "frame") frame();
    else if (suite == "near") near();
    else if (suite == "cruise") cruise();
    else if (suite == "drop") drop();
    else if (suite == "parked") parked();
    else if (suite == "sight") sight();
    else if (suite == "instruments") instruments();
    else if (suite == "places" || suite == "seams" || suite == "runway" || suite == "taxi" || suite == "ditch" ||
             suite == "determinism" || suite == "speed" || suite == "theatres" || suite == "detail") {
      packPath = argc > 2 ? argv[2] : "";
      if (!std::filesystem::is_regular_file(packPath)) {
        std::printf("[NOT RUN] no Earth terrain pack at '%s'; build it with scripts/build_earth_terrain.py\n", packPath.c_str());
        return 77;
      }
      pack = std::make_unique<EarthTerrain>(std::filesystem::path(packPath));
      if (suite == "detail")
        for (const auto& region : theatreRegions(*findTheatre("alps")))
          if (!std::filesystem::is_regular_file(EarthTerrain::detailPath(packPath, region))) {
            std::printf("[NOT RUN] no detail region %s; build it with scripts/build_earth_terrain.py detail --theatres alps\n",
                        region.c_str());
            return 77;
          }
      if (suite == "places") places();
      else if (suite == "seams") seams();
      else if (suite == "runway") runway();
      else if (suite == "taxi") taxi();
      else if (suite == "ditch") ditch();
      else if (suite == "determinism") determinism();
      else if (suite == "theatres") theatreStarts();
      else if (suite == "detail") detail();
      else speed();
    }
    else throw std::runtime_error("unknown suite " + suite);
    std::printf("earth.%s passed\n", suite.c_str());
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "earth test failed: %s\n", e.what());
    return 1;
  }
}
