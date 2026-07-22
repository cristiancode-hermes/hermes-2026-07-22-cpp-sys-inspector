#pragma once

#include <chrono>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

struct Error {
  int code;
  std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

// ── Data structures ──

struct CpuInfo {
  std::string model_name;
  int cores;
  double freq_mhz;
  double usage_percent;
};

struct MemInfo {
  unsigned long long total_kb;
  unsigned long long free_kb;
  unsigned long long available_kb;
  unsigned long long buffers_kb;
  unsigned long long cached_kb;
  unsigned long long swap_total_kb;
  unsigned long long swap_free_kb;

  [[nodiscard]] double used_percent() const;
  [[nodiscard]] std::string fmt_total() const;
  [[nodiscard]] std::string fmt_used() const;
  [[nodiscard]] std::string fmt_available() const;
};

struct DiskStat {
  std::string device;
  unsigned long long reads;
  unsigned long long writes;
  unsigned long long read_sectors;
  unsigned long long write_sectors;
  unsigned long io_ms;
};

struct LoadInfo {
  double load1;
  double load5;
  double load15;
  int running;
  int total;
};

struct UptimeInfo {
  double uptime_seconds;
  double idle_seconds;

  [[nodiscard]] std::string fmt_uptime() const;
};

struct ProcCount {
  int total;
  int running;
  int blocked;
};

// ── Readers ──

namespace sys {

Result<CpuInfo> read_cpu_info();
Result<std::vector<double>> read_cpu_usage_per_core();
Result<MemInfo> read_mem_info();
Result<std::vector<DiskStat>> read_disk_stats();
Result<LoadInfo> read_load_avg();
Result<UptimeInfo> read_uptime();
Result<ProcCount> read_proc_count();

// Report builder
std::string format_report(bool json, bool all, bool cpu, bool mem,
                          bool disk, bool uptime);
std::string json_report();

} // namespace sys
