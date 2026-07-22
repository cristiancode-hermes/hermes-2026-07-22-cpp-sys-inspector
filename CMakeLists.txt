cmake_minimum_required(VERSION 3.28)
project(sys_inspector VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies (FetchContent) ──
include(FetchContent)

# CLI11 (argument parsing)
FetchContent_Declare(CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.5.0
)
FetchContent_MakeAvailable(CLI11)

# nlohmann_json (JSON output)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# spdlog (logging)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.15.1
)
FetchContent_MakeAvailable(spdlog)

# ── Library (all sources except main.cpp) ──
file(GLOB_RECURSE LIB_SRCS src/*.cpp)
list(FILTER LIB_SRCS EXCLUDE REGEX "main\\.cpp$")
file(GLOB_RECURSE HEADERS include/*.hpp include/*.h)

add_library(sys-inspector-lib STATIC ${LIB_SRCS} ${HEADERS})
target_include_directories(sys-inspector-lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(sys-inspector-lib PUBLIC
  nlohmann_json::nlohmann_json
  spdlog::spdlog
)

# ── Executable ──
add_executable(sys-inspector src/main.cpp)
target_include_directories(sys-inspector PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${cli11_SOURCE_DIR}/include
)
target_link_libraries(sys-inspector PRIVATE
  sys-inspector-lib
  CLI11::CLI11
)

# Catch2 (tests only)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.1
)
FetchContent_MakeAvailable(Catch2)

# ── Tests ──
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ── Install ──
install(TARGETS sys-inspector DESTINATION bin)
