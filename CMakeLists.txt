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

# ── vcpkg toolchain (set via cmake preset or CLI) ─────────────────────────
# Pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# or set VCPKG_ROOT in your environment.

# ── Dependencies ───────────────────────────────────────────────────────────
find_package(Drogon CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# ── Include dirs ───────────────────────────────────────────────────────────
include_directories(include)

# ── Subdirectories ─────────────────────────────────────────────────────────
add_subdirectory(src)
add_subdirectory(tests/unit)

# ── Install ────────────────────────────────────────────────────────────────
install(TARGETS corvus-core DESTINATION bin)
