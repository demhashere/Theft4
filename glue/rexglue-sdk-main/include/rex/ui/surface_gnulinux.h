#pragma once
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

#include <rex/ui/surface.h>

#include <xcb/xcb.h>

struct SDL_Window;
struct wl_display;
struct wl_surface;

namespace rex {
namespace ui {

class XcbWindowSurface final : public Surface {
 public:
  explicit XcbWindowSurface(xcb_connection_t* connection, xcb_window_t window)
      : connection_(connection), window_(window) {}
  TypeIndex GetType() const override { return kTypeIndex_XcbWindow; }
  xcb_connection_t* connection() const { return connection_; }
  xcb_window_t window() const { return window_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override;

 private:
  xcb_connection_t* connection_;
  xcb_window_t window_;
};

// Native Wayland toplevel owned by SDL. Wayland has no geometry query, so the
// size comes from SDL in physical pixels (the window uses high pixel density).
class WaylandWindowSurface final : public Surface {
 public:
  WaylandWindowSurface(SDL_Window* sdl_window, wl_display* display, wl_surface* surface)
      : sdl_window_(sdl_window), display_(display), surface_(surface) {}
  TypeIndex GetType() const override { return kTypeIndex_WaylandWindow; }
  wl_display* display() const { return display_; }
  wl_surface* surface() const { return surface_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override;

 private:
  SDL_Window* sdl_window_;
  wl_display* display_;
  wl_surface* surface_;
};

}  // namespace ui
}  // namespace rex
