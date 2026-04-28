cmake_minimum_required(VERSION 3.22)
project(PhasePongAndroid)

set(CMAKE_CXX_STANDARD 17)

# ── Fetch raylib (pinned to same version as the desktop/WASM target) ───────
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5          # pinned — keep in sync with core_cpp/CMakeLists.txt
    GIT_SHALLOW    TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# The PLATFORM variable must be set before FetchContent_MakeAvailable so
# that raylib builds its Android back-end (NativeActivity + EGL/GLES).
set(PLATFORM "Android" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(raylib)

# ── Shared library loaded by NativeActivity ─────────────────────────────────
# FIX: path corrected from ../../src/main.cpp (pointed at repo root, which
#      has no src/ directory) to ../../core_cpp/src/main.cpp.
add_library(phasepong SHARED ../../core_cpp/src/main.cpp)

target_link_libraries(phasepong
    PRIVATE
    raylib    # raylib Android back-end (handles NativeActivity lifecycle)
    android   # NDK Android integration
    log       # __android_log_print
    EGL       # OpenGL ES surface management
    GLESv2    # OpenGL ES 2.0 draw calls
)
