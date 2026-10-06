#pragma once
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
namespace ofs::client {
// Simple stderr log. Accepts anything streamable, so call sites do not have to
// build temporary std::string values just to report a message.
template <typename T>
void log(const char* category, const T& message) {
  if constexpr (std::is_convertible_v<T, std::string_view>) {
    std::fprintf(stderr, "[%s] %.*s\n", category,
                 static_cast<int>(std::string_view(message).size()),
                 std::string_view(message).data());
  } else {
    std::fprintf(stderr, "[%s] %s\n", category, message.c_str());
  }
}
inline void log(const char* category, const char* message) {
  std::fprintf(stderr, "[%s] %s\n", category, message);
}
}
