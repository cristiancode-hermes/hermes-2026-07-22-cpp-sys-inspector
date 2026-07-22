#include "sysinspector/sys_info.hpp"

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <iostream>
#include <spdlog/spdlog.h>
#include <thread>

int main(int argc, char** argv) {
  CLI::App app{"sys-inspector — Multi-purpose Linux system inspector CLI"};

  bool flag_json = false;
  int watch_sec = 0;
  bool flag_cpu = false;
  bool flag_mem = false;
  bool flag_disk = false;
  bool flag_uptime = false;

  app.add_flag("-j,--json", flag_json, "Output in JSON format");
  app.add_option("-w,--watch", watch_sec, "Watch mode: refresh every N seconds")
      ->check(CLI::PositiveNumber);
  app.add_flag("--cpu", flag_cpu, "Show CPU info only");
  app.add_flag("--mem", flag_mem, "Show memory info only");
  app.add_flag("--disk", flag_disk, "Show disk I/O info only");
  app.add_flag("--uptime", flag_uptime, "Show uptime/load info only");

  CLI11_PARSE(app, argc, argv);

  auto show = [&] {
    bool all = !flag_cpu && !flag_mem && !flag_disk && !flag_uptime;
    std::string report = sys::format_report(flag_json, all, flag_cpu,
                                             flag_mem, flag_disk, flag_uptime);
    std::cout << report << std::flush;
  };

  if (watch_sec > 0) {
    // Watch mode
    while (true) {
      show();
      std::this_thread::sleep_for(std::chrono::seconds(watch_sec));
    }
  } else {
    show();
  }

  return 0;
}
