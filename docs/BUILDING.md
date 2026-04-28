# Phase Pong — Build Instructions

## Repository layout

```
Phase-Pong/
├── core_cpp/            ← shared C++ source (all platforms read from here)
│   ├── CMakeLists.txt
│   └── src/
│       └── main.cpp
├── android/
│   └── app/
│       ├── CMakeLists.txt   ← NDK build; links to ../../core_cpp/src/main.cpp
│       └── build.gradle
├── .github/
│   └── workflows/
│       ├── ci-debug.yml           ← Linux + Android debug builds
│       └── ci-release-deploy.yml  ← WASM build + GitHub Pages deploy
└── docs/
    └── BUILDING.md  ← you are here
```

---

## Prerequisites

| Target | Tools needed |
|--------|-------------|
| **Desktop (Linux / Windows / macOS)** | CMake ≥ 3.11, GCC / Clang / MSVC, git |
| **Android** | Android Studio, JDK 17, Android SDK + NDK (via SDK Manager) |
| **WebAssembly** | [Emscripten SDK (emsdk)](https://emscripten.org/docs/getting_started/downloads.html) |

raylib is fetched automatically by CMake via `FetchContent` — you do **not** need to install it manually.

---

## Desktop (Linux / macOS / Windows)

```bash
# From the repo root
cmake -B build -DCMAKE_BUILD_TYPE=Release core_cpp
cmake --build build --config Release -- -j$(nproc)

# Run
./build/PhasePong          # Linux / macOS
build\Release\PhasePong.exe  # Windows
```

For a **debug** build, replace `Release` with `Debug`.

---

## Android

1. Open **Android Studio** and select *Open an existing project*.
2. Navigate to the `android/` subdirectory and open it.
3. Let Gradle sync finish (it will invoke CMake to compile the NDK layer).
4. Connect a device or start an emulator, then run **▶ Run**.

To build from the command line:

```bash
cd android
./gradlew assembleRelease          # Linux / macOS
gradlew.bat assembleRelease        # Windows
```

Signed APK: `android/app/build/outputs/apk/release/app-release-unsigned.apk`

---

## WebAssembly (manual)

```bash
# Activate emsdk first (once per shell session)
source /path/to/emsdk/emsdk_env.sh

# From the repo root
mkdir -p build_web && cd build_web
emcmake cmake ../core_cpp -DCMAKE_BUILD_TYPE=Release -DPLATFORM=Web
emmake make -j$(nproc)

# Copy output so it can be served at the root URL
cp PhasePong.html index.html

# Serve locally
python3 -m http.server 8080
# then open http://localhost:8080
```

> **Note:** WASM files must be served over HTTP — opening `index.html` directly
> from the filesystem will fail due to browser CORS/COOP restrictions.

---

## WebAssembly (CI / GitHub Pages)

Push to `master` or `dev` to trigger the `ci-release-deploy.yml` workflow.
GitHub Actions will:

1. Set up Emscripten via `mymindstorm/setup-emsdk`.
2. Build the WASM bundle into `build_web/`.
3. Copy `PhasePong.html → index.html` so the Pages root URL resolves.
4. Deploy the `build_web/` directory to GitHub Pages.

The live URL will be printed in the workflow summary under **deploy-pages**.

---

## Common issues

| Symptom | Fix |
|---------|-----|
| `CMake Error: The source does not appear to contain CMakeLists.txt` | You ran `cmake .` from the repo root. Point cmake at `core_cpp/` instead. |
| Android build: `error: ../../src/main.cpp: No such file or directory` | Old `android/app/CMakeLists.txt` — the path was wrong. Pull the latest version. |
| WASM build: GitHub Pages shows 404 | `PhasePong.html` was not renamed to `index.html`. The CI workflow now handles this automatically. |
| raylib fetch fails / random upstream break | Upgrade to the pinned `GIT_TAG 5.5` in both `CMakeLists.txt` files. |
