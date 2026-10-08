#pragma once
// Finding games on the local network.
//
// A hosting server answers a small broadcast question on a fixed UDP port with
// its name, game port and how full it is, so players on the same network see
// it listed and never type an address. Discovery only describes a game; the
// connection itself goes through the ordinary transport and its checks.
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace ofs::net {
constexpr std::uint16_t discoveryPort = 27019;
constexpr std::size_t maxLobbyName = 48;
// A hosted game answers at most this many questions in any one second.
constexpr unsigned answersPerSecond = 30;
struct LobbyInfo {
  std::string name;    // printable ASCII, at most maxLobbyName
  std::string version; // game version, for display
  std::string address; // filled in by the browser from the reply's source
  std::uint16_t port{}, protocol{};
  std::uint8_t players{}, maxPlayers{}, bots{};
};
// The eight-byte question a browser broadcasts.
std::vector<std::uint8_t> discoveryQuery();
bool isDiscoveryQuery(std::span<const std::uint8_t>);
// Explicit wire form of an answer; decoding rejects anything malformed.
std::vector<std::uint8_t> encodeLobby(const LobbyInfo &);
bool decodeLobby(std::span<const std::uint8_t>, LobbyInfo &);
// Keeps `text` to printable ASCII within maxLobbyName, for a name typed by a player.
std::string lobbyName(std::string text);

// Answers questions for one hosted game. A port that cannot be opened leaves
// the game playable by address; available() reports it.
class DiscoveryResponder {
public:
  explicit DiscoveryResponder(std::uint16_t port = discoveryPort);
  ~DiscoveryResponder();
  DiscoveryResponder(const DiscoveryResponder &) = delete;
  DiscoveryResponder &operator=(const DiscoveryResponder &) = delete;
  bool available() const;
  // Answers the questions waiting, a bounded number per call and per second,
  // and returns how many it answered.
  unsigned poll(const LobbyInfo &);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
// Asks the local network once and collects the answers that arrive within
// `milliseconds`. `targets` are numeric IPv4 addresses to ask, broadcast or not.
std::vector<LobbyInfo> discoverLobbies(std::span<const std::string> targets,
                                       unsigned milliseconds,
                                       std::uint16_t port = discoveryPort);
} // namespace ofs::net
