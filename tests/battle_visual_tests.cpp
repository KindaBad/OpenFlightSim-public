// What a fight looks like, without a window: where damage is shown, the parts
// that break away, the effects that weapons and damage produce, and chat.
#include "breakaway.hpp"
#include "chat.hpp"
#include "damage_visuals.hpp"
#include "effects.hpp"
#include "ofs/trim.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace ofs;
using namespace ofs::client;
namespace {
void check(bool value, const char* why) {
  if (!value) throw std::runtime_error(why);
}
State flying(AircraftType type, double speed = 200) {
  State state;
  state.pos_ned = {0, 0, -3000};
  state.vel_ned = {speed, 0, 0};
  state.n1[0] = state.n1[1] = .8;
  (void)type;
  return state;
}
void view() {
  const auto& typhoon = aircraftDefinition(AircraftType::Typhoon);
  State state;
  check(!damageView(state, 100).any(), "an intact aircraft shows no damage");
  applyPartDamage(typhoon.flight, state, DamagePart::LeftWing, 50);
  applyPartDamage(typhoon.flight, state, DamagePart::RightEngine, 100);
  auto damage = damageView(state, 70);
  check(damage.any() && std::abs(damage[DamagePart::LeftWing] - .5f) < 1e-6f && damage[DamagePart::RightWing] == 0 &&
            damage[DamagePart::RightEngine] == 1 && damage[DamagePart::LeftEngine] == 0 &&
            std::abs(damage[DamagePart::Fuselage] - .3f) < 1e-6f,
        "each part shows its own damage; the fuselage shows the hit points lost");
  state = {};
  state.surface_health[5] = .4;  // a hard landing
  check(std::abs(damageView(state, 100)[DamagePart::Fuselage] - .6f) < 1e-6f, "structural damage shows on the fuselage");
  check(damageView(state, -20)[DamagePart::Fuselage] == 1 && damageView(state, 500)[DamagePart::Fuselage] >= .6f,
        "hit points outside their range are clamped");
  // The outer wing goes first, and only once the wing is badly damaged.
  check(wingRemaining(0) > 1 && wingRemaining(.4) > 1 && wingRemaining(.7) < 1 && std::abs(wingRemaining(1) - .08) < 1e-12 &&
            finRemaining(.4) > 1 && std::abs(finRemaining(1) - .1) < 1e-12,
        "nothing is torn off below 40 % damage; a destroyed part is a stub");
  for (double d = 0; d < 1; d += .01)
    check(wingRemaining(d + .01) <= wingRemaining(d) && finRemaining(d + .01) <= finRemaining(d), "more damage never restores structure");
  for (const auto& definition : aircraftDefinitions()) {
    const auto geometry = damageGeometry(definition.type);
    const double tip = std::abs(definition.visual.wingtip[1].y);
    check(geometry.wingRoot > 0 && geometry.wingRoot < geometry.wingTip && std::abs(geometry.wingTip - tip) < tip * .06 &&
              geometry.wingAft < geometry.wingFore && geometry.finBase > 0 && geometry.finBase < geometry.finTop,
          "damage geometry describes the model");
    // The wingtips and exhausts the effects already use lie inside it.
    check(definition.visual.wingtip[1].x > geometry.wingAft && definition.visual.wingtip[1].x < geometry.wingFore,
          "the wingtip lies within the wing");
    const Vec3 left = wingPoint(definition.type, false, 1), right = wingPoint(definition.type, true, 1);
    check((left - definition.visual.wingtip[0]).norm() < 1e-9 && (right - definition.visual.wingtip[1]).norm() < 1e-9 &&
              wingPoint(definition.type, true, 0).y == geometry.wingRoot,
          "wing points run from the stub to the tip");
    check(damagePoint(definition.type, DamagePart::LeftWing).y < 0 && damagePoint(definition.type, DamagePart::RightWing).y > 0 &&
              damagePoint(definition.type, DamagePart::Tail).z < 0 && damagePoint(definition.type, DamagePart::Tail).x < 0 &&
              (damagePoint(definition.type, DamagePart::RightEngine) - definition.visual.exhaust[1]).norm() == 0,
          "each part's smoke comes from that part");
  }
  std::puts("battle view: damage per part, tear-off thresholds, model geometry PASS");
}
void breakaway() {
  const auto type = AircraftType::Typhoon;
  const auto& definition = aircraftDefinition(type);
  Breakaways pieces;
  State state = flying(type);
  const std::vector<AssetMatrix> pose;
  // An aircraft seen for the first time, already damaged, sheds nothing.
  applyPartDamage(definition.flight, state, DamagePart::LeftWing, 60);
  check(pieces.observe(7, type, state, damageView(state, 100), pose, .5f) == 0 && pieces.pieces().empty(),
        "first sight releases nothing");
  // A little more damage is a sliver, not a piece.
  applyPartDamage(definition.flight, state, DamagePart::LeftWing, 1);
  check(pieces.observe(7, type, state, damageView(state, 100), pose, .5f) == 0, "slivers are not drawn");
  // The wing destroyed: its outer part leaves as one piece.
  const double before = wingRemaining(damageView(state, 100)[DamagePart::LeftWing]);
  applyPartDamage(definition.flight, state, DamagePart::LeftWing, 100);
  check(pieces.observe(7, type, state, damageView(state, 100), pose, .5f) == 1 && pieces.pieces().size() == 1,
        "a destroyed wing leaves as a piece");
  const auto piece = pieces.pieces().front();
  check(piece.part == DamagePart::LeftWing && !piece.wreck() && std::abs(piece.inner - .08f) < 1e-6f &&
            std::abs(piece.outer - float(before)) < 1e-6f && piece.pivot.y < 0,
        "the piece is the slab between the old break and the new");
  check((piece.velocity - state.vel_ned).norm() < 15 && (piece.velocity - state.vel_ned).norm() > 1 &&
            (piece.position - state.pos_ned).norm() < definition.visual.radius && piece.spin.norm() > 1,
        "it leaves at the aircraft's speed, with a push and a tumble");
  // Seen again unchanged, nothing more leaves; the other wing and the fin are separate.
  check(pieces.observe(7, type, state, damageView(state, 100), pose, .5f) == 0, "no piece twice");
  applyPartDamage(definition.flight, state, DamagePart::Tail, 200);
  check(pieces.observe(7, type, state, damageView(state, 100), pose, .5f) == 1 && pieces.pieces().back().part == DamagePart::Tail &&
            pieces.pieces().back().pivot.z < 0,
        "the fin leaves on its own");
  // It slows in the air, falls and comes to rest on the ground.
  pieces.endFrame();
  const double speed = pieces.pieces().front().velocity.norm();
  for (unsigned i = 0; i < 60 * 4; ++i) pieces.update(1. / 60);
  const auto falling = pieces.pieces().front();
  check(falling.velocity.norm() < speed * .6 && falling.velocity.z > 5 && falling.position.z > piece.position.z &&
            std::abs(falling.attitude.w * falling.attitude.w + falling.attitude.x * falling.attitude.x +
                     falling.attitude.y * falling.attitude.y + falling.attitude.z * falling.attitude.z - 1) < 1e-9,
        "a piece slows, falls and keeps a unit attitude");
  for (unsigned i = 0; i < 60 * 20 && !pieces.pieces().empty(); ++i) pieces.update(1. / 60);
  check(pieces.pieces().empty(), "pieces expire");
  // A piece released low reaches the ground and stops there.
  Breakaways low;
  State near = flying(type, 120);
  near.pos_ned.z = groundHeightNed(0, 0) - 40;
  low.observe(1, type, near, damageView(near, 100), pose, .1f);
  applyPartDamage(definition.flight, near, DamagePart::RightWing, 100);
  check(low.observe(1, type, near, damageView(near, 100), pose, .1f) == 1, "low piece released");
  bool struck = false;
  for (unsigned i = 0; i < 60 * 8; ++i) {
    low.update(1. / 60);
    if (!low.pieces().empty()) struck = struck || low.pieces().front().struck;
  }
  check(struck && low.pieces().front().grounded && low.pieces().front().velocity.norm() == 0 &&
            std::abs(low.pieces().front().position.z -
                     groundHeightNed(low.pieces().front().position.x, low.pieces().front().position.y)) < 1e-6,
        "a piece lands and stays where it fell");
  // An aircraft that blows up throws off what it still had, and its wreck falls burning.
  Breakaways blast;
  State whole = flying(type);
  check(blast.shatter(3, type, whole, pose, .2f) == 4 && blast.pieces().size() == 4, "an intact aircraft breaks into wings, fin and wreck");
  check(blast.pieces().back().wreck() && (blast.pieces().back().velocity - whole.vel_ned).norm() == 0,
        "the wreck carries on along the flight path");
  Breakaways partial;
  partial.observe(3, type, state, damageView(state, 100), pose, .2f);  // left wing and fin already gone
  check(partial.shatter(3, type, state, pose, .2f) == 2 && partial.pieces().front().part == DamagePart::RightWing &&
            partial.pieces().back().wreck(),
        "parts already lost are not thrown off again");
  // After it has gone, the same identity starts whole: a respawn sheds nothing.
  check(partial.observe(3, type, whole, damageView(whole, 100), pose, .2f) == 0, "respawn sheds nothing");
  // An aircraft that left and another type under its identity are both taken as they are.
  Breakaways roster;
  roster.observe(9, type, whole, damageView(whole, 100), pose, 0);
  roster.endFrame();
  roster.endFrame();
  check(roster.observe(9, type, state, damageView(state, 100), pose, 0) == 0, "a forgotten aircraft is seen afresh");
  check(roster.observe(9, AircraftType::A320, whole, damageView(whole, 100), pose, 0) == 0 &&
            roster.observe(9, AircraftType::A320, state, damageView(state, 100), pose, 0) > 0,
        "a change of aircraft is taken as it is, then tracked");
  // The number of pieces is bounded however many aircraft come apart.
  Breakaways crowd;
  for (std::uint64_t entity = 1; entity <= 40; ++entity) crowd.shatter(entity, type, whole, pose, float(entity));
  check(crowd.pieces().size() == Breakaways::kCapacity, "pieces are bounded");
  crowd.clear();
  check(crowd.pieces().empty(), "clear removes every piece");
  std::puts("battle breakaway: release on damage, slivers, kinematics, landing, shatter, respawn, bounds PASS");
}
void effects() {
  const auto type = AircraftType::Typhoon;
  const auto& definition = aircraftDefinition(type);
  {
    // A gun shot: tracer, flash, flame, light and gas at the muzzle, a case every other round.
    EffectPool pool(512);
    CombatEffects fx(pool, EffectsQuality::High);
    const Vec3 carrier{200, 0, 0};
    fx.onShot({0, 0, -3000}, {1200, 0, 0}, 3, true, 1, carrier);
    check(pool.countOf(EffectKind::Tracer) == 1 && pool.countOf(EffectKind::MuzzleFlash) == 1 && pool.countOf(EffectKind::Fire) == 1 &&
              pool.countOf(EffectKind::Light) == 1 && pool.countOf(EffectKind::Smoke) == 1,
          "a shot lights the muzzle");
    for (const auto& effect : pool.effects())
      if (effect.kind == EffectKind::MuzzleFlash) check((effect.velocity - carrier).norm() == 0, "the flash rides with the gun");
    fx.onShot({0, 0, -3000}, {1200, 0, 0}, 3, true, 2, carrier);
    check(pool.countOf(EffectKind::Debris) == 1, "every other round throws a case");
    // Without the carrier's velocity it is estimated from the round, never left at the round's own speed.
    EffectPool guess(64);
    CombatEffects estimate(guess, EffectsQuality::High);
    estimate.onShot({0, 0, -3000}, {1200, 0, 0}, 3, false, 5);
    for (const auto& effect : guess.effects())
      if (effect.kind == EffectKind::MuzzleFlash) check(std::abs(effect.velocity.x - 250) < 1e-9, "carrier speed is estimated");
    // A hit: flash, sparks and fragments carried along with the target.
    pool.clear();
    fx.onHit({50, 0, -3000}, true, 0, carrier);
    check(pool.countOf(EffectKind::Flash) == 1 && pool.countOf(EffectKind::Spark) == 12 && pool.countOf(EffectKind::Debris) == 5 &&
              pool.countOf(EffectKind::Smoke) == 1,
          "a hit throws sparks and fragments");
    for (const auto& effect : pool.effects()) check(effect.velocity.x > 60, "hit effects are carried with the target");
  }
  {
    // A warhead, an aircraft and a part breaking off each look different.
    EffectPool pool(1024);
    CombatEffects fx(pool, EffectsQuality::High);
    fx.onDetonation({0, 0, -3000});
    check(pool.countOf(EffectKind::Flash) == 1 && pool.countOf(EffectKind::Shockwave) == 1 && pool.countOf(EffectKind::Explosion) == 1 &&
              pool.countOf(EffectKind::Spark) == 36 && pool.countOf(EffectKind::Smoke) == 12 && pool.countOf(EffectKind::Debris) == 0,
          "a warhead is a flash, a ring and fragments");
    pool.update(8);
    check(pool.size() == 0, "a detonation is gone within seconds");
    fx.onDestroyed({0, 0, -3000}, {200, 0, 0});
    check(pool.countOf(EffectKind::Explosion) == 1 && pool.countOf(EffectKind::Shockwave) == 1 && pool.countOf(EffectKind::Debris) == 28,
          "an aircraft blows up with wreckage");
    // Burning wreckage leaves smoke behind it as it falls.
    const auto smoke = pool.countOf(EffectKind::Smoke);
    for (unsigned i = 0; i < 30; ++i) pool.update(1. / 60);
    check(pool.countOf(EffectKind::Smoke) > smoke, "burning debris trails smoke");
    pool.update(30);
    check(pool.size() == 0, "wreckage effects expire");
    fx.onPartLost({0, 0, -3000}, {200, 0, 0});
    check(pool.countOf(EffectKind::Flash) == 1 && pool.countOf(EffectKind::Spark) == 8 && pool.countOf(EffectKind::Debris) == 8,
          "a part breaking off throws fragments");
    pool.clear();
    fx.onPieceSmoke({0, 0, -3000}, {150, 0, 20}, false);
    check(pool.countOf(EffectKind::Smoke) == 1 && pool.countOf(EffectKind::Fire) == 0, "a falling piece smokes");
    fx.onPieceSmoke({0, 0, -3000}, {150, 0, 20}, true);
    check(pool.countOf(EffectKind::Fire) == 1, "a falling wreck burns");
  }
  {
    // Damage is seen on the aircraft: smoke from the part, fire from a dead
    // engine, and the burst of an engine going out, once.
    EffectPool pool(4096);
    CombatEffects fx(pool, EffectsQuality::High);
    fx.setEmissions(false, false);
    fx.setCondensation(false, 0);
    State state = flying(type);
    const auto fly = [&](unsigned frames) {
      for (unsigned i = 0; i < frames; ++i) {
        state.pos_ned += state.vel_ned * (1. / 60);
        pool.update(1. / 60);
        fx.updateAircraft(state, 1. / 60, type, 4, 1, 100);
      }
    };
    fly(30);
    check(pool.countOf(EffectKind::Trail) == 0 && pool.countOf(EffectKind::Fire) == 0 && pool.countOf(EffectKind::Light) > 0,
          "an intact aircraft trails nothing and shows both lights");
    applyPartDamage(definition.flight, state, DamagePart::LeftEngine, 100);
    state.n1[0] = 0;
    fly(1);
    check(pool.countOf(EffectKind::Flash) == 1 && pool.countOf(EffectKind::Explosion) == 1, "an engine going out is seen");
    fly(60);
    check(pool.countOf(EffectKind::Flash) == 0 && pool.countOf(EffectKind::Trail) > 20 && pool.countOf(EffectKind::Fire) > 0,
          "a dead engine burns and lays smoke, without a second burst");
    // The trail is unbroken: each length reaches from the last to the next.
    double longest = 0;
    for (const auto& effect : pool.effects())
      if (effect.kind == EffectKind::Trail) longest = std::max(longest, double(effect.stretch));
    check(longest > 4 && longest < 12, "smoke is laid in lengths along the flight path");
    // No fuel, no fire.
    pool.clear();
    state.fuel_mass = 0;
    fly(20);
    check(pool.countOf(EffectKind::Fire) == 0 && pool.countOf(EffectKind::Trail) > 0, "a dry wreck smokes without flame");
    // A torn wing takes its wingtip light with it.
    pool.clear();
    state = flying(type);
    applyPartDamage(definition.flight, state, DamagePart::LeftWing, 100);
    fly(40);
    unsigned red = 0, green = 0;
    for (const auto& effect : pool.effects())
      if (effect.kind == EffectKind::Light) ++((effect.tint & 0xffu) > 0x80 ? red : green);
    check(red == 0 && green > 0 && pool.countOf(EffectKind::Trail) > 0, "a torn wing loses its light and smokes");
    // A respawn far away is not joined to the old position by a trail.
    pool.clear();
    state.pos_ned.x += 5000;
    fx.updateAircraft(state, 1. / 60, type, 4, 1, 100);
    for (const auto& effect : pool.effects())
      if (effect.kind == EffectKind::Trail) check(effect.stretch < 300, "no trail is drawn across a jump");
    // Effects off emits nothing at all.
    EffectPool off(256);
    CombatEffects silent(off, EffectsQuality::Off);
    silent.onShot({}, {1000, 0, 0}, 3, true);
    silent.onHit({}, true);
    silent.onDetonation({});
    silent.onDestroyed({}, {});
    silent.onPartLost({}, {});
    silent.onPieceSmoke({}, {}, true);
    silent.updateAircraft(state, 1. / 60, type, 1, 1, 10);
    check(off.size() == 0, "effects off emits nothing");
    // A long fight stays inside the pool.
    EffectPool small(256);
    CombatEffects busy(small, EffectsQuality::High);
    for (unsigned i = 0; i < 2000; ++i) {
      busy.onShot({double(i), 0, -3000}, {1000, 0, 0}, 3, true, i);
      if (i % 50 == 0) busy.onDetonation({double(i), 0, -3000});
      small.update(1. / 60);
    }
    check(small.size() <= small.capacity() && small.peakSize() == small.capacity(), "effects stay bounded");
  }
  std::puts("battle effects: gun, hit, warhead, destruction, part loss, damage smoke and fire, bounds PASS");
}
void chat() {
  check(chatText("  hello there  ", 120) == "hello there" && chatText("\t\n", 120).empty() && chatText("   ", 120).empty() &&
            chatText("a\x01" "b\xc3\xa9" "c", 120) == "abc" && chatText(std::string(300, 'x'), 120).size() == 120 &&
            chatText(" x ", 1) == "x",
        "typed text is made fit to send");
  ChatLog log;
  check(log.visible(0, true, 10).empty(), "an empty log shows nothing");
  log.add({"Alpha", "one", 0, false, false});
  log.add({"", "", 1, false, false});
  check(log.size() == 1, "empty lines are not kept");
  log.add({"Bravo", "two", 5, false, true});
  log.add({"", "Alpha shot down Bravo", 8, true, false});
  // Fresh lines show on their own; old ones only with the box open.
  auto lines = log.visible(8.5, false, 6);
  check(lines.size() == 3 && lines.front()->text == "one" && lines.back()->notice, "recent lines show oldest first");
  lines = log.visible(12, false, 6);
  check(lines.size() == 2 && lines.front()->text == "two", "old lines drop out");
  check(log.visible(1000, false, 6).empty() && log.visible(1000, true, 6).size() == 3, "an open box shows the conversation");
  check(log.visible(8.5, true, 2).size() == 2 && log.visible(8.5, true, 2).front()->text == "two", "only the newest lines fit");
  const ChatEntry line{"A", "x", 10, false, false};
  check(ChatLog::opacity(line, 10, false) == 1 && ChatLog::opacity(line, 18.5, false) == .5f && ChatLog::opacity(line, 19, false) == 0 &&
            ChatLog::opacity(line, 500, true) == 1,
        "a line fades over its last second");
  for (unsigned i = 0; i < 500; ++i) log.add({"A", std::to_string(i), double(i), false, false});
  check(log.size() == ChatLog::kCapacity && log.visible(499, true, 1).front()->text == "499", "the log is bounded and keeps the newest");
  log.clear();
  check(log.size() == 0, "clear");
  std::puts("battle chat: sanitising, visibility, fading, bounds PASS");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    check(argc == 2, "suite required");
    const std::string suite = argv[1];
    if (suite == "view") view();
    else if (suite == "breakaway") breakaway();
    else if (suite == "effects") effects();
    else if (suite == "chat") chat();
    else throw std::invalid_argument("suite");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "BATTLE VISUAL FAIL: %s\n", error.what());
    return 1;
  }
}
