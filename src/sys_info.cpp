#include "sysinspector/sys_info.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <spdlog/spdlog.h>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ── Helpers ──

static std::string trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r'))
    s.remove_suffix(1);
  return std::string(s);
}

static Result<std::string> read_file(std::string_view path) {
  std::ifstream f(path.data());
  if (!f.is_open()) {
    return std::unexpected(Error{1, std::string("Cannot open ") + path.data()});
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

template <typename T>
static T kv_parse(const std::string& line) {
  auto colon = line.find(':');
  if (colon == std::string::npos) return T{};
  std::string val = trim(line.substr(colon + 1));
  if constexpr (std::is_same_v<T, unsigned long long>) {
    return std::stoull(val);
  } else if constexpr (std::is_same_v<T, double>) {
    return std::stod(val);
  } else if constexpr (std::is_same_v<T, int>) {
    return std::stoi(val);
  } else {
    return T(val);
  }
}

// ── MemInfo ──

double MemInfo::used_percent() const {
  if (total_kb == 0) return 0.0;
  double used = static_cast<double>(total_kb - free_kb - buffers_kb - cached_kb);
  return (used / total_kb) * 100.0;
}

std::string MemInfo::fmt_total() const {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f GB", total_kb / (1024.0 * 1024.0));
  return buf;
}

std::string MemInfo::fmt_used() const {
  double used = static_cast<double>(total_kb - free_kb - buffers_kb - cached_kb);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f GB", used / (1024.0 * 1024.0));
  return buf;
}

std::string MemInfo::fmt_available() const {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f GB", available_kb / (1024.0 * 1024.0));
  return buf;
}

// ── UptimeInfo ──

std::string UptimeInfo::fmt_uptime() const {
  int days = static_cast<int>(uptime_seconds) / 86400;
  int hours = (static_cast<int>(uptime_seconds) % 86400) / 3600;
  int mins = (static_cast<int>(uptime_seconds) % 3600) / 60;
  int secs = static_cast<int>(uptime_seconds) % 60;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%dd %02d:%02d:%02d", days, hours, mins, secs);
  return buf;
}

// ── CPU Info ──

Result<CpuInfo> sys::read_cpu_info() {
  auto content = read_file("/proc/cpuinfo");
  if (!content) return std::unexpected(content.error());

  CpuInfo info{};
  info.cores = 0;
  std::istringstream ss(*content);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.starts_with("model name")) {
      if (info.model_name.empty())
        info.model_name = kv_parse<std::string>(line);
    }
    if (line.starts_with("processor")) {
      info.cores++;
    }
    if (line.starts_with("cpu MHz")) {
      double mhz = kv_parse<double>(line);
      if (mhz > info.freq_mhz) info.freq_mhz = mhz;
    }
  }

  // Read /proc/stat for usage
  auto stat_content = read_file("/proc/stat");
  if (stat_content) {
    std::istringstream ss2(*stat_content);
    std::getline(ss2, line); // "cpu  ..."
    unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0;
    std::sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu",
                &user, &nice, &sys, &idle, &iowait);
    unsigned long long total = user + nice + sys + idle + iowait;
    // Sleep 100ms and read again for delta
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto stat2 = read_file("/proc/stat");
    if (stat2) {
      std::istringstream ss3(*stat2);
      std::getline(ss3, line);
      unsigned long long user2 = 0, nice2 = 0, sys2 = 0, idle2 = 0, iowait2 = 0;
      std::sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu",
                  &user2, &nice2, &sys2, &idle2, &iowait2);
      unsigned long long total2 = user2 + nice2 + sys2 + idle2 + iowait2;
      unsigned long long dtotal = total2 - total;
      unsigned long long didle = idle2 - idle;
      if (dtotal > 0) {
        info.usage_percent = 100.0 * (1.0 - static_cast<double>(didle) / dtotal);
      }
    }
  }

  return info;
}

