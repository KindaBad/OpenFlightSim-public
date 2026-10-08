// Local-network play: finding a hosted game, knowing who is in it, and chat.
#include "ofs/net/client.hpp"
#include "ofs/net/discovery.hpp"
#include "ofs/net/server.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
using namespace ofs;
using namespace ofs::net;
namespace {
void check(bool value, const char *why) {
  if (!value)
    throw std::runtime_error(why);
}
// Tests run in parallel on one machine, so each takes a discovery port of its own.
std::uint16_t privatePort(unsigned offset) {
  const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::uint16_t(40000 + (std::uint64_t(seed) / 1000 + offset * 977) % 20000);
}
void wire() {
  LobbyInfo lobby;
  lobby.name = "Friday night";
  lobby.version = "0.5.0";
  lobby.port = 27020;
  lobby.protocol = protocolVersion;
  lobby.players = 3;
  lobby.maxPlayers = 16;
  lobby.bots = 2;
  const auto bytes = encodeLobby(lobby);
  LobbyInfo decoded;
  check(decodeLobby(bytes, decoded) && decoded.name == lobby.name &&
            decoded.version == lobby.version && decoded.port == 27020 &&
            decoded.protocol == protocolVersion && decoded.players == 3 &&
            decoded.maxPlayers == 16 && decoded.bots == 2 &&
            decoded.address.empty(),
        "lobby round trip");
  // The launcher lists games with its own decoder (launcher/lan.py); both
  // sides are pinned to the same bytes, which launcher/tests/test_lan.py holds too.
  auto pinned = lobby;
  pinned.protocol = 15;
  const std::vector<std::uint8_t> golden{
      'O', 'F', 'S', 'L', 'A', 'N', 1, 'R', 0x00, 0x0f, 0x69, 0x8c, 3, 16, 2, 12,
      'F', 'r', 'i', 'd', 'a', 'y', ' ', 'n', 'i', 'g', 'h', 't', 5, '0', '.', '5', '.', '0'};
  check(encodeLobby(pinned) == golden, "lobby wire form changed; update the launcher with it");
  // Every truncation is refused, and a corrupted byte either is refused or
  // still decodes to a well-formed lobby; nothing reads out of bounds.
  for (std::size_t size = 0; size < bytes.size(); ++size)
    check(!decodeLobby(std::span(bytes).first(size), decoded),
          "truncated lobby accepted");
  for (std::size_t i = 0; i < bytes.size(); ++i)
    for (unsigned value = 0; value < 256; value += 5) {
      auto corrupt = bytes;
      corrupt[i] = std::uint8_t(value);
      LobbyInfo result;
      if (decodeLobby(corrupt, result))
        check(!result.name.empty() && result.name.size() <= maxLobbyName &&
                  result.port && result.players <= result.maxPlayers,
              "corrupt lobby decoded to nonsense");
    }
  auto longer = bytes;
  longer.push_back(0);
  check(!decodeLobby(longer, decoded), "trailing bytes accepted");
  auto unprintable = lobby;
  unprintable.name = "bad\nname";
  check(!decodeLobby(encodeLobby(unprintable), decoded),
        "control characters accepted in a lobby name");
  auto full = lobby;
  full.players = 17;
  check(!decodeLobby(encodeLobby(full), decoded), "overfull lobby accepted");
  check(isDiscoveryQuery(discoveryQuery()) && !isDiscoveryQuery(bytes) &&
            !isDiscoveryQuery({}),
        "query recognition");
  check(lobbyName("  Ace's game \t\n") == "  Ace's game" &&
            lobbyName(std::string(200, 'x')).size() == maxLobbyName &&
            lobbyName("\x01\x02").empty(),
        "lobby names are made printable and bounded");
  // Names and chat on the game connection.
  Message m;
  m.type = Type::Joined;
  m.entity = 4;
  m.text = "Maverick";
  Message out;
  std::string reason;
  check(decode(encode(m), out, reason) && out.type == Type::Joined &&
            out.entity == 4 && out.text == "Maverick",
        "joined carries the pilot name");
  m.type = Type::Chat;
  m.text = std::string(maxChatText, 'a');
  check(decode(encode(m), out, reason) && out.text.size() == maxChatText &&
            out.entity == 4,
        "chat round trip at the length limit");
  m.text.push_back('a');
  bool refused = false;
  try {
    encode(m);
  } catch (const std::exception &) {
    refused = true;
  }
  check(refused, "over-long chat encoded");
  m.text = "ok";
  auto bytes2 = encode(m);
  bytes2.back() = '\n';
  check(!decode(bytes2, out, reason), "control character accepted in chat");
  m.text.clear();
  check(!decode(encode(m), out, reason), "empty chat accepted");
  std::puts("lan wire: lobby encoding, truncation and corruption, names, "
            "chat limits PASS");
}
void discovery() {
  const auto port = privatePort(1);
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 0;
  config.maxClients = 6;
  config.lobbyName = "Test range";
  config.version = "9.9.9";
  config.discoveryPort = port;
  Server server(config);
  check(server.announced(), "discovery port opened");
  // A second game on the same computer shares the port and answers for itself.
  config.lobbyName = "Second range";
  Server second(config);
  check(second.announced(), "second game shares the discovery port");
  ServerConfig quiet;
  quiet.bind = "127.0.0.1";
  quiet.port = 0;
  Server hidden(quiet);
  check(!hidden.announced() && hidden.lobby().name.empty(),
        "an unnamed game stays off the network list");
  std::vector<LobbyInfo> found;
  const std::string target = "127.0.0.1";
  // A question sent to one address reaches only one of the games sharing the
  // port (a LAN broadcast reaches them all), so one answer is what is required.
  for (unsigned attempt = 0; attempt < 40 && found.empty(); ++attempt) {
    std::thread asker([&] {
      for (const auto &lobby : discoverLobbies(std::span(&target, 1), 150, port))
        if (std::none_of(found.begin(), found.end(), [&](const auto &known) {
              return known.port == lobby.port;
            }))
          found.push_back(lobby);
    });
    const auto until =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(170);
    while (std::chrono::steady_clock::now() < until) {
      server.poll();
      second.poll();
      hidden.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    asker.join();
  }
  check(!found.empty(), "hosted game was not found");
  for (const auto &lobby : found) {
    const bool first = lobby.port == server.port();
    check((first || lobby.port == second.port()) &&
              lobby.name == (first ? "Test range" : "Second range") &&
              lobby.address == "127.0.0.1" && lobby.version == "9.9.9" &&
              lobby.protocol == protocolVersion && lobby.maxPlayers == 6 &&
              lobby.players == 0,
          "found lobby describes its game");
  }
  // Players are counted without the bots.
  Client pilot("Scout", AircraftType::Typhoon);
  pilot.connect("127.0.0.1", server.port());
  for (unsigned i = 0; i < 600 && !pilot.ready(); ++i) {
    server.poll();
    server.step();
    pilot.poll(1. / 120);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  check(pilot.ready() && server.lobby().players == 1 &&
            server.lobby().bots == 0,
        "lobby counts the pilots in it");
  // Answers are rationed, so forged questions cannot make the server a
  // source of traffic: a burst far past the limit gets at most the limit.
  {
    DiscoveryResponder rationed(std::uint16_t(port + 2));
    check(rationed.available(), "rationed responder");
    LobbyInfo info = server.lobby();
    unsigned answered = 0;
    const std::string local = "127.0.0.1";
    for (unsigned burst = 0; burst < 12; ++burst) {
      // Each call sends one question and waits no time for an answer.
      for (unsigned i = 0; i < 10; ++i)
        discoverLobbies(std::span(&local, 1), 0, std::uint16_t(port + 2));
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      for (unsigned i = 0; i < 20; ++i)
        answered += rationed.poll(info);
    }
    check(answered > 0 && answered <= 2 * answersPerSecond,
          "answers are not rationed");
  }
  // Garbage on the discovery port is ignored and never answered.
  const std::string garbage = "127.0.0.1";
  check(discoverLobbies(std::span(&garbage, 1), 30, std::uint16_t(port + 1))
            .empty(),
        "nothing answers on an unused port");
  std::puts("lan discovery: named games answer, share a port, describe "
            "themselves, unnamed games stay hidden PASS");
}
void chat() {
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 0;
  Server server(config);
  Client a("Alpha", AircraftType::Typhoon), b("Bravo", AircraftType::Typhoon);
  std::vector<ChatLine> heardA, heardB;
  const auto pump = [&](double seconds) {
    const auto start = std::chrono::steady_clock::now();
    auto next = start, last = start;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         start)
               .count() < seconds) {
      const auto now = std::chrono::steady_clock::now();
      const double dt = std::chrono::duration<double>(now - last).count();
      last = now;
      server.poll();
      while (now >= next) {
        server.step();
        next += std::chrono::microseconds(8333);
      }
      a.poll(dt);
      b.poll(dt);
      for (auto &line : a.takeChat())
        heardA.push_back(std::move(line));
      for (auto &line : b.takeChat())
        heardB.push_back(std::move(line));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  const auto heard = [](const std::vector<ChatLine> &lines, EntityId from,
                        const std::string &text) {
    return std::count_if(lines.begin(), lines.end(), [&](const ChatLine &l) {
      return l.from == from && l.text == text;
    });
  };
  a.connect("127.0.0.1", server.port());
  pump(.4);
  check(a.ready(), "first pilot connected");
  b.connect("127.0.0.1", server.port());
  pump(.4);
  check(b.ready(), "second pilot connected");
  // Both know each other by name, whoever arrived first.
  check(a.pilot(a.entity()) == "Alpha" && a.pilot(b.entity()) == "Bravo" &&
            b.pilot(a.entity()) == "Alpha" && b.pilot(b.entity()) == "Bravo" &&
            a.pilot(999).empty(),
        "pilots are known by name");
  check(heard(heardA, 0, "Bravo joined the game") == 1 &&
            heard(heardB, 0, "Bravo joined the game") == 1 &&
            heard(heardA, 0, "Alpha joined the game") == 1,
        "arrivals are announced");
  a.chat("fox two!");
  pump(.2);
  check(heard(heardA, a.entity(), "fox two!") == 1 &&
            heard(heardB, a.entity(), "fox two!") == 1,
        "chat reaches everyone, sender included");
  const auto line =
      std::find_if(heardB.begin(), heardB.end(),
                   [&](const ChatLine &l) { return l.text == "fox two!"; });
  check(line->name == "Alpha", "chat is attributed by name");
  // Control characters are dropped and over-long text is cut, not rejected.
  b.chat(std::string("tab\there") + std::string(200, '!'));
  b.chat("   ");
  pump(.2);
  const auto cut =
      std::find_if(heardA.begin(), heardA.end(),
                   [&](const ChatLine &l) { return l.from == b.entity(); });
  check(cut != heardA.end() && cut->text.size() == maxChatText &&
            cut->text.starts_with("tabhere!") &&
            std::count_if(heardA.begin(), heardA.end(),
                          [&](const ChatLine &l) {
                            return l.from == b.entity();
                          }) == 1,
        "chat is sanitised; blank lines are not sent");
  // Flooding is ignored without disconnecting the pilot.
  const auto before = std::count_if(
      heardB.begin(), heardB.end(),
      [&](const ChatLine &l) { return l.from == a.entity(); });
  for (unsigned i = 0; i < 40; ++i)
    a.chat("spam " + std::to_string(i));
  pump(.5);
  const auto after = std::count_if(
      heardB.begin(), heardB.end(),
      [&](const ChatLine &l) { return l.from == a.entity(); });
  check(after - before >= 1 && after - before <= 8 && a.ready() &&
            server.stats().invalid == 0,
        "chat flood is throttled, not punished");
  // A kill is announced to everyone by name.
  auto &victim = const_cast<Player &>(server.world().players().at(b.entity()));
  auto &killer = const_cast<Player &>(server.world().players().at(a.entity()));
  victim.lastAttacker = a.entity();
  victim.lastAttacked = server.world().tick();
  auto diving = victim.sim.state();
  diving.pos_ned.z = groundHeightNed(diving.pos_ned.x, diving.pos_ned.y) - 30;
  diving.vel_ned = {150, 0, 80};
  victim.sim.setState(diving);
  pump(.6);
  check(killer.life.kills == 1 &&
            heard(heardA, 0, "Alpha shot down Bravo") == 1 &&
            heard(heardB, 0, "Alpha shot down Bravo") == 1,
        "kills are announced by name");
  b.disconnect();
  pump(.4);
  check(heard(heardA, 0, "Bravo left the game") == 1 &&
            a.pilot(b.entity()).empty(),
        "departures are announced and forgotten");
  // A client that is not in a game sends nothing and does not fault.
  Client idle("Idle");
  idle.chat("hello?");
  check(idle.takeChat().empty(), "offline chat");
  std::puts("lan chat: roster by name, relay to all, sanitising, flood "
            "throttle, kill and departure notices PASS");
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2, "suite required");
    const std::string suite = argv[1];
    if (suite == "wire")
      wire();
    else if (suite == "discovery")
      discovery();
    else if (suite == "chat")
      chat();
    else
      throw std::invalid_argument("suite");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "LAN FAIL: %s\n", error.what());
    return 1;
  }
}
