#pragma once
// How large the in-game interface is drawn. Its layout is written in design
// units for a 1280x800 window; the scale turns those units into device pixels
// so that the interface grows with the window and its text keeps the same
// proportions on any screen. The fonts are rasterized at the size they are
// drawn (see uiFont), so the text stays sharp instead of being stretched from a
// smaller bitmap.

#include <algorithm>
#include <cmath>

struct ImFont;

namespace ofs::client {

// Device pixels per design unit for a window of this many device pixels. It is
// never below one, so a small window keeps the design size, and it moves in
// eighths so that dragging a window's edge does not rebuild the fonts each frame.
inline float uiDeviceScale(int widthPixels, int heightPixels) {
  const float fit = std::min(widthPixels / 1280.f, heightPixels / 800.f);
  return std::clamp(std::round(fit * 8.f) / 8.f, 1.f, 4.f);
}

// The font for text drawn at this design size, built at the current scale.
ImFont* uiFont(float designSize);

// Window points per design unit. ImGui lays its windows out in points, which
// differ from device pixels on high-density displays.
float uiPointScale();

}  // namespace ofs::client
