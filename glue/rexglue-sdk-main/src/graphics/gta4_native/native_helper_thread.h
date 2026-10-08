#pragma once
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <system_error>
#include <thread>
#if defined(__linux__)
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace rex::graphics::gta4_native {

// Portable stand-in for a GCD serial queue plus group: one persistent thread
// per owner and one job in flight. Start publishes the job under the mutex;
// Wait/TryJoin observe completion under the same mutex, which orders every
// write the job made before the owner reads its results. No spinning, no
// per-draw dispatch: owners start at most one batch at a time.
class NativeHelperThread {
 public:
  using Function = void (*)(void*);
  NativeHelperThread() = default;
  NativeHelperThread(const NativeHelperThread&) = delete;
  NativeHelperThread& operator=(const NativeHelperThread&) = delete;
  ~NativeHelperThread() { Shutdown(); }

  // nice_increment > 0 lowers the helper below the render worker (Linux
  // per-thread nice), the analogue of GCD's utility QoS.
  bool Launch(const char* name, int nice_increment = 0) {
    if (thread_.joinable()) return true;
    try {
      thread_ = std::thread([this, nice_increment] { Main(nice_increment); });
    } catch (const std::system_error&) {
      return false;
    }
#if defined(__linux__)
    // Thread names are limited to 15 characters plus the terminator.
    char short_name[16] = {};
    const char* source = name ? name : "Theft4 helper";
    std::memcpy(short_name, source, std::min(std::strlen(source), sizeof(short_name) - 1));
    pthread_setname_np(thread_.native_handle(), short_name);
#else
    (void)name;
#endif
    return true;
  }
  bool launched() const { return thread_.joinable(); }

  bool Start(void* context, Function function) {
    if (!thread_.joinable() || !function) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (job_ || stop_) return false;
    context_ = context;
    function_ = function;
    job_ = true;
    done_ = false;
    condition_.notify_all();
    return true;
  }
  // True once the started job has finished (or nothing is in flight).
  bool TryJoin() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!job_) return true;
    if (!done_) return false;
    job_ = false;
    return true;
  }
  void Join() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!job_) return;
    condition_.wait(lock, [this] { return done_; });
    job_ = false;
  }
  void Shutdown() {
    if (!thread_.joinable()) return;
    Join();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
      condition_.notify_all();
    }
    thread_.join();
  }

  static uint32_t OnlineCpus() {
#if defined(__linux__)
    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online > 0) return uint32_t(std::min<long>(online, 1024));
#endif
    return std::thread::hardware_concurrency();
  }

#if defined(__linux__)
  // THEFT4_HELPER_AFFINITY=performance pins helpers to the highest-capacity
  // cores (P-cores on Apple Silicon under Asahi). Energy-aware scheduling
  // otherwise tends to place a briefly busy helper on an E-core, where a job
  // the render worker joins can take longer than the work it saves.
  static bool PinToPerformanceCores() {
    const char* setting = std::getenv("THEFT4_HELPER_AFFINITY");
    if (!setting || std::strcmp(setting, "performance") != 0) return false;
    uint32_t capacities[CPU_SETSIZE] = {};
    uint32_t highest = 0, lowest = UINT32_MAX, count = 0;
    for (uint32_t cpu = 0; cpu < CPU_SETSIZE; ++cpu, ++count) {
      char path[64], text[32] = {};
      std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%u/cpu_capacity", cpu);
      const int fd = open(path, O_RDONLY | O_CLOEXEC);
      if (fd < 0) break;
      const ssize_t size = read(fd, text, sizeof(text) - 1);
      close(fd);
      if (size <= 0) break;
      capacities[cpu] = uint32_t(std::strtoul(text, nullptr, 10));
      highest = std::max(highest, capacities[cpu]);
      lowest = std::min(lowest, capacities[cpu]);
    }
    if (!count || highest == lowest) return false;  // symmetric or unknown topology
    cpu_set_t set;
    CPU_ZERO(&set);
    for (uint32_t cpu = 0; cpu < count; ++cpu)
      if (capacities[cpu] == highest) CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;
  }
#endif

 private:
  void Main(int nice_increment) {
#if defined(__linux__)
    if (nice_increment > 0) {
      const auto tid = id_t(syscall(SYS_gettid));
      setpriority(PRIO_PROCESS, tid, getpriority(PRIO_PROCESS, tid) + nice_increment);
    }
    // Low-priority helpers (cleanup) stay wherever the scheduler puts them.
    if (nice_increment <= 0) PinToPerformanceCores();
#else
    (void)nice_increment;
#endif
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      condition_.wait(lock, [this] { return stop_ || (job_ && !done_); });
      if (stop_) return;
      const Function function = function_;
      void* context = context_;
      lock.unlock();
      function(context);
      lock.lock();
      done_ = true;
      condition_.notify_all();
    }
  }

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable condition_;
  Function function_ = nullptr;
  void* context_ = nullptr;
  bool job_ = false;
  bool done_ = false;
  bool stop_ = false;
};

}  // namespace rex::graphics::gta4_native
