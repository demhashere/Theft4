/**
 * @file native_helper_thread_test.cpp
 * @brief Portable helper-thread, preparation-task and deferred-cleanup tests.
 */

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include "graphics/gta4_native/native_deferred_cleanup.h"
#include "graphics/gta4_native/native_helper_thread.h"
#include "graphics/gta4_native/native_preparation_task.h"

namespace gta4 = rex::graphics::gta4_native;

namespace {
struct Job {
  std::vector<int> values;
  int sum = 0;
  std::thread::id ran_on{};
};
void SumJob(void* raw) {
  auto& job = *static_cast<Job*>(raw);
  for (int value : job.values) job.sum += value;
  job.ran_on = std::this_thread::get_id();
}

struct Record { int value = 0; };
struct Recycler {
  std::atomic<int> recycled{0};
  void RecycleExternalBatch(std::vector<std::unique_ptr<Record>>& records) {
    for (auto& record : records) recycled += record->value;
    records.clear();
  }
};
}  // namespace

TEST_CASE("GTA IV native helper thread runs one job at a time and publishes results",
          "[gta4-native][helper-thread]") {
  gta4::NativeHelperThread helper;
  REQUIRE_FALSE(helper.launched());
  Job unlaunched;
  REQUIRE_FALSE(helper.Start(&unlaunched, &SumJob));
  REQUIRE(helper.Launch("Theft4 helper test name longer than fifteen"));
  REQUIRE(helper.launched());
  for (int round = 0; round < 200; ++round) {
    Job job{{round, 1, 2, 3}};
    REQUIRE(helper.Start(&job, &SumJob));
    Job second;
    // One job in flight per owner: a second start is refused until joined.
    REQUIRE_FALSE(helper.Start(&second, &SumJob));
    helper.Join();
    REQUIRE(job.sum == round + 6);
    REQUIRE(job.ran_on != std::this_thread::get_id());
    REQUIRE(helper.TryJoin());
  }
  helper.Shutdown();
  REQUIRE_FALSE(helper.launched());
}

TEST_CASE("GTA IV native helper TryJoin never blocks and observes completion",
          "[gta4-native][helper-thread]") {
  Job job{{4, 5}};  // outlives the helper, which joins the last job on destruction
  gta4::NativeHelperThread helper;
  REQUIRE(helper.Launch("test"));
  REQUIRE(helper.TryJoin());  // nothing in flight
  REQUIRE(helper.Start(&job, &SumJob));
  while (!helper.TryJoin()) std::this_thread::yield();
  REQUIRE(job.sum == 9);
  REQUIRE(helper.Start(&job, &SumJob));
  // Destruction joins the in-flight job before stopping the thread.
}

TEST_CASE("GTA IV native preparation task stays serial unless requested on Linux",
          "[gta4-native][helper-thread]") {
#if defined(__linux__)
  unsetenv("THEFT4_PARALLEL_PREPARATION");
  REQUIRE_FALSE(gta4::NativePreparationTask::Requested());
  setenv("THEFT4_PARALLEL_PREPARATION", "0", 1);
  REQUIRE_FALSE(gta4::NativePreparationTask::Requested());
  setenv("THEFT4_PARALLEL_PREPARATION", "1", 1);
  REQUIRE(gta4::NativePreparationTask::Requested());
  unsetenv("THEFT4_PARALLEL_PREPARATION");
#endif
  gta4::NativePreparationTask off;
  off.Initialize(false, "off", 1);
  REQUIRE_FALSE(off.available());
  Job job{{1}};
  REQUIRE_FALSE(off.Start(&job, &SumJob));

  gta4::NativePreparationTask small;
  small.Initialize(true, "small", 100000);
  REQUIRE_FALSE(small.available());

  gta4::NativePreparationTask task;
  task.Initialize(true, "Theft4 constant preparation", 1);
  REQUIRE(task.available_cpus() >= 1);
  REQUIRE(task.available());
  REQUIRE(task.Start(&job, &SumJob));
  REQUIRE(task.pending());
  REQUIRE_FALSE(task.Start(&job, &SumJob));
  task.Wait();
  REQUIRE_FALSE(task.pending());
  REQUIRE(job.sum == 1);
  task.Initialize(false, "ignored", 1);  // initialization is latched
  REQUIRE(task.available());
}

TEST_CASE("GTA IV native deferred cleanup recycles completed batches off the caller",
          "[gta4-native][helper-thread]") {
  using Cleanup = gta4::NativeDeferredCleanup<Record, Recycler>;
  Recycler recycler;
  {
    Cleanup disabled;
    disabled.Initialize(false, 8);
    std::vector<std::unique_ptr<Record>> records;
    records.push_back(std::make_unique<Record>(Record{1}));
    REQUIRE_FALSE(disabled.TryStart(records, 0, recycler));
    REQUIRE(records.size() == 1);
  }
  Cleanup cleanup;
  cleanup.Initialize(true, 8);
  REQUIRE(cleanup.available());
  int expected = 0;
  for (int round = 1; round <= 50; ++round) {
    std::vector<std::unique_ptr<Record>> records;
    for (int i = 0; i < 10; ++i) records.push_back(std::make_unique<Record>(Record{round}));
    expected += round * 10;
    REQUIRE(cleanup.TryStart(records, 128, recycler));
    REQUIRE(records.empty());
    std::vector<std::unique_ptr<Record>> busy;
    busy.push_back(std::make_unique<Record>(Record{1}));
    // Only one batch in flight; the caller keeps the records it could not hand off.
    if (!cleanup.TryStart(busy, 0, recycler)) REQUIRE(busy.size() == 1);
    else expected += 1;
    cleanup.Wait();
  }
  REQUIRE(recycler.recycled == expected);
  REQUIRE(cleanup.completed() == cleanup.started());
  REQUIRE(cleanup.busy_fallbacks() + cleanup.started() >= 50);
  REQUIRE(cleanup.retained_metadata_bytes() == 0);
  // Over-budget batches stay on the caller.
  std::vector<std::unique_ptr<Record>> huge;
  huge.push_back(std::make_unique<Record>(Record{1}));
  REQUIRE_FALSE(cleanup.TryStart(huge, 67108865, recycler));
  REQUIRE(cleanup.budget_fallbacks() == 1);
}