Result<std::vector<double>> sys::read_cpu_usage_per_core() {
  auto content = read_file("/proc/stat");
  if (!content) return std::unexpected(content.error());

  std::vector<double> percents;
  std::istringstream ss(*content);
  std::string line;

  // Read first snapshot
  std::vector<std::array<unsigned long long, 5>> snap1;
  while (std::getline(ss, line)) {
    if (!line.starts_with("cpu") || line.size() < 4 || !std::isdigit(line[3]))
      continue;
    std::array<unsigned long long, 5> vals{};
    std::sscanf(line.c_str(), "cpu%*d %llu %llu %llu %llu %llu",
                &vals[0], &vals[1], &vals[2], &vals[3], &vals[4]);
    snap1.push_back(vals);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto content2 = read_file("/proc/stat");
  if (!content2) return std::unexpected(content2.error());

  std::istringstream ss2(*content2);
  size_t idx = 0;
  while (std::getline(ss2, line)) {
    if (!line.starts_with("cpu") || line.size() < 4 || !std::isdigit(line[3]))
      continue;
    if (idx >= snap1.size()) break;
    std::array<unsigned long long, 5> vals{};
    std::sscanf(line.c_str(), "cpu%*d %llu %llu %llu %llu %llu",
                &vals[0], &vals[1], &vals[2], &vals[3], &vals[4]);
    auto& p = snap1[idx];
    unsigned long long dtotal = (vals[0] - p[0]) + (vals[1] - p[1]) + (vals[2] - p[2])
                               + (vals[3] - p[3]) + (vals[4] - p[4]);
    unsigned long long didle = vals[3] - p[3];
    if (dtotal > 0)
      percents.push_back(100.0 * (1.0 - static_cast<double>(didle) / dtotal));
    idx++;
  }

  return percents;
}

// ── Memory Info ──

Result<MemInfo> sys::read_mem_info() {
  auto content = read_file("/proc/meminfo");
  if (!content) return std::unexpected(content.error());

  MemInfo info{};
  std::istringstream ss(*content);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.starts_with("MemTotal:"))
      info.total_kb = kv_parse<unsigned long long>(line);
    else if (line.starts_with("MemFree:"))
      info.free_kb = kv_parse<unsigned long long>(line);
    else if (line.starts_with("MemAvailable:"))
      info.available_kb = kv_parse<unsigned long long>(line);
    else if (line.starts_with("Buffers:"))
      info.buffers_kb = kv_parse<unsigned long long>(line);
    else if (line.starts_with("Cached:"))
      info.cached_kb = kv_parse<unsigned long long>(line);
    else if (line.starts_with("SwapTotal:"))
      info.swap_total_kb = kv_parse<unsigned long long>(line);
    else if (line.starts_with("SwapFree:"))
      info.swap_free_kb = kv_parse<unsigned long long>(line);
  }

  return info;
}

// ── Disk Stats ──

Result<std::vector<DiskStat>> sys::read_disk_stats() {
  auto content = read_file("/proc/diskstats");
  if (!content) return std::unexpected(content.error());

  std::vector<DiskStat> stats;
  std::istringstream ss(*content);
  std::string line;
  while (std::getline(ss, line)) {
    DiskStat ds{};
    int major = 0, minor = 0;
    char dev[64]{};
    int parsed = std::sscanf(line.c_str(), "%d %d %63s %llu %*u %llu %*u %llu %*u %llu %*u %llu",
                             &major, &minor, dev,
                             &ds.reads, &ds.read_sectors,
                             &ds.writes, &ds.write_sectors,
                             &ds.io_ms);
    if (parsed >= 8) {
      ds.device = dev;
      // Filter physical devices (sdX, nvmeXnY, vdX)
      if ((ds.device.starts_with("sd") && ds.device.size() <= 3) ||
          (ds.device.starts_with("nvme") && ds.device.find('n') == 4) ||
          (ds.device.starts_with("vd") && ds.device.size() <= 3)) {
        stats.push_back(std::move(ds));
      }
    }
  }

  return stats;
}

// ── Load Avg ──

Result<LoadInfo> sys::read_load_avg() {
  auto content = read_file("/proc/loadavg");
  if (!content) return std::unexpected(content.error());

  LoadInfo info{};
  std::sscanf(content->c_str(), "%lf %lf %lf %d/%d",
              &info.load1, &info.load5, &info.load15,
              &info.running, &info.total);
  return info;
}

