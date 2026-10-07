#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

enum Stage : size_t {
  Open,
  Stat,
  ReadReserve,
  ReadOverlay,
  ReadAdvise,
  ReadUnmap,
  WriteReserve,
  WriteOverlay,
  WriteTouch,
  WriteAdvise,
  WriteUnmap,
  Close,
  Count
};
constexpr const char *kNames[] = {
    "open",
    "fstat",
    "read-only reserve",
    "read-only mmap",
    "read-only advise",
    "read-only unmap",
    "writable reserve",
    "writable mmap",
    "writable page touch",
    "writable advise",
    "writable unmap",
    "close"};

uint64_t now_ns() {
  timespec ts{};
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0)
    throw std::runtime_error("clock_gettime failed");
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

template <typename F> uint64_t time_call(F &&fn) {
  const uint64_t start = now_ns();
  fn();
  return now_ns() - start;
}

void check(bool ok, const char *operation) {
  if (!ok)
    throw std::runtime_error(std::string(operation) +
                             " failed: " + std::strerror(errno));
}

void summarize(const char *path, size_t iterations,
               const std::array<std::vector<uint64_t>, Count> &samples,
               const std::array<std::vector<long>, Count> &faults) {
  std::printf("%s (%zu iterations)\n", path, iterations);
  for (size_t i = 0; i < Count; ++i) {
    std::vector<uint64_t> sorted = samples[i];
    std::sort(sorted.begin(), sorted.end());
    const uint64_t median = sorted[sorted.size() / 2];
    const uint64_t p90 = sorted[(sorted.size() * 9) / 10];
    std::printf("%-20s median %9.1f us  p90 %9.1f us", kNames[i],
                median / 1000.0, p90 / 1000.0);
    if (!faults[i].empty()) {
      std::vector<long> sorted_faults = faults[i];
      std::sort(sorted_faults.begin(), sorted_faults.end());
      std::printf("  minor faults %ld", sorted_faults[sorted_faults.size() / 2]);
    }
    std::printf("\n");
  }
}

long minor_faults() {
  rusage usage{};
  check(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage");
  return usage.ru_minflt;
}

void run(const char *path, size_t iterations) {
  const long page_size_value = sysconf(_SC_PAGESIZE);
  check(page_size_value > 0, "sysconf(_SC_PAGESIZE)");
  const size_t page_size = static_cast<size_t>(page_size_value);
  const int fd_for_size = open(path, O_RDONLY);
  check(fd_for_size >= 0, "open");
  struct stat file_stat{};
  const int stat_result = fstat(fd_for_size, &file_stat);
  const int close_result = close(fd_for_size);
  check(stat_result == 0, "fstat");
  check(close_result == 0, "close");
  check(file_stat.st_size > 0, "non-empty corpus");
  const size_t file_size = static_cast<size_t>(file_stat.st_size);
  const size_t file_pages = (file_size + page_size - 1) / page_size;
  const size_t map_size = (file_pages + 1) * page_size;

  std::array<std::vector<uint64_t>, Count> samples;
  std::array<std::vector<long>, Count> faults;
  for (auto &stage : samples)
    stage.reserve(iterations);
  for (auto &stage : faults)
    stage.reserve(iterations);

  for (size_t i = 0; i < iterations + 50; ++i) {
    std::array<uint64_t, Count> elapsed{};
    std::array<long, Count> stage_faults{};
    int fd = -1;
    struct stat st{};
    void *base = MAP_FAILED;
    void *file_map = MAP_FAILED;

    elapsed[Open] = time_call([&] {
      fd = open(path, O_RDONLY);
      check(fd >= 0, "open");
    });
    elapsed[Stat] = time_call([&] { check(fstat(fd, &st) == 0, "fstat"); });
    check(st.st_size == file_stat.st_size, "corpus size changed");
    elapsed[ReadReserve] = time_call([&] {
      base = mmap(nullptr, map_size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1,
                  0);
      check(base != MAP_FAILED, "reserve mmap");
    });
    long before = minor_faults();
    elapsed[ReadOverlay] = time_call([&] {
      file_map = mmap(base, file_size, PROT_READ,
                      MAP_PRIVATE | MAP_FIXED | MAP_POPULATE, fd, 0);
      check(file_map == base, "file mmap");
    });
    stage_faults[ReadOverlay] = minor_faults() - before;
    elapsed[ReadAdvise] = time_call([&] {
      check(madvise(base, file_size, MADV_SEQUENTIAL | MADV_WILLNEED) == 0,
            "read-only madvise");
    });
    elapsed[ReadUnmap] =
        time_call([&] { check(munmap(base, map_size) == 0, "read-only munmap"); });

    elapsed[WriteReserve] = time_call([&] {
      base = mmap(nullptr, map_size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1,
                  0);
      check(base != MAP_FAILED, "reserve mmap for writable overlay");
    });
    before = minor_faults();
    elapsed[WriteOverlay] = time_call([&] {
      file_map = mmap(base, file_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_FIXED | MAP_POPULATE, fd, 0);
      check(file_map == base, "writable file mmap");
    });
    stage_faults[WriteOverlay] = minor_faults() - before;
    before = minor_faults();
    elapsed[WriteTouch] = time_call([&] {
      volatile unsigned char *bytes =
          static_cast<volatile unsigned char *>(file_map);
      for (size_t offset = 0; offset < file_size; offset += page_size) {
        const unsigned char value = bytes[offset];
        bytes[offset] = value;
      }
    });
    stage_faults[WriteTouch] = minor_faults() - before;
    elapsed[WriteAdvise] = time_call([&] {
      check(madvise(base, file_size, MADV_SEQUENTIAL | MADV_WILLNEED) == 0,
            "writable madvise");
    });
    elapsed[WriteUnmap] =
        time_call([&] { check(munmap(base, map_size) == 0, "writable munmap"); });
    elapsed[Close] = time_call([&] { check(close(fd) == 0, "close"); });

    if (i >= 50) {
      for (size_t stage = 0; stage < Count; ++stage)
        samples[stage].push_back(elapsed[stage]);
      for (size_t stage = 0; stage < Count; ++stage)
        if (stage == ReadOverlay || stage == WriteOverlay ||
            stage == WriteTouch)
          faults[stage].push_back(stage_faults[stage]);
    }
  }

  summarize(path, iterations, samples, faults);
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    std::fprintf(stderr, "usage: %s CORPUS [ITERATIONS]\n", argv[0]);
    return 2;
  }

  const size_t iterations =
      argc == 3 ? static_cast<size_t>(std::strtoull(argv[2], nullptr, 10))
                : 1000;
  if (iterations == 0) {
    std::fprintf(stderr, "ITERATIONS must be positive\n");
    return 2;
  }

  try {
    run(argv[1], iterations);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
  return 0;
}
