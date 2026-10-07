#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace ofs::client {
// Authenticated OFSPACK1 envelope. Decrypted bytes never leave process memory.
std::vector<std::uint8_t> readProtectedAsset(const std::string& path);
void writeProtectedAsset(const std::string& source, const std::string& destination,
                         const std::vector<std::uint8_t>& nonce);
}
