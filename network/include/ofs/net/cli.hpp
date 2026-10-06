#pragma once
#include <charconv>
#include <stdexcept>
#include <string_view>
namespace ofs::net {
inline unsigned number(std::string_view s, unsigned min, unsigned max) {
  unsigned n = 0;
  auto [p, e] = std::from_chars(s.data(), s.data() + s.size(), n);
  if (e != std::errc{} || p != s.data() + s.size() || n < min || n > max)
    throw std::invalid_argument("numeric argument outside allowed range");
  return n;
}
inline std::string_view argument(int &i, int argc, char **argv) {
  if (i + 1 >= argc)
    throw std::invalid_argument("missing argument");
  return argv[++i];
}
} // namespace ofs::net
