# VM Go - Universal Native Android Virtualization

**VM Go** is a high-performance, containerized Android-on-Android Virtualization Engine & Application capable of running unmodified Generic System Images (GSI) such as AOSP 12.1, LineageOS, and PixelOS with near-native execution speed.

---

## Key Features

* **100% Native Speed Execution**:
  * Utilizes **Seccomp-BPF + SIGSYS Trap Handlers** (`seccomp_trap.cpp`) to intercept syscalls (`openat`, `mount`, `mknodat`, `setuid`) without slow ptrace overhead.
  * Direct ARM64-on-ARM64 bare-metal CPU execution.

* **Universal GSI Support**:
  * Embedded **Android Sparse Image Unpacker** (`sparse_parser.cpp`) converting GSI `.img` / `.xz` / `.zip` directly into raw ext4 partitions.
  * Automatic **APEX Module Decompression** (`ApexExtractor.kt`) for Android 10/11/12+ runtimes (`com.android.art`, `com.android.runtime`, `com.android.vndk`).

* **Hardware-Accelerated Display**:
  * OpenGL ES 2.0 / 3.0 texture pipeline (`egl_renderer.cpp`) rendering guest SurfaceFlinger framebuffers onto Android `SurfaceView`.

* **Linux Evdev Input Bridge**:
  * Serializes multi-touch gestures (`ABS_MT_POSITION_X/Y`) and navigation keycodes into kernel `struct input_event` structures.

* **QEMU Pipe HAL Multiplexing**:
  * Local UNIX domain socket server (`qemu_pipe_server.cpp`) bridging host Accelerometer, Gyroscope, Ambient Light, GPS, and AudioTrack directly to guest OS.

* **Modular Add-ons & Overlays**:
  * Plug-and-play **Magisk Root** (`/system/xbin/su`) and **Google Play Services (GApps)** toggles.

* **In-App Diagnostic & Crash Logcat System**:
  * Persistent file logging (`vmgo_system_logs.txt`) and fatal exception catchers displaying live logs inside the app with search, copy, share, and clear capabilities.

---

## Automated CI/CD Build (GitHub Actions)

This repository includes a GitHub Actions workflow (`.github/workflows/build.yml`) that automatically builds the Debug APK with Android NDK on every push.

---

## Local Build

```bash
./gradlew assembleDebug
```
The resulting APK will be generated at `app/build/outputs/apk/debug/app-debug.apk`.
