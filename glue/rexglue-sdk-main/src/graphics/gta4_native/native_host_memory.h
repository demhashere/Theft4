#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>
#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace rex::graphics::gta4_native {

// Host memory signals for drivers without VK_EXT_memory_budget (Honeykrisp
// reports one unified heap and no budget). Sampled at coarse intervals from
// renderer housekeeping, never per draw.
struct NativeHostMemorySample {
  bool mem_available_valid = false;
  uint64_t mem_available_bytes = 0;
  bool psi_valid = false;
  double psi_some_avg10 = 0.0;  // percent of wall time with a stalled task
};

// Parses "MemAvailable:  123456 kB" out of /proc/meminfo text.
inline bool ParseNativeMemAvailable(std::string_view text, uint64_t& bytes) {
  constexpr std::string_view key = "MemAvailable:";
  const size_t at = text.find(key);
  if (at == std::string_view::npos) return false;
  size_t i = at + key.size();
  while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
  uint64_t kib = 0;
  size_t digits = 0;
  while (i < text.size() && text[i] >= '0' && text[i] <= '9' && digits < 19) {
    kib = kib * 10 + uint64_t(text[i++] - '0');
    ++digits;
  }
  if (!digits) return false;
  bytes = kib * 1024;
  return true;
}

// Parses the "some avg10=1.23" field of /proc/pressure/memory.
inline bool ParseNativeMemoryPressureSomeAvg10(std::string_view text, double& avg10) {
  const size_t line = text.find("some ");
  if (line == std::string_view::npos) return false;
  const size_t field = text.find("avg10=", line);
  if (field == std::string_view::npos) return false;
  size_t i = field + 6;
  double whole = 0.0, scale = 0.0;
  size_t digits = 0;
  for (; i < text.size() && digits < 12; ++i) {
    const char c = text[i];
    if (c == '.' && scale == 0.0) { scale = 1.0; continue; }
    if (c < '0' || c > '9') break;
    ++digits;
    if (scale == 0.0) whole = whole * 10.0 + double(c - '0');
    else whole += double(c - '0') * (scale *= 0.1);
  }
  if (!digits) return false;
  avg10 = whole;
  return true;
}

inline size_t ReadNativeProcFile(const char* path, char* buffer, size_t capacity) {
#if defined(__linux__)
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return 0;
  size_t used = 0;
  while (used + 1 < capacity) {
    const ssize_t got = read(fd, buffer + used, capacity - 1 - used);
    if (got <= 0) break;
    used += size_t(got);
  }
  close(fd);
  buffer[used] = '\0';
  return used;
#else
  (void)path; (void)buffer; (void)capacity;
  return 0;
#endif
}

inline NativeHostMemorySample SampleNativeHostMemory() {
  NativeHostMemorySample sample;
  // MemAvailable is in the first few lines; PSI is two short lines.
  char buffer[1024];
  if (const size_t size = ReadNativeProcFile("/proc/meminfo", buffer, sizeof(buffer)))
    sample.mem_available_valid =
        ParseNativeMemAvailable(std::string_view(buffer, size), sample.mem_available_bytes);
  if (const size_t size = ReadNativeProcFile("/proc/pressure/memory", buffer, sizeof(buffer)))
    sample.psi_valid =
        ParseNativeMemoryPressureSomeAvg10(std::string_view(buffer, size), sample.psi_some_avg10);
  return sample;
}

// Texture budget for one heap from host headroom: what the renderer already
// holds plus what the host can still give before reaching the reserve, capped
// at a fraction of the heap. Usage is the renderer's own texture bytes, so the
// existing 90%/85% pressure thresholds fire as host headroom runs out.
constexpr uint64_t NativeHostTextureBudget(uint64_t heap_size, uint64_t texture_usage,
                                           uint64_t mem_available, uint64_t reserve,
                                           uint32_t heap_percent) {
  const uint64_t headroom = mem_available > reserve ? mem_available - reserve : 0;
  const uint64_t cap = heap_size / 100 * std::min<uint32_t>(heap_percent, 100);
  return std::min(cap, texture_usage + headroom);
}

}  // namespace rex::graphics::gta4_native
