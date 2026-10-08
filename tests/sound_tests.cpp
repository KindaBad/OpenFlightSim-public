// The sound engine without a sound card: scenes are played into memory and
// measured. `render <directory>` writes the same scenes as WAV files, to be
// listened to.
#include "sound.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ofs;
using namespace ofs::client;

namespace {

void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

// A flight heard through the director, one sixtieth of a second at a time.
struct Session {
  SoundMixer mixer;
  SoundDirector director{mixer};
  SoundFrame frame;
  State own;
  std::vector<State> otherStates;
  std::vector<SoundFrame::Aircraft> others;
  std::vector<SoundFrame::Missile> missiles;
  std::vector<SoundFrame::Threat> threats;
  std::vector<float> samples;  // interleaved stereo
  double time{};

  Session() {
    frame.dt = 1. / 60;
    frame.attached = true;
    frame.type = AircraftType::Typhoon;
    frame.onWheels = false;
    frame.gearCommand = 0;
    own.pos_ned = {0, 0, -1500};
    own.vel_ned = {220, 0, 0};
    own.n1[0] = own.n1[1] = .6;
    otherStates.reserve(8);
  }
  // Puts the camera where a chase view has it: behind and a little above.
  void chase(double behind = 28) {
    frame.attached = true;
    frame.cockpit = false;
    frame.eye = own.pos_ned + own.att.rotate({-behind, 0, -5});
    frame.forward = own.att.rotate({1, 0, 0});
    frame.up = own.att.rotate({0, 0, -1});
  }
  void cockpit() {
    chase(-4);
    frame.cockpit = true;
  }
  void step() {
    own.pos_ned += own.vel_ned * frame.dt;
    if (frame.attached) frame.cockpit ? cockpit() : chase();
    frame.own = &own;
    const double speed = own.vel_ned.norm();
    frame.instruments.tas = speed;
    frame.instruments.mach = speed / 340;
    frame.qbar = .5 * 1.05 * speed * speed;
    others.clear();
    for (std::size_t i = 0; i < otherStates.size(); ++i) {
      otherStates[i].pos_ned += otherStates[i].vel_ned * frame.dt;
      others.push_back({&otherStates[i], AircraftType::Typhoon, 100 + i});
    }
    for (auto& missile : missiles) { missile.position += missile.velocity * frame.dt; missile.age += frame.dt; }
    frame.others = others;
    frame.missiles = missiles;
    frame.threats = threats;
    director.update(frame);
    const std::size_t count = kSoundRate / 60;
    samples.resize(samples.size() + count * 2);
    mixer.render(samples.data() + samples.size() - count * 2, count);
    time += frame.dt;
  }
  void run(double seconds, const std::function<void(double)>& each = {}) {
    const double end = time + seconds;
    while (time < end - 1e-9) {
      if (each) each(time);
      step();
    }
  }
  // Root-mean-square level of one channel (or both) between two times.
  double level(double from, double to, int channel = -1) const {
    const std::size_t first = std::size_t(from * kSoundRate), last = std::min(samples.size() / 2, std::size_t(to * kSoundRate));
    double sum = 0;
    std::size_t counted = 0;
    for (std::size_t i = first; i < last; ++i)
      for (int c = 0; c < 2; ++c)
        if (channel < 0 || channel == c) { sum += double(samples[i * 2 + c]) * samples[i * 2 + c]; ++counted; }
    return counted ? std::sqrt(sum / counted) : 0;
  }
  // How bright the sound is: the share of its energy that changes from sample to sample.
  double brightness(double from, double to) const {
    const std::size_t first = std::size_t(from * kSoundRate) + 1, last = std::min(samples.size() / 2, std::size_t(to * kSoundRate));
    double change = 0, energy = 1e-12;
    for (std::size_t i = first; i < last; ++i) {
      const double delta = samples[i * 2] - samples[i * 2 - 2];
      change += delta * delta; energy += double(samples[i * 2]) * samples[i * 2];
    }
    return std::sqrt(change / energy);
  }
  double peak() const {
    double value = 0;
    for (float sample : samples) value = std::max(value, double(std::abs(sample)));
    return value;
  }
  void write(const std::filesystem::path& path) const {
    std::ofstream file(path, std::ios::binary);
    const auto u32 = [&](std::uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&](std::uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    const std::uint32_t bytes = std::uint32_t(samples.size() * 2);
    file.write("RIFF", 4); u32(36 + bytes); file.write("WAVEfmt ", 8); u32(16); u16(1); u16(2);
    u32(kSoundRate); u32(kSoundRate * 4); u16(4); u16(16); file.write("data", 4); u32(bytes);
    for (float sample : samples) u16(std::uint16_t(std::int16_t(std::lround(std::clamp(sample, -1.f, 1.f) * 32767))));
  }
};

void throttle(State& state, double power, double reheat) {
  state.n1[0] = state.n1[1] = power;
  state.afterburner[0] = state.afterburner[1] = reheat;
}

// ---- Scenes, shared by the checks and by `render` ----

// Idle, up to full power, into reheat and back, from behind.
void engineSweep(Session& s) {
  s.run(14, [&](double t) {
    const double power = t < 2 ? 0 : t < 6 ? (t - 2) / 4 : t < 11 ? 1 : std::max(0., 1 - (t - 11) / 2);
    const double reheat = t > 7 && t < 10 ? std::min(1., (t - 7) / .5) : 0;
    throttle(s.own, power, reheat);
  });
}

// A fighter in reheat passing a camera that is standing still.
void flyby(Session& s) {
  s.frame.attached = false;
  s.own.pos_ned = {-1500, 60, -200};
  s.own.vel_ned = {250, 0, 0};
  throttle(s.own, 1, 1);
  s.frame.eye = {0, 0, -200};
  s.frame.forward = {0, 1, 0};
  s.frame.up = {0, 0, -1};
  s.run(12);
}

void gunBurst(Session& s) {
  throttle(s.own, .7, 0);
  double next = .5;
  s.run(4.5, [&](double t) {
    // A second's burst from the pilot's own gun, then one from 900 m away.
    while (t >= next && next < 1.5) {
      s.director.at(SoundKind::Gun, s.own.pos_ned + Vec3{6, 0, 0}, 1, true);
      next += 60. / 1700;
    }
    if (next >= 1.5 && next < 2.2) next = 2.2;
    while (t >= next && next >= 2.2 && next < 3.2) {
      s.director.at(SoundKind::Gun, s.own.pos_ned + Vec3{600, 650, 0});
      next += 60. / 1700;
    }
  });
}

void missileShot(Session& s) {
  throttle(s.own, .8, 0);
  s.run(6, [&](double t) {
    if (t >= 1 && s.missiles.empty())
      s.missiles.push_back({7, s.own.pos_ned + Vec3{2, 3, 1}, s.own.vel_ned + Vec3{40, 0, 0}, true, true, 0});
    if (!s.missiles.empty()) s.missiles[0].velocity.x = std::min(900., s.missiles[0].velocity.x + 300 * s.frame.dt);
  });
}

// One nearby, one that takes four seconds to arrive, and a warhead.
void explosions(Session& s) {
  throttle(s.own, .5, 0);
  bool fired[3] = {};
  s.run(13, [&](double t) {
    if (t >= .5 && !fired[0]) { fired[0] = true; s.director.at(SoundKind::Explosion, s.own.pos_ned + Vec3{120, 40, 0}); }
    if (t >= 4 && !fired[1]) { fired[1] = true; s.director.at(SoundKind::Explosion, s.own.pos_ned + Vec3{1500, -300, 0}); }
    if (t >= 10 && !fired[2]) { fired[2] = true; s.director.at(SoundKind::Detonation, s.own.pos_ned + Vec3{200, -60, 0}); }
  });
}

// What the flight deck says: the stall horn, a missile closing, and a heat seeker finding its target.
void warnings(Session& s) {
  s.cockpit();
  throttle(s.own, .5, 0);
  s.run(16, [&](double t) {
    s.frame.instruments.stall_warn = t > 1 && t < 3;
    s.frame.instruments.alpha_deg = t > 1 && t < 3 ? 22 : 3;
    s.threats.clear();
    if (t > 4 && t < 9) s.threats.push_back({s.own.pos_ned + Vec3{-(9 - t) * 800, 0, 0}, s.own.vel_ned + Vec3{800, 0, 0}, false});
    s.frame.missileSelected = t > 10;
    s.frame.weapon = weapons::WeaponType::Infrared;
    s.frame.seekerTracking = t > 11;
    s.frame.lockProgress = std::clamp((t - 11) / 2, 0., 1.);
    s.frame.seekerReady = t > 13;
  });
}

// Gear down on the approach, the wheels touching, and the roll-out.
void landing(Session& s) {
  s.own.pos_ned.z = -30;
  s.own.vel_ned = {75, 0, 2};
  throttle(s.own, .25, 0);
  s.run(14, [&](double t) {
    s.frame.gearCommand = t > 1 ? 1 : 0;
    if (t > 7) {
      s.frame.onWheels = true;
      s.own.vel_ned.z = 0;
      s.own.vel_ned.x = std::max(0., s.own.vel_ned.x - 9 * s.frame.dt);
      throttle(s.own, 0, 0);
    } else if (t > 6.9) {
      s.own.vel_ned.z = 2.2;
    }
  });
}

// Every one-off sound in turn, as heard beside it.
void catalogue(Session& s) {
  s.frame.master = 1;
  s.own.engine_health[0] = s.own.engine_health[1] = 0;  // in silence
  int next = 0;
  s.run(1.6 * double(SoundKind::Count), [&](double t) {
    if (next < int(SoundKind::Count) && t >= 1.6 * next + .1) s.director.cue(SoundKind(next++));
  });
}

// A fight: everything at once, which is what the limiter is for.
void fight(Session& s) {
  throttle(s.own, 1, 1);
  for (int i = 0; i < 6; ++i) {
    State other;
    other.pos_ned = s.own.pos_ned + Vec3{300. + 150 * i, -400. + 160 * i, -30. * i};
    other.vel_ned = {-180, 40. * i, 0};
    throttle(other, 1, i % 2);
    s.otherStates.push_back(other);
  }
  double shot = 0;
  int event = 0;
  s.run(8, [&](double t) {
    while (t >= shot) {
      s.director.at(SoundKind::Gun, s.own.pos_ned + Vec3{6, 0, 0}, 1, true);
      s.director.at(SoundKind::Gun, s.otherStates[0].pos_ned);
      shot += 60. / 1700;
    }
    if (t >= event * .4) {
      const Vec3 where = s.own.pos_ned + Vec3{80. + 40 * event, 30. * (event % 3 - 1), 0};
      s.director.at(event % 3 == 0 ? SoundKind::Explosion : event % 3 == 1 ? SoundKind::Detonation : SoundKind::Flare, where);
      s.director.at(SoundKind::HitTaken, s.own.pos_ned, 1, true);
      s.director.cue(SoundKind::HitMarker);
      ++event;
    }
    s.frame.instruments.stall_warn = true;
    s.threats.assign(1, {s.own.pos_ned + Vec3{-900, 0, 0}, s.own.vel_ned + Vec3{700, 0, 0}, false});
  });
}

struct Scene { const char* name; void (*play)(Session&); bool cockpit; AircraftType type; };
const Scene kScenes[] = {
    {"engine_typhoon", engineSweep, false, AircraftType::Typhoon},
    {"engine_typhoon_cockpit", engineSweep, true, AircraftType::Typhoon},
    {"engine_a320", engineSweep, false, AircraftType::A320},
    {"engine_sr71", engineSweep, false, AircraftType::SR71},
    {"flyby", flyby, false, AircraftType::Typhoon},
    {"gun", gunBurst, false, AircraftType::Typhoon},
    {"gun_cockpit", gunBurst, true, AircraftType::Typhoon},
    {"missile", missileShot, false, AircraftType::Typhoon},
    {"explosions", explosions, false, AircraftType::Typhoon},
    {"warnings", warnings, true, AircraftType::Typhoon},
    {"landing", landing, false, AircraftType::A320},
    {"catalogue", catalogue, false, AircraftType::Typhoon},
    {"fight", fight, false, AircraftType::Typhoon},
};

void play(const Scene& scene, Session& s) {
  s.frame.type = scene.type;
  if (scene.cockpit) s.cockpit();
  scene.play(s);
}

// ---- Checks ----

void checks() {
  // Every scene stays finite and inside the range a sound card accepts.
  for (const auto& scene : kScenes) {
    Session s;
    play(scene, s);
    for (float sample : s.samples) check(std::isfinite(sample), std::string(scene.name) + " produced a non-finite sample");
    check(s.peak() <= 1.0, std::string(scene.name) + " clipped");
    check(s.level(0, s.time) > 1e-4, std::string(scene.name) + " was silent");
    std::printf("%-24s peak %.3f  level %.4f\n", scene.name, s.peak(), s.level(0, s.time));
  }
  {
    // Power is heard: reheat is louder than full power, and that than idle.
    Session s;
    s.frame.volume[std::size_t(SoundBus::Airframe)] = 0;
    engineSweep(s);
    const double idle = s.level(1, 2), full = s.level(6.2, 7), reheat = s.level(8, 10);
    check(full > idle * 2, "full power is not clearly louder than idle");
    check(reheat > full * 1.25, "reheat is not louder than full power");
    check(idle > .004, "an idling engine cannot be heard");
    check(s.peak() < .95 * s.frame.master, "one aircraft's engines reach the limiter");
  }
  {
    // A pass: the pitch falls, the sound crosses from one ear to the other,
    // and it is loudest when the aircraft is closest.
    Session s;
    flyby(s);
    // The camera looks east, so it comes up from the south on the right and leaves on the left.
    check(s.level(4.5, 5.5, 1) > s.level(4.5, 5.5, 0) * 1.3, "an approaching aircraft is not heard on its own side");
    check(s.level(6.5, 7.5, 0) > s.level(6.5, 7.5, 1) * 1.3, "a departing aircraft is not heard on its own side");
    check(s.level(5.5, 6.5) > s.level(1, 2) * 4, "a pass is not loudest at its closest");
    Session approach, depart;
    approach.frame.attached = depart.frame.attached = false;
    approach.own.pos_ned = {-800, 0, -200}; depart.own.pos_ned = {800, 0, -200};
    approach.frame.eye = depart.frame.eye = {0, 0, -200};
    approach.step(); depart.step();
    approach.step(); depart.step();
    check(approach.director.scene().jets[0].doppler > 1.5, "an approaching aircraft is not raised in pitch");
    check(depart.director.scene().jets[0].doppler < .7, "a departing aircraft is not lowered in pitch");
    check(depart.director.scene().jets[0].rear > .9 && approach.director.scene().jets[0].rear < .1,
          "the exhaust is not heard from behind");
  }
  {
    // Distance: a far explosion arrives late, quieter and duller.
    const auto explode = [](Session& s, double distance, double seconds) {
      s.own.engine_health[0] = s.own.engine_health[1] = 0;
      s.own.vel_ned = {};
      s.frame.master = 1;
      s.frame.volume[std::size_t(SoundBus::Airframe)] = 0;
      s.run(.2);
      s.director.at(SoundKind::Explosion, s.own.pos_ned + Vec3{distance, 0, 0});
      s.run(seconds);
    };
    Session close, far;
    explode(close, 100, 1.5);
    explode(far, 1850, 6.5);  // five seconds away
    const double near = close.level(.2, 1.2), early = far.level(.3, 5), late = far.level(5.2, 6.2);
    check(early < near * .001, "a distant explosion is heard before its sound can arrive");
    check(late > near * .03 && late < near * .5, "a distant explosion is not heard quieter, in its time");
    check(far.brightness(5.2, 6.2) < close.brightness(.2, 1.2) * .8, "distance does not dull an explosion");
  }
  {
    // The settings: the master silences everything, a held flight only the world.
    Session s;
    s.frame.master = 0;
    s.run(.5);
    s.director.cue(SoundKind::KillChime);
    s.run(.5);
    check(s.level(.25, 1) < 1e-6, "a zero master volume is not silent");
    Session held;
    held.run(.5);
    held.frame.held = true;
    held.run(.5);
    check(held.level(.8, 1) < 1e-4, "a paused flight still sounds");
    held.director.cue(SoundKind::UiClick);
    held.run(.2);
    check(held.level(1, 1.2) > 1e-3, "the interface is silent while paused");
    Session unfocused;
    unfocused.frame.focused = false;
    unfocused.run(.5);
    check(unfocused.level(.3, .5) < 1e-6, "a window in the background still sounds");
    unfocused.frame.muteUnfocused = false;
    unfocused.run(.5);
    check(unfocused.level(.8, 1) > 1e-3, "sound in the background cannot be turned on");
    Session quiet;
    quiet.frame.volume[std::size_t(SoundBus::Engines)] = 0;
    quiet.frame.volume[std::size_t(SoundBus::Airframe)] = 0;
    quiet.run(.5);
    check(quiet.level(.3, .5) < 1e-5, "the engine and airframe volumes do not silence them");
  }
  {
    // What ends an aircraft is told apart by ear, and a missile is not quiet.
    const auto heard = [](SoundKind kind, double from, double to) {
      SoundMixer mixer;
      SoundScene scene;
      scene.master = 1;
      mixer.setScene(scene);
      std::vector<float> samples(std::size_t(kSoundRate) * 2 * 4);
      mixer.render(samples.data(), kSoundRate / 4);
      mixer.play({kind});
      mixer.render(samples.data(), kSoundRate * 4);
      double sum = 0;
      const std::size_t first = std::size_t(from * kSoundRate), last = std::size_t(to * kSoundRate);
      for (std::size_t i = first; i < last; ++i) sum += double(samples[i * 2]) * samples[i * 2];
      return std::sqrt(sum / double(last - first));
    };
    check(heard(SoundKind::MissileLaunch, 0, .6) > heard(SoundKind::Gun, 0, .6) * 2.5, "a missile leaving is not clearly louder than a cannon round");
    check(heard(SoundKind::MissileLaunch, .6, 1.4) > .02, "a missile's motor is not heard going away");
    check(heard(SoundKind::Crash, .15, .6) > heard(SoundKind::Explosion, .15, .6) * .5 && heard(SoundKind::Crash, 1, 2) > .01,
          "a crash has no tearing of the airframe after the blow");
    check(heard(SoundKind::PartBreak, 0, .3) > .03 && heard(SoundKind::PartBreak, 1.5, 2.5) < .004, "a part breaking away is not a short, sharp sound");
    check(heard(SoundKind::Parachute, .25, .5) > heard(SoundKind::Parachute, 0, .2), "a parachute does not end in the crack of it filling");
    // A motor burning close by is heard over the pilot's own engines.
    Session with, without;
    with.frame.volume[std::size_t(SoundBus::Airframe)] = without.frame.volume[std::size_t(SoundBus::Airframe)] = 0;
    with.missiles.push_back({7, with.own.pos_ned + Vec3{60, 12, 0}, {300, 0, 0}, true, true, 1});
    with.run(1); without.run(1);
    check(with.level(.4, 1) > without.level(.4, 1) * 1.5, "a missile's motor is lost under the engines");
  }
  {
    // The pilot under load: a pulse that only strain brings on, and a world
    // that goes dull with it.
    Session rested, strained, out;
    for (Session* s : {&rested, &strained, &out})
      for (const auto bus : {SoundBus::Engines, SoundBus::Airframe, SoundBus::Weapons}) s->frame.volume[std::size_t(bus)] = 0;
    strained.frame.strain = .9;
    out.frame.strain = 1; out.frame.unconscious = true;
    rested.run(3); strained.run(3); out.run(3);
    check(rested.level(1, 3) < 1e-5, "a rested pilot hears a pulse");
    check(strained.level(1, 3) > .004 && out.level(1, 3) >= strained.level(1, 3), "a pilot losing their sight hears no pulse");
    Session clear, dull;
    dull.frame.strain = 1; dull.frame.unconscious = true;
    for (Session* s : {&clear, &dull}) s->frame.volume[std::size_t(SoundBus::Cockpit)] = 0;
    clear.run(3); dull.run(3);
    check(dull.brightness(2, 3) < clear.brightness(2, 3) * .6, "the world is not dulled for a pilot who has blacked out");
  }
  {
    // Every one-off sound makes itself heard, and none of them clips alone.
    for (int kind = 0; kind < int(SoundKind::Count); ++kind) {
      SoundMixer mixer;
      SoundScene scene;
      scene.master = 1;
      mixer.setScene(scene);
      std::vector<float> samples(kSoundRate * 2);
      mixer.render(samples.data(), kSoundRate / 4);  // the master volume comes up from nothing
      mixer.play({SoundKind(kind)});
      mixer.render(samples.data(), kSoundRate);
      double peak = 0;
      for (float sample : samples) peak = std::max(peak, double(std::abs(sample)));
      check(peak > .05 && peak <= 1, "sound " + std::to_string(kind) + " peaks at " + std::to_string(peak));
    }
    // Far more at once than there are voices for.
    SoundMixer mixer;
    SoundScene scene;
    scene.master = 1;
    mixer.setScene(scene);
    std::vector<float> samples(kSoundRate / 5 * 2);
    for (int round = 0; round < 20; ++round) {
      for (int i = 0; i < 90; ++i) mixer.play({SoundKind(i % int(SoundKind::Count)), 1, float(i % 3 - 1), float(i % 4) * .01f});
      mixer.render(samples.data(), samples.size() / 2);
      for (float sample : samples) check(std::isfinite(sample) && std::abs(sample) <= 1, "a crowd of sounds overflowed");
    }
    check(mixer.activeVoices() <= 64, "more voices than the pool holds");
  }
  {
    // What the director is told is not trusted to be finite.
    Session s;
    s.run(.2);
    const double bad = std::nan("");
    s.own.n1[0] = bad; s.own.afterburner[1] = bad;
    s.frame.instruments.alpha_deg = bad; s.frame.instruments.g_load = bad; s.frame.qbar = bad;
    s.frame.lockProgress = bad; s.frame.health = bad;
    s.director.at(SoundKind::Explosion, {bad, 0, 0});
    s.director.scrape(float(bad));
    s.threats.push_back({{bad, 0, 0}, {}, false});
    s.run(.3);
    s.own.pos_ned.x = bad;
    s.frame.eye.y = bad;
    s.run(.3);
    for (float sample : s.samples) check(std::isfinite(sample), "a non-finite input reached the output");
  }
  {
    // Events are heard once: a gear selection runs the motor and ends in a lock,
    // a touchdown sounds, and a new missile launch is announced.
    Session s;
    landing(s);
    check(s.director.scene().roll >= 0, "roll speed is reported");
    check(s.level(1.2, 2) > s.level(.2, .9) * 1.02, "lowering the gear cannot be heard");
    check(s.level(7, 7.3) > s.level(6.4, 6.8) * 1.5, "a touchdown cannot be heard");
    Session m;
    m.own.engine_health[0] = m.own.engine_health[1] = 0;
    m.own.vel_ned = {};
    m.run(.5);
    m.missiles.push_back({9, m.own.pos_ned + Vec3{3, 2, 0}, {300, 0, 0}, true, true, 0});
    m.run(1.5);
    check(m.level(.55, 1) > m.level(.1, .45) * 5, "a missile launch cannot be heard");
    check(m.level(1.6, 2) < m.level(.55, .9), "a departing missile does not fade");
  }
  std::printf("sound: all checks passed\n");
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc >= 3 && std::string(argv[1]) == "render") {
      // An optional third argument leaves one volume control up and the rest down.
      const std::string only = argc > 3 ? argv[3] : "";
      const char* const buses[] = {"engines", "weapons", "airframe", "cockpit"};
      std::filesystem::create_directories(argv[2]);
      for (const auto& scene : kScenes) {
        Session s;
        for (std::size_t bus = 0; bus < 4 && !only.empty(); ++bus) s.frame.volume[bus] = only == buses[bus] ? 1 : 0;
        play(scene, s);
        s.write(std::filesystem::path(argv[2]) / (std::string(scene.name) + ".wav"));
        std::printf("%-24s %5.1f s  peak %.3f  level %.4f\n", scene.name, s.time, s.peak(), s.level(0, s.time));
      }
      return 0;
    }
    checks();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "sound test failed: %s\n", error.what());
    return 1;
  }
}
