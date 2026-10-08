#pragma once
// Opt-in (THEFT4_PASS_STATS=1) dynamic-rendering pass statistics for the
// Vulkan recorder: which call sites begin and end passes, how many draws each
// pass holds, and how much of the render area the draws' viewport/scissor
// rectangles cover. Tile-based GPUs load and store the whole render area per
// pass, so this measures what tighter or merged passes could save. Render
// worker thread only; writes a summary every 600 frames.
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rex::graphics::gta4_native::pass_stats {

inline bool Enabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("THEFT4_PASS_STATS");
    return value && std::strcmp(value, "1") == 0;
  }();
  return enabled;
}

struct SizeBucket {
  uint64_t passes = 0, draws = 0, empty_passes = 0;
  double area = 0, covered = 0;  // pixels: render area, union of draw rectangles
};

struct State {
  bool open = false;
  int begin_site = 0;
  uint32_t width = 0, height = 0, draws = 0;
  int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  uint64_t frames = 0, window_frames = 0;
  std::map<std::pair<int, int>, uint64_t> sites;  // (begin line, end line) -> passes
  std::map<std::pair<uint32_t, uint32_t>, SizeBucket> sizes;
  std::array<uint64_t, 6> draw_histogram{};  // 0, 1, 2-4, 5-16, 17-64, 65+
  std::map<std::string, uint64_t> break_reasons;  // scope breaks on target changes
};

inline State& Get() {
  static State state;
  return state;
}

inline void Begin(const VkRenderingInfo* info, int line) {
  if (!Enabled() || !info) return;
  State& s = Get();
  s.open = true;
  s.begin_site = line;
  s.width = info->renderArea.extent.width;
  s.height = info->renderArea.extent.height;
  s.draws = 0;
  s.x0 = s.y0 = INT32_MAX;
  s.x1 = s.y1 = INT32_MIN;
}

// Rectangle a draw can touch: its scissor intersected with its viewport.
inline void Draw(const VkRect2D& scissor, const VkViewport& viewport) {
  if (!Enabled()) return;
  State& s = Get();
  if (!s.open) return;
  ++s.draws;
  int32_t x0 = scissor.offset.x, y0 = scissor.offset.y;
  int32_t x1 = x0 + int32_t(scissor.extent.width), y1 = y0 + int32_t(scissor.extent.height);
  const float vx0 = std::min(viewport.x, viewport.x + viewport.width);
  const float vy0 = std::min(viewport.y, viewport.y + viewport.height);
  const float vx1 = std::max(viewport.x, viewport.x + viewport.width);
  const float vy1 = std::max(viewport.y, viewport.y + viewport.height);
  x0 = std::max(x0, int32_t(vx0));
  y0 = std::max(y0, int32_t(vy0));
  x1 = std::min(x1, int32_t(vx1 + 0.999f));
  y1 = std::min(y1, int32_t(vy1 + 0.999f));
  if (x1 <= x0 || y1 <= y0) return;
  s.x0 = std::min(s.x0, x0);
  s.y0 = std::min(s.y0, y0);
  s.x1 = std::max(s.x1, x1);
  s.y1 = std::max(s.y1, y1);
}

inline void End(int line) {
  if (!Enabled()) return;
  State& s = Get();
  if (!s.open) return;
  s.open = false;
  ++s.sites[{s.begin_site, line}];
  SizeBucket& bucket = s.sizes[{s.width, s.height}];
  ++bucket.passes;
  bucket.draws += s.draws;
  const double area = double(s.width) * double(s.height);
  bucket.area += area;
  if (s.x1 > s.x0 && s.y1 > s.y0) {
    const int32_t x0 = std::max(s.x0, 0), y0 = std::max(s.y0, 0);
    const int32_t x1 = std::min(s.x1, int32_t(s.width)), y1 = std::min(s.y1, int32_t(s.height));
    if (x1 > x0 && y1 > y0) bucket.covered += double(x1 - x0) * double(y1 - y0);
  } else {
    ++bucket.empty_passes;
  }
  const uint32_t d = s.draws;
  ++s.draw_histogram[d == 0 ? 0 : d == 1 ? 1 : d <= 4 ? 2 : d <= 16 ? 3 : d <= 64 ? 4 : 5];
}

// Why a target change ended the scope (only called when Enabled()).
inline void Reason(const std::string& key) { ++Get().break_reasons[key]; }

inline void Write() {
  State& s = Get();
  std::string path;
  if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data) {
    path = std::string(data) + "/LibertyRecomp/Diagnostics/pass-stats.txt";
  } else if (const char* home = std::getenv("HOME")) {
    path = std::string(home) + "/.local/share/LibertyRecomp/Diagnostics/pass-stats.txt";
  } else {
    return;
  }
  FILE* file = std::fopen(path.c_str(), "a");
  if (!file) return;
  const double n = double(std::max<uint64_t>(s.window_frames, 1));
  uint64_t passes = 0;
  for (const auto& [key, count] : s.sites) passes += count;
  std::fprintf(file, "=== frames %llu-%llu: %.1f passes/frame\n",
               (unsigned long long)(s.frames - s.window_frames + 1),
               (unsigned long long)s.frames, passes / n);
  std::fprintf(file, "draws per pass (per frame): 0:%.1f 1:%.1f 2-4:%.1f 5-16:%.1f 17-64:%.1f 65+:%.1f\n",
               s.draw_histogram[0] / n, s.draw_histogram[1] / n, s.draw_histogram[2] / n,
               s.draw_histogram[3] / n, s.draw_histogram[4] / n, s.draw_histogram[5] / n);
  std::fprintf(file, "by render area (passes/frame, draws/pass, empty/frame, Mpx/frame area, covered%%):\n");
  for (const auto& [size, b] : s.sizes) {
    std::fprintf(file, "  %5ux%-5u %7.1f %7.1f %6.1f %8.2f %6.1f%%\n", size.first, size.second,
                 b.passes / n, b.passes ? double(b.draws) / b.passes : 0.0, b.empty_passes / n,
                 b.area / n / 1e6, b.area > 0 ? 100.0 * b.covered / b.area : 0.0);
  }
  std::fprintf(file, "begin line -> end line (passes/frame):\n");
  for (const auto& [key, count] : s.sites) {
    std::fprintf(file, "  %6d -> %-6d %7.2f\n", key.first, key.second, count / n);
  }
  std::vector<std::pair<uint64_t, std::string>> reasons;
  for (const auto& [key, count] : s.break_reasons) reasons.emplace_back(count, key);
  std::sort(reasons.rbegin(), reasons.rend());
  std::fprintf(file, "target-change breaks (per frame):\n");
  for (size_t i = 0; i < reasons.size() && i < 16; ++i) {
    std::fprintf(file, "  %7.2f  %s\n", reasons[i].first / n, reasons[i].second.c_str());
  }
  std::fclose(file);
  s.break_reasons.clear();
  s.sites.clear();
  s.sizes.clear();
  s.draw_histogram = {};
  s.window_frames = 0;
}

inline void Frame() {
  if (!Enabled()) return;
  State& s = Get();
  ++s.frames;
  if (++s.window_frames == 600) Write();
}

}  // namespace rex::graphics::gta4_native::pass_stats
