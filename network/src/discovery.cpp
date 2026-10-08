// The socket headers come first: on Windows winsock2.h has to precede any
// header that might pull in the older winsock declarations.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "ofs/net/discovery.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <thread>
namespace ofs::net {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
const Socket noSocket = INVALID_SOCKET;
void closeSocket(Socket s) { closesocket(s); }
bool nonBlocking(Socket s) {
  u_long enabled = 1;
  return ioctlsocket(s, FIONBIO, &enabled) == 0;
}
// Winsock is reference counted, so this pairs with the transport's own use.
struct SocketRuntime {
  SocketRuntime() {
    WSADATA data;
    ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  ~SocketRuntime() {
    if (ok)
      WSACleanup();
  }
  bool ok{};
};
#else
using Socket = int;
const Socket noSocket = -1;
void closeSocket(Socket s) { close(s); }
bool nonBlocking(Socket s) {
  const int flags = fcntl(s, F_GETFL, 0);
  return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
struct SocketRuntime {
  bool ok{true};
};
#endif
constexpr std::array<std::uint8_t, 8> queryMagic{'O', 'F', 'S', 'L',
                                                 'A', 'N', 1,   'Q'};
constexpr std::array<std::uint8_t, 8> replyMagic{'O', 'F', 'S', 'L',
                                                 'A', 'N', 1,   'R'};
bool option(Socket s, int name) {
  const int enabled = 1;
  return setsockopt(s, SOL_SOCKET, name,
                    reinterpret_cast<const char *>(&enabled),
                    socklen_t(sizeof(enabled))) == 0;
}
Socket openSocket() {
  const Socket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == noSocket)
    return noSocket;
  if (!nonBlocking(s)) {
    closeSocket(s);
    return noSocket;
  }
  return s;
}
bool printable(const std::string &text) {
  return std::all_of(text.begin(), text.end(),
                     [](unsigned char c) { return c >= 32 && c <= 126; });
}
} // namespace
std::vector<std::uint8_t> discoveryQuery() {
  return {queryMagic.begin(), queryMagic.end()};
}
bool isDiscoveryQuery(std::span<const std::uint8_t> bytes) {
  return bytes.size() == queryMagic.size() &&
         std::equal(bytes.begin(), bytes.end(), queryMagic.begin());
}
std::string lobbyName(std::string text) {
  std::erase_if(text, [](unsigned char c) { return c < 32 || c > 126; });
  if (text.size() > maxLobbyName)
    text.resize(maxLobbyName);
  while (!text.empty() && text.back() == ' ')
    text.pop_back();
  return text;
}
std::vector<std::uint8_t> encodeLobby(const LobbyInfo &lobby) {
  std::vector<std::uint8_t> bytes(replyMagic.begin(), replyMagic.end());
  const auto pair = [&](std::uint16_t value) {
    bytes.push_back(std::uint8_t(value >> 8));
    bytes.push_back(std::uint8_t(value));
  };
  const auto text = [&](const std::string &value, std::size_t limit) {
    const auto size = std::min(value.size(), limit);
    bytes.push_back(std::uint8_t(size));
    bytes.insert(bytes.end(), value.begin(), value.begin() + size);
  };
  pair(lobby.protocol);
  pair(lobby.port);
  bytes.push_back(lobby.players);
  bytes.push_back(lobby.maxPlayers);
  bytes.push_back(lobby.bots);
  text(lobby.name, maxLobbyName);
  text(lobby.version, 24);
  return bytes;
}
bool decodeLobby(std::span<const std::uint8_t> bytes, LobbyInfo &output) {
  if (bytes.size() < replyMagic.size() + 9 || bytes.size() > 128 ||
      !std::equal(replyMagic.begin(), replyMagic.end(), bytes.begin()))
    return false;
  std::size_t position = replyMagic.size();
  LobbyInfo lobby;
  const auto pair = [&] {
    const auto value =
        std::uint16_t((bytes[position] << 8) | bytes[position + 1]);
    position += 2;
    return value;
  };
  lobby.protocol = pair();
  lobby.port = pair();
  lobby.players = bytes[position++];
  lobby.maxPlayers = bytes[position++];
  lobby.bots = bytes[position++];
  const auto text = [&](std::string &value, std::size_t limit) {
    if (position >= bytes.size())
      return false;
    const std::size_t size = bytes[position++];
    if (size > limit || size > bytes.size() - position)
      return false;
    value.assign(bytes.begin() + position, bytes.begin() + position + size);
    position += size;
    return printable(value);
  };
  if (!text(lobby.name, maxLobbyName) || !text(lobby.version, 24) ||
      position != bytes.size() || !lobby.port || lobby.name.empty() ||
      lobby.players > lobby.maxPlayers)
    return false;
  output = std::move(lobby);
  return true;
}
struct DiscoveryResponder::Impl {
  SocketRuntime runtime;
  Socket socket{noSocket};
  // Answers are several times the size of a question and go to whatever
  // address a question claims to come from, so they are rationed: a few
  // launchers looking every few seconds never come near the limit, and a
  // flood of forged questions cannot turn the server into a traffic source.
  std::chrono::steady_clock::time_point window{};
  unsigned answered{};
};
DiscoveryResponder::DiscoveryResponder(std::uint16_t port)
    : impl_(std::make_unique<Impl>()) {
  if (!impl_->runtime.ok)
    return;
  const Socket s = openSocket();
  if (s == noSocket)
    return;
  // Several games on one computer share the port; each answers for itself.
  option(s, SO_REUSEADDR);
#ifdef SO_REUSEPORT
  option(s, SO_REUSEPORT);
#endif
  option(s, SO_BROADCAST);
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = htons(port);
  if (bind(s, reinterpret_cast<const sockaddr *>(&local),
           socklen_t(sizeof(local))) != 0) {
    closeSocket(s);
    return;
  }
  impl_->socket = s;
}
DiscoveryResponder::~DiscoveryResponder() {
  if (impl_->socket != noSocket)
    closeSocket(impl_->socket);
}
bool DiscoveryResponder::available() const {
  return impl_->socket != noSocket;
}
unsigned DiscoveryResponder::poll(const LobbyInfo &lobby) {
  if (impl_->socket == noSocket)
    return 0;
  unsigned answered = 0;
  const auto reply = encodeLobby(lobby);
  const auto now = std::chrono::steady_clock::now();
  if (now - impl_->window >= std::chrono::seconds(1)) {
    impl_->window = now;
    impl_->answered = 0;
  }
  // A flood of questions costs a bounded amount of work each call; those past
  // the ration are read and dropped so they do not queue.
  for (unsigned i = 0; i < 16; ++i) {
    std::array<char, 64> buffer;
    sockaddr_in from{};
    socklen_t length = sizeof(from);
    const auto received =
        recvfrom(impl_->socket, buffer.data(), int(buffer.size()), 0,
                 reinterpret_cast<sockaddr *>(&from), &length);
    if (received <= 0)
      break;
    const auto *data = reinterpret_cast<const std::uint8_t *>(buffer.data());
    if (!isDiscoveryQuery({data, std::size_t(received)}) ||
        impl_->answered >= answersPerSecond)
      continue;
    ++impl_->answered;
    sendto(impl_->socket, reinterpret_cast<const char *>(reply.data()),
           int(reply.size()), 0, reinterpret_cast<const sockaddr *>(&from),
           length);
    ++answered;
  }
  return answered;
}
std::vector<LobbyInfo> discoverLobbies(std::span<const std::string> targets,
                                       unsigned milliseconds,
                                       std::uint16_t port) {
  std::vector<LobbyInfo> lobbies;
  SocketRuntime runtime;
  if (!runtime.ok)
    return lobbies;
  const Socket s = openSocket();
  if (s == noSocket)
    return lobbies;
  option(s, SO_BROADCAST);
  const auto query = discoveryQuery();
  for (const auto &target : targets) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    if (inet_pton(AF_INET, target.c_str(), &to.sin_addr) != 1)
      continue;
    sendto(s, reinterpret_cast<const char *>(query.data()), int(query.size()),
           0, reinterpret_cast<const sockaddr *>(&to), socklen_t(sizeof(to)));
  }
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(milliseconds);
  while (std::chrono::steady_clock::now() < deadline && lobbies.size() < 64) {
    std::array<char, 160> buffer;
    sockaddr_in from{};
    socklen_t length = sizeof(from);
    const auto received =
        recvfrom(s, buffer.data(), int(buffer.size()), 0,
                 reinterpret_cast<sockaddr *>(&from), &length);
    if (received <= 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    LobbyInfo lobby;
    const auto *data = reinterpret_cast<const std::uint8_t *>(buffer.data());
    if (!decodeLobby({data, std::size_t(received)}, lobby))
      continue;
    char text[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text)))
      continue;
    lobby.address = text;
    // One game answers once per address it was asked on.
    const bool known =
        std::any_of(lobbies.begin(), lobbies.end(), [&](const LobbyInfo &l) {
          return l.address == lobby.address && l.port == lobby.port;
        });
    if (!known)
      lobbies.push_back(std::move(lobby));
  }
  closeSocket(s);
  return lobbies;
}
} // namespace ofs::net
