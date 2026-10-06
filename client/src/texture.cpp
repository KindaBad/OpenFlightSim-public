#include "texture.hpp"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_MAX_DIMENSIONS 4096
#include "stb_image.h"
#include <limits>
#include <memory>
#include <stdexcept>

namespace ofs::client {
Image decodeImage(std::span<const std::uint8_t> encoded) {
  if (encoded.empty() || encoded.size() > 64 * 1024 * 1024)
    throw std::runtime_error("PNG/JPEG image missing or exceeds 64 MiB");
  int width, height, channels;
  if (!stbi_info_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels) ||
      width < 1 || height < 1 || width > 4096 || height > 4096)
    throw std::runtime_error("invalid PNG/JPEG dimensions (limit 4096)");
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
      stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 4),
      stbi_image_free);
  if (!pixels) throw std::runtime_error("PNG/JPEG decoding failed");
  Image image;
  image.width = width; image.height = height;
  image.rgba.assign(pixels.get(), pixels.get() + static_cast<std::size_t>(width) * height * 4);
  return image;
}
}
