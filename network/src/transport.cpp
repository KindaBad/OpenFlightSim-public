#include "ofs/net/transport.hpp"
#include "ofs/net/protocol.hpp"
#include "ofs/net/replication.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>
namespace ofs::net {
struct Transport::Impl {
  ISteamNetworkingSockets *sockets{};
  HSteamListenSocket listener{};
  HSteamNetPollGroup group{};
  std::set<Connection> connections;
  std::map<Connection, std::int32_t> payloadLimits;
  std::vector<TransportEvent> events;
  static inline unsigned users{};
  static inline std::map<HSteamListenSocket, Impl *> listeners;
  static inline std::map<Connection, Impl *> owners;
  static void status(SteamNetConnectionStatusChangedCallback_t *info) {
    Impl *self = nullptr;
    auto owner = owners.find(info->m_hConn);
    if (owner != owners.end())
      self = owner->second;
    else {
      auto it = listeners.find(info->m_info.m_hListenSocket);
      if (it != listeners.end())
        self = it->second;
    }
    if (!self)
      return;
    auto c = info->m_hConn;
    if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting &&
        self->listener) {
      if (self->connections.size() >= maxPlayers + 8 ||
          self->sockets->AcceptConnection(c) != k_EResultOK) {
        self->sockets->CloseConnection(c, 4001, "connection limit", false);
        return;
      }
      owners[c] = self;
      self->connections.insert(c);
    }
    if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
      if (!self->sockets->SetConnectionPollGroup(c, self->group)) {
        self->sockets->CloseConnection(c, 4002, "poll group", false);
        return;
      }
      self->events.push_back({TransportEvent::Kind::Connected, c, {}, {}});
    }
    if (info->m_info.m_eState ==
            k_ESteamNetworkingConnectionState_ClosedByPeer ||
        info->m_info.m_eState ==
            k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
      self->events.push_back({TransportEvent::Kind::Disconnected,
                              c,
                              {},
                              info->m_info.m_szEndDebug});
      self->sockets->CloseConnection(c, 0, nullptr, false);
      self->connections.erase(c);
      self->payloadLimits.erase(c);
      owners.erase(c);
    }
  }
};
Transport::Transport() : impl_(std::make_unique<Impl>()) {
  if (!Impl::users) {
    SteamDatagramErrMsg err;
    if (!GameNetworkingSockets_Init(nullptr, err))
      throw std::runtime_error(err);
  }
  ++Impl::users;
  impl_->sockets = SteamNetworkingSockets();
  impl_->group = impl_->sockets->CreatePollGroup();
  if (!impl_->group) {
    if (--Impl::users == 0)
      GameNetworkingSockets_Kill();
    throw std::runtime_error("GNS poll group creation failed");
  }
}
Transport::~Transport() {
  if (impl_->listener) {
    Impl::listeners.erase(impl_->listener);
    impl_->sockets->CloseListenSocket(impl_->listener);
  }
  for (auto c : impl_->connections) {
    Impl::owners.erase(c);
    impl_->sockets->CloseConnection(c, 0, "shutdown", false);
  }
  impl_->sockets->DestroyPollGroup(impl_->group);
  if (--Impl::users == 0)
    GameNetworkingSockets_Kill();
}
namespace {
SteamNetworkingIPAddr address(const std::string &host, std::uint16_t port) {
  SteamNetworkingIPAddr a;
  a.Clear();
  if (!a.ParseString(host.c_str()))
    throw std::invalid_argument("address must be a numeric IPv4/IPv6 literal");
  a.m_port = port;
  return a;
}
} // namespace
void Transport::listen(const std::string &host, std::uint16_t port) {
  if (impl_->listener)
    throw std::logic_error("already listening");
  SteamNetworkingConfigValue_t c[6];
  c[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
              reinterpret_cast<void *>(Impl::status));
  c[1].SetInt32(k_ESteamNetworkingConfig_SendBufferSize, 262144);
  c[2].SetInt32(k_ESteamNetworkingConfig_SendRateMax, 2000000);
  c[3].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, 10000);
  c[4].SetInt32(k_ESteamNetworkingConfig_MTU_PacketSize, 1200);
  c[5].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize, snapshotPayload);
  // GNS disallows port zero; test servers request a random private port with
  // retries.
  std::random_device random;
  for (unsigned attempt = 0; attempt < (port ? 1u : 32u); ++attempt) {
    auto selected =
        port ? port : static_cast<std::uint16_t>(49152 + random() % 16000);
    impl_->listener =
        impl_->sockets->CreateListenSocketIP(address(host, selected), 6, c);
    if (impl_->listener)
      break;
  }
  if (!impl_->listener)
    throw std::runtime_error("cannot bind GNS listen socket");
  Impl::listeners[impl_->listener] = impl_.get();
}
Connection Transport::connect(const std::string &host, std::uint16_t port) {
  SteamNetworkingConfigValue_t c[6];
  c[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
              reinterpret_cast<void *>(Impl::status));
  c[1].SetInt32(k_ESteamNetworkingConfig_SendBufferSize, 262144);
  c[2].SetInt32(k_ESteamNetworkingConfig_SendRateMax, 2000000);
  c[3].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, 10000);
  c[4].SetInt32(k_ESteamNetworkingConfig_MTU_PacketSize, 1200);
  c[5].SetInt32(k_ESteamNetworkingConfig_RecvMaxMessageSize, snapshotPayload);
  auto conn = impl_->sockets->ConnectByIPAddress(address(host, port), 6, c);
  if (!conn)
    throw std::runtime_error("GNS connect failed");
  impl_->connections.insert(conn);
  Impl::owners[conn] = impl_.get();
  return conn;
}
std::vector<TransportEvent> Transport::poll() {
  impl_->sockets->RunCallbacks();
  SteamNetworkingMessage_t *messages[64];
  unsigned budget = 256;
  while (budget) {
    int count = impl_->sockets->ReceiveMessagesOnPollGroup(
        impl_->group, messages, static_cast<int>(std::min(64u, budget)));
    if (count <= 0)
      break;
    budget -= static_cast<unsigned>(count);
    for (int i = 0; i < count; ++i) {
      auto *m = messages[i];
      TransportEvent e{TransportEvent::Kind::Packet, m->m_conn, {}, {}};
      if (m->m_cbSize > 0 &&
          static_cast<std::size_t>(m->m_cbSize) <= snapshotPayload) {
        const auto *p = static_cast<const std::uint8_t *>(m->m_pData);
        e.bytes.assign(p, p + m->m_cbSize);
      }
      impl_->events.push_back(std::move(e));
      m->Release();
    }
  }
  auto events = std::move(impl_->events);
  impl_->events.clear();
  return events;
}
bool Transport::send(Connection c, std::span<const std::uint8_t> bytes,
                     bool reliable) {
  if (!withinApplicationBudget(bytes.size()))
    return false;
  auto flags = reliable ? k_nSteamNetworkingSend_Reliable
                        : k_nSteamNetworkingSend_UnreliableNoDelay;
  return impl_->sockets->SendMessageToConnection(
             c, bytes.data(), static_cast<std::uint32_t>(bytes.size()), flags,
             nullptr) == k_EResultOK;
}
std::vector<bool>
Transport::sendBatch(Connection c,
                     std::span<const std::vector<std::uint8_t>> packets) {
  if (packets.size() > fragmentLimit)
    return {};
  std::vector<bool> accepted(packets.size(), false);
  std::array<SteamNetworkingMessage_t *, fragmentLimit> messages{};
  std::array<int64, fragmentLimit> results{};
  for (std::size_t i = 0; i < packets.size(); ++i) {
    if (packets[i].empty() || packets[i].size() > snapshotPayload)
      continue;
    auto *message = SteamNetworkingUtils()->AllocateMessage(
        static_cast<int>(packets[i].size()));
    if (!message)
      continue;
    std::memcpy(message->m_pData, packets[i].data(), packets[i].size());
    message->m_conn = c;
    message->m_nFlags = k_nSteamNetworkingSend_UnreliableNoDelay;
    messages[i] = message;
  }
  if (!packets.empty())
    impl_->sockets->SendMessages(static_cast<int>(packets.size()),
                                 messages.data(), results.data(), true);
  for (std::size_t i = 0; i < packets.size(); ++i)
    accepted[i] = results[i] > 0;
  return accepted;
}
void Transport::close(Connection c, const std::string &reason, bool linger) {
  impl_->sockets->CloseConnection(c, 4000, reason.c_str(), linger);
  impl_->connections.erase(c);
  impl_->payloadLimits.erase(c);
  Impl::owners.erase(c);
}
LinkStats Transport::stats(Connection c) const {
  SteamNetConnectionRealTimeStatus_t s{};
  if (impl_->sockets->GetConnectionRealTimeStatus(c, &s, 0, nullptr) !=
      k_EResultOK)
    return {};
  auto &payload = impl_->payloadLimits[c];
  if (!payload) {
    std::size_t size = sizeof(payload);
    ESteamNetworkingConfigDataType type;
    SteamNetworkingUtils()->GetConfigValue(
        k_ESteamNetworkingConfig_MTU_DataSize,
        k_ESteamNetworkingConfig_Connection, c, &type, &payload, &size);
  }
  return {s.m_nPing, s.m_flInBytesPerSec, s.m_flOutBytesPerSec,
          s.m_cbPendingReliable + s.m_cbPendingUnreliable, payload};
}
std::uint16_t Transport::port() const {
  SteamNetworkingIPAddr a;
  a.Clear();
  return impl_->sockets->GetListenSocketAddress(impl_->listener, &a) ? a.m_port
                                                                     : 0;
}
void Transport::conditions(const std::string &preset) {
  int lag = 0, jitter = 0;
  float loss = 0, reorder = 0;
  if (preset == "good") {
    lag = 15;
    jitter = 3;
    loss = .1f;
  } else if (preset == "moderate") {
    lag = 50;
    jitter = 15;
    loss = 2;
    reorder = 1;
  } else if (preset == "bad") {
    lag = 100;
    jitter = 30;
    loss = 5;
    reorder = 2;
  } else if (preset != "local")
    throw std::invalid_argument("conditions: local|good|moderate|bad");
  auto *u = SteamNetworkingUtils();
  u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send,
                               lag);
  u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send,
                               loss);
  u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketReorder_Send,
                               reorder);
  u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketReorder_Time,
                               15);
  u->SetGlobalConfigValueFloat(
      k_ESteamNetworkingConfig_FakePacketJitter_Send_Avg,
      static_cast<float>(jitter));
  u->SetGlobalConfigValueFloat(
      k_ESteamNetworkingConfig_FakePacketJitter_Send_Max,
      static_cast<float>(jitter * 3));
  u->SetGlobalConfigValueFloat(
      k_ESteamNetworkingConfig_FakePacketJitter_Send_Pct, jitter ? 30.f : 0.f);
}
} // namespace ofs::net
