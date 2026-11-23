/***************************************************************************
 * How many CPUs this process may actually use.
 *
 * std::thread::hardware_concurrency() answers "how many does the machine
 * have", which is the wrong number wherever a process is confined to fewer: a
 * Slurm job given 4 of a node's 64 cores (its cgroup cpuset narrows the CPU
 * affinity mask), a container started with --cpus=2 (a cgroup quota), or a run
 * under taskset. Sizing a thread pool from it starts 64 threads on 4 cores.
 *
 * UsableCpuCount() is the smaller of
 *   - the CPU affinity mask (Linux sched_getaffinity, Windows
 *     GetProcessAffinityMask), and
 *   - the cgroup CPU quota, rounded up (cgroup v2 cpu.max, v1 cfs_quota/period),
 * never below 1, and never above hardware_concurrency(). OBJECK_CPU_COUNT=<n>
 * overrides it (a diagnostic, and a way to ask for fewer on a shared box).
 * The answer is computed once per process.
 ***************************************************************************/

#ifndef __OBJECK_CPU_COUNT_H__
#define __OBJECK_CPU_COUNT_H__

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sched.h>
#endif

namespace objeck_cpu {

#ifdef __linux__
// the quota in CPUs from a cgroup file, or 0 when there is none ("max")
inline unsigned CgroupQuotaCpus() {
  // cgroup v2: "<quota|max> <period>"
  if(FILE* f = std::fopen("/sys/fs/cgroup/cpu.max", "r")) {
    char quota[32] = {0};
    long long period = 0;
    const int fields = std::fscanf(f, "%31s %lld", quota, &period);
    std::fclose(f);
    if(fields == 2 && quota[0] != 'm' && period > 0) {
      const long long q = std::atoll(quota);
      if(q > 0) {
        return (unsigned)((q + period - 1) / period);
      }
    }
    return 0;
  }
  // cgroup v1: two files, a quota of -1 meaning none
  long long quota = -1, period = 0;
  if(FILE* f = std::fopen("/sys/fs/cgroup/cpu/cpu.cfs_quota_us", "r")) {
    if(std::fscanf(f, "%lld", &quota) != 1) {
      quota = -1;
    }
    std::fclose(f);
  }
  if(FILE* f = std::fopen("/sys/fs/cgroup/cpu/cpu.cfs_period_us", "r")) {
    if(std::fscanf(f, "%lld", &period) != 1) {
      period = 0;
    }
    std::fclose(f);
  }
  if(quota > 0 && period > 0) {
    return (unsigned)((quota + period - 1) / period);
  }
  return 0;
}
#endif

inline unsigned Compute() {
  unsigned hw = std::thread::hardware_concurrency();
  if(hw == 0) {
    hw = 1;
  }
  if(const char* env = std::getenv("OBJECK_CPU_COUNT")) {
    const long n = std::atol(env);
    if(n >= 1) {
      return (unsigned)n;
    }
  }
  unsigned usable = hw;
#ifdef __linux__
  cpu_set_t set;
  CPU_ZERO(&set);
  if(sched_getaffinity(0, sizeof(set), &set) == 0) {
    const int n = CPU_COUNT(&set);
    if(n >= 1) {
      usable = (std::min)(usable, (unsigned)n);
    }
  }
  const unsigned quota = CgroupQuotaCpus();
  if(quota >= 1) {
    usable = (std::min)(usable, quota);
  }
#elif defined(_WIN32)
  DWORD_PTR process_mask = 0, system_mask = 0;
  // the masks cover one processor group (64 CPUs); only a mask narrower than the
  // group's own says the process was restricted, else a 128-CPU box would read 64
  if(GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) && process_mask &&
     process_mask != system_mask) {
    unsigned bits = 0;
    for(DWORD_PTR m = process_mask; m; m &= (m - 1)) {
      ++bits;
    }
    usable = (std::min)(usable, bits);
  }
#endif
  return (std::max)(1u, usable);
}

}  // namespace objeck_cpu

// computed once, on first use
inline unsigned UsableCpuCount() {
  static const unsigned count = objeck_cpu::Compute();
  return count;
}

#endif