// ── Uptime ──

Result<UptimeInfo> sys::read_uptime() {
  auto content = read_file("/proc/uptime");
  if (!content) return std::unexpected(content.error());

  UptimeInfo info{};
  std::sscanf(content->c_str(), "%lf %lf",
              &info.uptime_seconds, &info.idle_seconds);
  return info;
}

// ── Process Count ──

Result<ProcCount> sys::read_proc_count() {
  ProcCount pc{0, 0, 0};
  try {
    for (const auto& entry : fs::directory_iterator("/proc")) {
      if (!entry.is_directory()) continue;
      std::string name = entry.path().filename().string();
      if (name.empty() || !std::all_of(name.begin(), name.end(),
                                        [](char c) { return std::isdigit(c); }))
        continue;
      pc.total++;
      std::ifstream stat(entry.path() / "stat");
      if (!stat.is_open()) continue;
      std::string line;
      std::getline(stat, line);
      // Format: pid (comm) state ...
      auto close_paren = line.rfind(')');
      if (close_paren == std::string::npos) continue;
      if (close_paren + 2 >= line.size()) continue;
      char state = line[close_paren + 2];
      if (state == 'R') pc.running++;
      if (state == 'D') pc.blocked++;
    }
  } catch (const std::exception& e) {
    spdlog::warn("Error reading /proc: {}", e.what());
  }
  return pc;
}

// ── Format helpers ──

static std::string pct_bar(double pct, int width = 20) {
  int filled = static_cast<int>(pct / 100.0 * width);
  if (filled < 0) filled = 0;
  if (filled > width) filled = width;
  std::string bar = "[";
  bar.append(filled, '#');
  bar.append(width - filled, '.');
  bar.append("]");
  return bar;
}

static std::string fmt_load_bar(double loadv, int cores, int width = 20) {
  double pct = (loadv / cores) * 100.0;
  return pct_bar(pct, width);
}

// ── Report ──

std::string sys::format_report(bool json_output, bool all, bool cpu,
                                bool mem, bool disk, bool uptime) {
  if (json_output) {
    return json_report();
  }

  std::ostringstream out;

  if (all || uptime) {
    auto ut = sys::read_uptime();
    auto ld = sys::read_load_avg();
    auto pc = sys::read_proc_count();
    auto ci = sys::read_cpu_info();

    out << "╔══════════════════════════════════════════╗\n";
    out << "║      System Inspector — sys-inspector    ║\n";
    out << "╚══════════════════════════════════════════╝\n\n";

    if (ut) {
      out << "  Uptime   : " << ut->fmt_uptime() << "\n";
    }
    if (ci) {
      out << "  Host     : " << ci->cores << " cores @ "
          << std::fixed << std::setprecision(1) << ci->freq_mhz << " MHz\n";
    }
    if (ld) {
      out << "  Load     : " << fmt_load_bar(ld->load1, ci ? ci->cores : 1)
          << "  " << std::fixed << std::setprecision(2) << ld->load1
          << " (1m)  " << ld->load5 << " (5m)  " << ld->load15 << " (15m)\n";
    }
    if (pc) {
      out << "  Processes: " << pc->total << " total, "
          << pc->running << " running, " << pc->blocked << " blocked\n";
    }
    out << "\n";
  }

  if (all || cpu) {
    auto ci = sys::read_cpu_info();
    if (ci) {
      out << "── CPU ────────────────────────────────────\n";
      out << "  Model    : " << ci->model_name << "\n";
      out << "  Cores    : " << ci->cores << "\n";
      out << "  Freq     : " << std::fixed << std::setprecision(1)
          << ci->freq_mhz << " MHz\n";
      out << "  Usage    : " << pct_bar(ci->usage_percent)
          << "  " << std::fixed << std::setprecision(1)
          << ci->usage_percent << "%\n";

      auto per_core = sys::read_cpu_usage_per_core();
      if (per_core && !per_core->empty()) {
        for (size_t i = 0; i < per_core->size(); ++i) {
          out << "  CPU" << i << "     : " << pct_bar((*per_core)[i])
              << "  " << std::fixed << std::setprecision(1)
              << (*per_core)[i] << "%\n";
        }
      }
      out << "\n";
    }
  }

  if (all || mem) {
    auto mi = sys::read_mem_info();
    if (mi) {
      out << "── Memory ────────────────────────────────\n";
      out << "  Total    : " << mi->fmt_total() << "\n";
      out << "  Used     : " << mi->fmt_used()
          << "  " << pct_bar(mi->used_percent())
          << "  " << std::fixed << std::setprecision(1)
          << mi->used_percent() << "%\n";
      out << "  Available: " << mi->fmt_available() << "\n";
      out << "  Buffers  : " << std::fixed << std::setprecision(1)
          << mi->buffers_kb / 1024.0 << " MB\n";
      out << "  Cached   : " << std::fixed << std::setprecision(1)
          << mi->cached_kb / 1024.0 << " MB\n";
      if (mi->swap_total_kb > 0) {
        double swap_used_pct = 100.0 * (1.0 - static_cast<double>(mi->swap_free_kb) / mi->swap_total_kb);
        out << "  Swap     : " << std::fixed << std::setprecision(1)
            << mi->swap_total_kb / (1024.0 * 1024.0) << " GB total, "
            << std::fixed << std::setprecision(1)
            << mi->swap_free_kb / (1024.0 * 1024.0) << " GB free"
            << "  " << pct_bar(swap_used_pct)
            << "  " << std::fixed << std::setprecision(1) << swap_used_pct << "%\n";
      }
      out << "\n";
    }
  }

  if (all || disk) {
    auto ds = sys::read_disk_stats();
    if (ds) {
      out << "── Disk I/O ──────────────────────────────\n";
      for (const auto& d : *ds) {
        out << "  " << std::left << std::setw(8) << d.device
            << "  R:" << std::right << std::setw(8) << d.reads
            << "  W:" << std::right << std::setw(8) << d.writes
            << "  IOms:" << d.io_ms << "\n";
      }
      out << "\n";
    }
  }

  return out.str();
}

