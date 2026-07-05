cmake_minimum_required(VERSION 3.25)
project(corvus VERSION 0.1.0 LANGUAGES CXX)

# ── C++20 ──────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler warnings ──────────────────────────────────────────────────────
add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Wshadow
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Wcast-align
  -Wunused
  -Woverloaded-virtual
  -Wconversion
  -Wsign-conversion
)

# ── Build type default ─────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ── Sanitizers / coverage (opt-in) ─────────────────────────────────────────
# Set via -DCORVUS_SANITIZER=asan|tsan|coverage (default: none).
# asan and tsan are mutually exclusive - pick one per build.
# Each pulls in -g -O1 so stack traces stay readable without being
# unusably slow.
set(CORVUS_SANITIZER "none" CACHE STRING
    "Instrumentation: none|asan|tsan|coverage")
set_property(CACHE CORVUS_SANITIZER PROPERTY STRINGS
             none asan tsan coverage)

if(CORVUS_SANITIZER STREQUAL "asan")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
  add_link_options(-fsanitize=address,undefined)
elseif(CORVUS_SANITIZER STREQUAL "tsan")
  add_compile_options(-fsanitize=thread,undefined -fno-omit-frame-pointer -g -O1)
  add_link_options(-fsanitize=thread,undefined)
elseif(CORVUS_SANITIZER STREQUAL "coverage")
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()

# ── vcpkg toolchain (set via cmake preset or CLI) ─────────────────────────
# Pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# or set VCPKG_ROOT in your environment.

# ── Dependencies ───────────────────────────────────────────────────────────
find_package(Drogon CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# ── Include dirs ───────────────────────────────────────────────────────────
include_directories(include)

# ── Testing ────────────────────────────────────────────────────────────────
enable_testing()

# ── Subdirectories ─────────────────────────────────────────────────────────
add_subdirectory(src)
add_subdirectory(tests/unit)

# ── Install ────────────────────────────────────────────────────────────────
install(TARGETS corvus-core DESTINATION bin)
