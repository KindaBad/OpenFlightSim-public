#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
namespace ofs::client {
enum class ScreenshotOrder { RGBA8, BGRA8 };
inline void screenshotRgbRow(std::span<const std::uint8_t> source,std::span<std::uint8_t> rgb,ScreenshotOrder order) {
  if(rgb.size()%3||source.size()<rgb.size()/3*4)throw std::invalid_argument("Short screenshot row");
  const unsigned red=order==ScreenshotOrder::BGRA8?2:0,blue=2-red;
  for(std::size_t x=0;x<rgb.size()/3;++x){rgb[x*3]=source[x*4+red];rgb[x*3+1]=source[x*4+1];rgb[x*3+2]=source[x*4+blue];}
}
}
