# Build Instructions



## Prerequisites

- **Android:** Android Studio, JDK 17, Android SDK, Android NDK (installed via SDK Manager).

- **Desktop (Windows/Linux):** CMake 3.11+, GCC/Clang/MSVC.

- **WebAssembly:** Emscripten SDK (emsdk).



## Compiling for Android (Native C++)

1. Navigate to the `android/` directory.

2. Execute `./gradlew assembleRelease` (Linux/macOS) or `gradlew.bat assembleRelease` (Windows).

3. The Gradle build will automatically trigger CMake to compile the NDK C++ core into `libphasepong.so`.

4. The signed APK will be generated in `android/app/build/outputs/apk/release/`.



## Compiling for Desktop

1. Navigate to the root directory.

2. Create a build directory: `mkdir build \&\& cd build`.

3. Generate build files: `cmake .. -DCMAKE\_BUILD\_TYPE=Release`.

4. Compile the project: `cmake --build .`.

5. Run the output binary `PhasePong`.



## Compiling for WebAssembly (GitHub Pages)

1. Ensure `emsdk` is activated in your terminal session.

2. Navigate to the root directory.

3. Create a build directory: `mkdir build\_web \&\& cd build\_web`.

4. Generate build files: `emcmake cmake .. -DCMAKE\_BUILD\_TYPE=Release -DPLATFORM=Web`.

5. Compile: `emmake make`.

6\. Host the resulting files locally (e.g., `python3 -m http.server`) or push to trigger the GitHub Actions deployment.

