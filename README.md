# sys-inspector

**Multi-purpose Linux system inspector CLI** — C++23, CLI11, nlohmann_json, spdlog, Catch2

## Features

- **CPU info**: model name, core count, frequency, usage % per-core
- **Memory**: total, free, available, buffers, cached, swap usage
- **Disk I/O**: reads, writes, sectors, I/O time per physical device
- **Uptime & Load**: load averages (1/5/15 min), process counts, uptime
- **JSON output**: machine-readable output for scripting
- **Watch mode**: continuous refresh every N seconds

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/sys-inspector
```

Or use presets:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Usage

```bash
# Show all info
./sys-inspector

# Show only memory
./sys-inspector --mem

# Show only CPU
./sys-inspector --cpu

# JSON output
./sys-inspector --json

# Watch mode (refresh every 2s)
./sys-inspector --watch 2

# Combine filters
./sys-inspector --cpu --mem --json
```

## Dependencies

All fetched via CMake FetchContent:
- [CLI11](https://github.com/CLIUtils/CLI11) — argument parsing
- [nlohmann_json](https://github.com/nlohmann/json) — JSON output
- [spdlog](https://github.com/gabime/spdlog) — logging
- [Catch2](https://github.com/catchorg/Catch2) — testing

## Architecture

```
lib/ (sys-inspector-lib)
  ├── sys_info.hpp / sys_info.cpp  — Reader functions for /proc/*
  └── types                        — Error/Result, CpuInfo, MemInfo, etc.

app/ (sys-inspector)
  └── main.cpp                     — CLI entry point with CLI11

tests/
  └── test_main.cpp                — Unit + integration + benchmarks
```

## License

MIT
