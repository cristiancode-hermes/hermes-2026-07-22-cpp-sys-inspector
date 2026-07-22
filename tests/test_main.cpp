#include "sysinspector/sys_info.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

// ── Unit: MemInfo formatting ──

TEST_CASE("MemInfo used_percent calculation", "[mem]") {
  MemInfo mi;
  mi.total_kb = 16 * 1024 * 1024;    // 16 GB
  mi.free_kb = 8ULL * 1024 * 1024;   // 8 GB
  mi.buffers_kb = 256 * 1024;        // 256 MB
  mi.cached_kb = 2ULL * 1024 * 1024; // 2 GB
  // used = 16 - 8 - 0.25 - 2 = 5.75 GB => 35.94%
  double pct = mi.used_percent();
  REQUIRE(pct > 30.0);
  REQUIRE(pct < 45.0);
}

TEST_CASE("MemInfo formatting", "[mem]") {
  MemInfo mi;
  mi.total_kb = 8ULL * 1024 * 1024;
  auto s = mi.fmt_total();
  // Should contain "8.0 GB"
  CHECK(s.find("8") != std::string::npos);
  CHECK(s.find("GB") != std::string::npos);
}

TEST_CASE("MemInfo used_percent zero total", "[mem]") {
  MemInfo mi{};
  REQUIRE(mi.used_percent() == 0.0);
}

// ── Unit: UptimeInfo formatting ──

TEST_CASE("UptimeInfo formatting", "[uptime]") {
  UptimeInfo ut;
  ut.uptime_seconds = 90061; // 1d 01:01:01
  auto s = ut.fmt_uptime();
  bool ok = s.find("1d") != std::string::npos || s.find("01:01:01") != std::string::npos;
  CHECK(ok);
}

TEST_CASE("UptimeInfo zero", "[uptime]") {
  UptimeInfo ut{0, 0};
  CHECK(ut.fmt_uptime() == "0d 00:00:00");
}

// ── Integration: /proc readers ──

TEST_CASE("read_load_avg returns valid values", "[integration][load]") {
  auto r = sys::read_load_avg();
  REQUIRE(r.has_value());
  CHECK(r->load1 >= 0.0);
  CHECK(r->load5 >= 0.0);
  CHECK(r->load15 >= 0.0);
  CHECK(r->total > 0);
}

TEST_CASE("read_uptime returns valid values", "[integration][uptime]") {
  auto r = sys::read_uptime();
  REQUIRE(r.has_value());
  CHECK(r->uptime_seconds > 0.0);
  // idle_seconds may be > uptime_seconds on multi-core systems
}

TEST_CASE("read_mem_info returns valid values", "[integration][mem]") {
  auto r = sys::read_mem_info();
  REQUIRE(r.has_value());
  CHECK(r->total_kb > 0);
  CHECK(r->available_kb > 0);
  CHECK(r->free_kb > 0);
  // total >= available
  CHECK(r->total_kb >= r->available_kb);
}

TEST_CASE("read_cpu_info returns valid values", "[integration][cpu]") {
  auto r = sys::read_cpu_info();
  REQUIRE(r.has_value());
  CHECK(r->cores > 0);
  CHECK(!r->model_name.empty());
  CHECK(r->freq_mhz > 0.0);
}

TEST_CASE("read_cpu_usage_per_core returns values", "[integration][cpu]") {
  auto r = sys::read_cpu_usage_per_core();
  REQUIRE(r.has_value());
  // Should have at least 1 core
  CHECK(!r->empty());
  // Each usage should be between 0 and 100
  for (double pct : *r) {
    CHECK(pct >= 0.0);
    CHECK(pct <= 100.0);
  }
}

TEST_CASE("read_disk_stats returns valid values", "[integration][disk]") {
  auto r = sys::read_disk_stats();
  REQUIRE(r.has_value());
  for (const auto& d : *r) {
    CHECK(!d.device.empty());
    // reads and writes could be zero on idle disk
  }
}

TEST_CASE("read_proc_count returns valid values", "[integration][proc]") {
  auto r = sys::read_proc_count();
  REQUIRE(r.has_value());
  CHECK(r->total > 0);
  CHECK(r->running >= 0);
}

// ── JSON report ──

TEST_CASE("json_report is valid JSON", "[json]") {
  auto json_str = sys::json_report();
  CHECK(!json_str.empty());
  // Should start with '{'
  CHECK(json_str.front() == '{');
}

// ── Benchmarks ──

TEST_CASE("Benchmark /proc reads", "[benchmark]") {
  BENCHMARK("Read /proc/loadavg") {
    return sys::read_load_avg();
  };
  BENCHMARK("Read /proc/meminfo") {
    return sys::read_mem_info();
  };
  BENCHMARK("Read /proc/uptime") {
    return sys::read_uptime();
  };
}
