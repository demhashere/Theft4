/**
 * @file wait_multiple_test.cpp
 * @brief Notification-based WaitMultiple (THEFT4_RUNTIME_WAIT_FIXES) tests.
 */

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <rex/thread.h>
#include <rex/thread/runtime_wait_policy.h>

using namespace std::chrono_literals;
namespace thread = rex::thread;

namespace {
// The policy latches once per process; every case in this file runs corrected.
void UseCorrectedWaits() {
  REQUIRE(thread::ConfigureRuntimeWaitFixes(true));
  REQUIRE(thread::RuntimeWaitFixesEnabled());
}
}  // namespace

TEST_CASE("Corrected WaitAny wakes on a later signal without polling", "[thread][wait-fixes]") {
  UseCorrectedWaits();
  auto a = thread::Event::CreateAutoResetEvent(false);
  auto b = thread::Event::CreateAutoResetEvent(false);
  std::vector<thread::WaitHandle*> handles{a.get(), b.get()};
  REQUIRE(thread::WaitAny(handles, false, 0ms).first == thread::WaitResult::kTimeout);
  std::thread signaler([&] { std::this_thread::sleep_for(20ms); b->Set(); });
  const auto begin = std::chrono::steady_clock::now();
  const auto result = thread::WaitAny(handles, false, 2000ms);
  signaler.join();
  REQUIRE(result.first == thread::WaitResult::kSuccess);
  REQUIRE(result.second == 1);
  REQUIRE(std::chrono::steady_clock::now() - begin < 1500ms);
  // Auto-reset: consumed by the successful wait.
  REQUIRE(thread::WaitAny(handles, false, 0ms).first == thread::WaitResult::kTimeout);
}

TEST_CASE("Corrected WaitAll needs every handle and consumes all", "[thread][wait-fixes]") {
  UseCorrectedWaits();
  auto a = thread::Event::CreateAutoResetEvent(false);
  auto s = thread::Semaphore::Create(0, 4);
  std::vector<thread::WaitHandle*> handles{a.get(), s.get()};
  a->Set();
  REQUIRE(thread::WaitAll(handles, false, 10ms) == thread::WaitResult::kTimeout);
  std::thread signaler([&] { std::this_thread::sleep_for(10ms); s->Release(1, nullptr); });
  REQUIRE(thread::WaitAll(handles, false, 2000ms) == thread::WaitResult::kSuccess);
  signaler.join();
  REQUIRE(thread::WaitAny(handles, false, 0ms).first == thread::WaitResult::kTimeout);
}

TEST_CASE("Corrected WaitAny honours timeouts and duplicate handles", "[thread][wait-fixes]") {
  UseCorrectedWaits();
  auto a = thread::Event::CreateManualResetEvent(false);
  auto b = thread::Event::CreateManualResetEvent(false);
  std::vector<thread::WaitHandle*> handles{a.get(), b.get()};
  const auto begin = std::chrono::steady_clock::now();
  REQUIRE(thread::WaitAny(handles, false, 30ms).first == thread::WaitResult::kTimeout);
  REQUIRE(std::chrono::steady_clock::now() - begin >= 30ms);
  // NT accepts duplicate objects for WaitAny; WaitAll rejects them.
  std::vector<thread::WaitHandle*> duplicate{a.get(), a.get(), b.get()};
  REQUIRE(thread::WaitAny(duplicate, false, 0ms).first == thread::WaitResult::kTimeout);
  REQUIRE(thread::WaitAll(duplicate, false, 0ms) == thread::WaitResult::kFailed);
  a->Set();
  REQUIRE(thread::WaitAny(duplicate, false, 0ms).second == 0);
  // Manual reset stays signaled; the first signaled handle in caller order wins.
  b->Set();
  REQUIRE(thread::WaitAny(handles, false, 0ms).second == 0);
  REQUIRE(thread::WaitAny(handles, false, 0ms).second == 0);
}

TEST_CASE("Corrected waits lose no wakeups under contention", "[thread][wait-fixes]") {
  UseCorrectedWaits();
  constexpr int kRounds = 5000;
  auto work = thread::Semaphore::Create(0, kRounds);
  auto stop = thread::Event::CreateManualResetEvent(false);
  std::atomic<int> consumed{0};
  std::atomic<bool> failed{false};
  std::vector<std::thread> consumers;
  for (int i = 0; i < 3; ++i) {
    consumers.emplace_back([&] {
      std::vector<thread::WaitHandle*> handles{work.get(), stop.get()};
      for (;;) {
        const auto result = thread::WaitAny(handles, false, 5000ms);
        if (result.first != thread::WaitResult::kSuccess) { failed = true; return; }
        if (result.second == 1) return;
        ++consumed;
      }
    });
  }
  for (int i = 0; i < kRounds; ++i) {
    REQUIRE(work->Release(1, nullptr));
    if (i % 64 == 0) std::this_thread::yield();
  }
  // Every release is consumed before stop: work is first in caller order.
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (consumed.load() < kRounds && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  stop->Set();
  for (auto& consumer : consumers) consumer.join();
  REQUIRE_FALSE(failed.load());
  REQUIRE(consumed.load() == kRounds);
}

TEST_CASE("Corrected waits acquire a released mutant", "[thread][wait-fixes]") {
  UseCorrectedWaits();
  auto mutant = thread::Mutant::Create(false);
  auto never = thread::Event::CreateManualResetEvent(false);
  // Catch2 assertions stay on the test thread; workers report through flags.
  std::atomic<bool> held{false}, release{false}, owner_ok{true};
  std::thread owner([&] {
    std::vector<thread::WaitHandle*> handles{never.get(), mutant.get()};
    owner_ok = thread::WaitAny(handles, false, 1000ms).second == 1;
    held = true;
    while (!release) std::this_thread::sleep_for(1ms);
    owner_ok = owner_ok && mutant->Release();
  });
  while (!held) std::this_thread::sleep_for(1ms);
  std::vector<thread::WaitHandle*> handles{never.get(), mutant.get()};
  REQUIRE(thread::WaitAny(handles, false, 20ms).first == thread::WaitResult::kTimeout);
  std::thread releaser([&] { std::this_thread::sleep_for(10ms); release = true; });
  const auto result = thread::WaitAny(handles, false, 2000ms);
  releaser.join();
  owner.join();
  REQUIRE(owner_ok.load());
  REQUIRE(result.first == thread::WaitResult::kSuccess);
  REQUIRE(result.second == 1);
  REQUIRE(mutant->Release());
  REQUIRE_FALSE(mutant->Release());
}

TEST_CASE("Corrected zero-timeout polls see a signaled object despite contention",
          "[thread][wait-fixes]") {
  UseCorrectedWaits();
  // A signaler briefly holding an object mutex is not a timeout.
  auto ready = thread::Event::CreateManualResetEvent(true);
  auto busy = thread::Event::CreateManualResetEvent(false);
  std::atomic<bool> stop{false};
  std::thread signaler([&] {
    while (!stop) { busy->Set(); busy->Reset(); }
  });
  std::vector<thread::WaitHandle*> handles{ready.get(), busy.get()};
  int timeouts = 0;
  for (int i = 0; i < 100000; ++i) {
    timeouts += thread::WaitAny(handles, false, 0ms).first == thread::WaitResult::kTimeout;
  }
  stop = true;
  signaler.join();
  REQUIRE(timeouts == 0);
}
