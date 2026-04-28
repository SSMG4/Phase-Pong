cmake_minimum_required(VERSION 3.11)
project(PhasePong VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)

# ── Fetch raylib (pinned to a stable release) ──────────────────────────────
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5          # pinned — never use 'master' in production
    GIT_SHALLOW    TRUE         # faster clone: no full history needed
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# ── Target ─────────────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

# ── Platform-specific configuration ────────────────────────────────────────
if(${PLATFORM} STREQUAL "Web")
    # Output a self-contained .html page that embeds the JS loader
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")

    target_link_options(${PROJECT_NAME} PRIVATE
        # WebGL / GLFW layer supplied by Emscripten
        -s USE_GLFW=3

        # Emit a WASM binary (not asm.js)
        -s WASM=1

        # Allow the heap to grow at runtime instead of being capped at compile time.
        # Safer than a fixed TOTAL_MEMORY value for a game with variable assets.
        -s ALLOW_MEMORY_GROWTH=1

        # Expose the full POSIX filesystem API needed for file I/O (high-score file)
        -s FORCE_FILESYSTEM=1

        # Balanced optimisation — fast builds in CI, playable frame-rate
        -O2
    )
    # NOTE: ASYNCIFY is intentionally omitted.
    # The game uses emscripten_set_main_loop(), which is the correct
    # non-blocking approach for WASM.  ASYNCIFY would only be needed if
    # the game loop were a blocking while-loop, and it carries significant
    # code-size overhead.
endif()
