/**
 * @file native_host_memory_test.cpp
 * @brief Host memory budget fallback tests for the GTA IV native renderer.
 */

#include <catch2/catch_test_macros.hpp>

#include "graphics/gta4_native/native_host_memory.h"

namespace gta4 = rex::graphics::gta4_native;

TEST_CASE("GTA IV native host memory parses /proc/meminfo and PSI text",
          "[gta4-native][host-memory]") {
  uint64_t bytes = 0;
  REQUIRE(gta4::ParseNativeMemAvailable(
      "MemTotal:        7925452 kB\nMemFree:          123456 kB\nMemAvailable:    2097152 kB\n",
      bytes));
  REQUIRE(bytes == 2097152ull * 1024);
  REQUIRE_FALSE(gta4::ParseNativeMemAvailable("MemTotal: 1 kB\n", bytes));
  REQUIRE_FALSE(gta4::ParseNativeMemAvailable("MemAvailable: kB\n", bytes));

  double avg10 = -1.0;
  REQUIRE(gta4::ParseNativeMemoryPressureSomeAvg10(
      "some avg10=12.34 avg60=5.00 avg300=1.00 total=123\n"
      "full avg10=3.21 avg60=0.00 avg300=0.00 total=45\n",
      avg10));
  REQUIRE(avg10 > 12.339);
  REQUIRE(avg10 < 12.341);
  REQUIRE(gta4::ParseNativeMemoryPressureSomeAvg10("some avg10=0.00 avg60=0.00\n", avg10));
  REQUIRE(avg10 == 0.0);
  REQUIRE_FALSE(gta4::ParseNativeMemoryPressureSomeAvg10("full avg10=1.00\n", avg10));
}

TEST_CASE("GTA IV native host texture budget tracks headroom above the reserve",
          "[gta4-native][host-memory]") {
  constexpr uint64_t MiB = 1048576;
  // Plenty of headroom: capped at the heap fraction.
  REQUIRE(gta4::NativeHostTextureBudget(4000 * MiB, 500 * MiB, 6000 * MiB, 768 * MiB, 75) ==
          3000 * MiB);
  // Low headroom: budget is usage plus what is left above the reserve.
  REQUIRE(gta4::NativeHostTextureBudget(4000 * MiB, 1000 * MiB, 868 * MiB, 768 * MiB, 75) ==
          1100 * MiB);
  // Below the reserve: budget equals usage, so pressure (>= 90%) is active.
  REQUIRE(gta4::NativeHostTextureBudget(4000 * MiB, 1000 * MiB, 100 * MiB, 768 * MiB, 75) ==
          1000 * MiB);
  REQUIRE(gta4::NativeHostTextureBudget(4000 * MiB, 0, 0, 768 * MiB, 150) == 0);
}

TEST_CASE("GTA IV native host memory sample reads the running system",
          "[gta4-native][host-memory]") {
#if defined(__linux__)
  const auto sample = gta4::SampleNativeHostMemory();
  REQUIRE(sample.mem_available_valid);
  REQUIRE(sample.mem_available_bytes > 0);
#endif
}