std::string sys::json_report() {
  json j;

  if (auto ci = sys::read_cpu_info()) {
    j["cpu"]["model"] = ci->model_name;
    j["cpu"]["cores"] = ci->cores;
    j["cpu"]["freq_mhz"] = ci->freq_mhz;
    j["cpu"]["usage_percent"] = ci->usage_percent;
    auto per_core = sys::read_cpu_usage_per_core();
    if (per_core) {
      j["cpu"]["per_core"] = *per_core;
    }
  }

  if (auto mi = sys::read_mem_info()) {
    j["memory"]["total_kb"] = mi->total_kb;
    j["memory"]["free_kb"] = mi->free_kb;
    j["memory"]["available_kb"] = mi->available_kb;
    j["memory"]["used_percent"] = mi->used_percent();
    j["memory"]["swap_total_kb"] = mi->swap_total_kb;
    j["memory"]["swap_free_kb"] = mi->swap_free_kb;
  }

  if (auto ds = sys::read_disk_stats()) {
    auto& arr = j["disk"] = json::array();
    for (const auto& d : *ds) {
      arr.push_back({
        {"device", d.device},
        {"reads", d.reads},
        {"writes", d.writes},
        {"read_sectors", d.read_sectors},
        {"write_sectors", d.write_sectors},
        {"io_ms", d.io_ms}
      });
    }
  }

  if (auto ld = sys::read_load_avg()) {
    j["load"]["load1"] = ld->load1;
    j["load"]["load5"] = ld->load5;
    j["load"]["load15"] = ld->load15;
    j["load"]["running"] = ld->running;
    j["load"]["total"] = ld->total;
  }

  if (auto ut = sys::read_uptime()) {
    j["uptime"]["seconds"] = ut->uptime_seconds;
    j["uptime"]["formatted"] = ut->fmt_uptime();
  }

  if (auto pc = sys::read_proc_count()) {
    j["processes"]["total"] = pc->total;
    j["processes"]["running"] = pc->running;
    j["processes"]["blocked"] = pc->blocked;
  }

  return j.dump(2);
}
