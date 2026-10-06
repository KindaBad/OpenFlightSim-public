#pragma once
#include "gltf.hpp"
#include <span>
namespace ofs::client {
Image decodeImage(std::span<const std::uint8_t> encoded);
}
