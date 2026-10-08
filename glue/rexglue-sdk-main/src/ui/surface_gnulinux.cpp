/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstdlib>

#include <SDL3/SDL_video.h>

#include <rex/ui/surface_gnulinux.h>

namespace rex {
namespace ui {

bool XcbWindowSurface::GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const {
  xcb_get_geometry_reply_t* reply =
      xcb_get_geometry_reply(connection_, xcb_get_geometry(connection_, window_), nullptr);
  if (!reply) {
    return false;
  }
  width_out = reply->width;
  height_out = reply->height;
  std::free(reply);
  return true;
}

bool WaylandWindowSurface::GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const {
  int width = 0;
  int height = 0;
  if (!sdl_window_ || !SDL_GetWindowSizeInPixels(sdl_window_, &width, &height) || width <= 0 ||
      height <= 0) {
    return false;
  }
  width_out = uint32_t(width);
  height_out = uint32_t(height);
  return true;
}

}  // namespace ui
}  // namespace rex
