cmake_minimum_required(VERSION 3.22)
project(PhasePongAndroid)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG master
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PLATFORM "Android" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(raylib)

add_library(phasepong SHARED ../../src/main.cpp)

target_link_libraries(phasepong
    PRIVATE
    raylib
    android
    log
    EGL
    GLESv2
)