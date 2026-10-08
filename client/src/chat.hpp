#pragma once
// In-flight chat: what was said and what is still worth showing.
//
// The network carries the lines; this keeps the recent ones and decides which
// are on screen, so a quiet game shows nothing and an open chat box shows the
// conversation so far. It has no window or renderer dependency.

#include <algorithm>
#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace ofs::client {

struct ChatEntry {
  std::string name, text;
  double time{};  // seconds, on the caller's clock
  bool notice{};  // from the server itself: arrivals, departures, kills
  bool own{};
};

// Text as typed, made fit to send: printable ASCII only, without leading or
// trailing spaces, cut to `limit`. Empty when nothing is left to say.
inline std::string chatText(std::string_view typed, std::size_t limit) {
  std::string text;
  for (const unsigned char c : typed)
    if (c >= 32 && c <= 126) text.push_back(char(c));
  const auto first = text.find_first_not_of(' ');
  if (first == std::string::npos) return {};
  text = text.substr(first, text.find_last_not_of(' ') - first + 1);
  if (text.size() > limit) text.resize(limit);
  return text;
}

class ChatLog {
 public:
  static constexpr std::size_t kCapacity = 48;
  // A line stays up this long after it arrives, then fades over the last second.
  static constexpr double kVisibleSeconds = 9;

  void add(ChatEntry entry) {
    if (entry.text.empty()) return;
    entries_.push_back(std::move(entry));
    while (entries_.size() > kCapacity) entries_.pop_front();
  }
  void clear() { entries_.clear(); }
  std::size_t size() const { return entries_.size(); }

  // The newest `limit` lines to draw, oldest first. With the chat box open
  // that is the conversation so far; otherwise only lines still fresh at `now`.
  std::vector<const ChatEntry*> visible(double now, bool open, std::size_t limit) const {
    std::vector<const ChatEntry*> lines;
    for (auto entry = entries_.rbegin(); entry != entries_.rend() && lines.size() < limit; ++entry)
      if (open || now - entry->time < kVisibleSeconds) lines.push_back(&*entry);
    std::reverse(lines.begin(), lines.end());
    return lines;
  }
  // Opacity of a line at `now`: full, then fading out over its last second.
  static float opacity(const ChatEntry& entry, double now, bool open) {
    return open ? 1.f : float(std::clamp(kVisibleSeconds - (now - entry.time), 0., 1.));
  }

 private:
  std::deque<ChatEntry> entries_;
};

}  // namespace ofs::client
