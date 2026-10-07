#include "protected_asset.hpp"
#include "ofs_asset_key.hpp"
#include "monocypher.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
namespace ofs::client {
namespace {
constexpr std::size_t headerSize = 40, tagSize = 16;
constexpr std::size_t maxSize = 512 * 1024 * 1024;
constexpr std::array<std::uint8_t, 8> magic{'O','F','S','P','A','C','K','1'};
std::vector<std::uint8_t> read(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("Protected asset: cannot open " + path);
  auto size = file.tellg();
  if (size < 0 || size > static_cast<std::streamoff>(maxSize + headerSize + tagSize))
    throw std::runtime_error("Protected asset: invalid size");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
    throw std::runtime_error("Protected asset: incomplete read");
  return bytes;
}
void requireKey() {
  if (!kOfsAssetKeyEnabled) throw std::runtime_error("Protected asset: this build has no content key");
}
}
std::vector<std::uint8_t> readProtectedAsset(const std::string& path) {
  requireKey();
  auto bytes = read(path);
  if (bytes.size() < headerSize + tagSize || !std::equal(magic.begin(), magic.end(), bytes.begin()))
    throw std::runtime_error("Protected asset: invalid envelope");
  std::uint64_t length = 0;
  for (unsigned i = 0; i < 8; ++i) length |= std::uint64_t(bytes[32+i]) << (8*i);
  if (length > maxSize || length != bytes.size() - headerSize - tagSize)
    throw std::runtime_error("Protected asset: invalid payload length");
  std::vector<std::uint8_t> plain(static_cast<std::size_t>(length));
  if (crypto_aead_unlock(plain.data(), bytes.data()+headerSize+length, kOfsAssetKey,
      bytes.data()+8, bytes.data(), headerSize, bytes.data()+headerSize, plain.size()))
    throw std::runtime_error("Protected asset: authentication failed");
  return plain;
}
void writeProtectedAsset(const std::string& source, const std::string& destination,
                         const std::vector<std::uint8_t>& nonce) {
  requireKey();
  if (nonce.size() != 24) throw std::runtime_error("Protected asset: nonce must be 24 bytes");
  auto plain = read(source);
  // Tool accepts only self-contained GLBs; the caller checks the document too.
  if (plain.size() < 12 || plain.size() > maxSize ||
      !std::equal(plain.begin(), plain.begin()+4, "glTF"))
    throw std::runtime_error("Protected asset: expected GLB");
  std::vector<std::uint8_t> bytes(headerSize + plain.size() + tagSize);
  std::copy(magic.begin(), magic.end(), bytes.begin());
  std::copy(nonce.begin(), nonce.end(), bytes.begin()+8);
  for (unsigned i = 0; i < 8; ++i) bytes[32+i] = static_cast<std::uint8_t>(std::uint64_t(plain.size()) >> (8*i));
  crypto_aead_lock(bytes.data()+headerSize, bytes.data()+headerSize+plain.size(),
    kOfsAssetKey, nonce.data(), bytes.data(), headerSize, plain.data(), plain.size());
  crypto_wipe(plain.data(), plain.size());
  std::ofstream file(destination, std::ios::binary | std::ios::trunc);
  if (!file || !file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    throw std::runtime_error("Protected asset: cannot write output");
}
}
