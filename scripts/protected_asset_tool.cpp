#include "gltf.hpp"
#include "protected_asset.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--verify") {
      auto mesh = ofs::client::loadGltf(argv[2]);
      if (!mesh.valid()) throw std::runtime_error("No renderable geometry");
      std::cout << mesh.triangleCount << ' ' << mesh.nodes.size() << ' ' << mesh.images.size() << '\n';
      return 0;
    }
    if (argc != 4) throw std::runtime_error("Usage: ofs_protected_asset_tool source.glb output.ofspack nonce_hex | --verify asset");
    std::string hex(argv[3]);
    if (hex.size() != 48 || hex.find_first_not_of("0123456789abcdef") != std::string::npos)
      throw std::runtime_error("Invalid nonce");
    std::vector<std::uint8_t> nonce;
    for (std::size_t i = 0; i < hex.size(); i += 2) nonce.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    ofs::client::writeProtectedAsset(argv[1], argv[2], nonce);
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
