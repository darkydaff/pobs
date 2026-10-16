# POBS — Phone to OBS Camera Suite

POBS transforms your Android smartphone into a ultra-low latency, broadcast-grade video source directly inside OBS Studio. It delivers zero-lag video streaming over USB (ADB reverse tunnel) or wireless LAN with integrated camera switching, hardware digital zoom, torch/key light control, and real-time telemetry.

---

## 🚀 Features

- **Ultra-Low Latency Pipeline**: End-to-end hardware acceleration using Android `MediaCodec` (H.264 Annex B) and OBS native FFmpeg H.264 packet decoder.
- **Wireless ADB Auto-Tunneling**: One-click pairing and persistent device reconnection for hassle-free USB or Wi-Fi ADB streaming without manual port forwarding.
- **Direct LAN & QR Pairing**: Live QR code generator inside the OBS dock for rapid instant pairing over Wi-Fi.
- **Full Studio Dock Controls**:
  - Resolution selector (1080p, 720p, 4K)
  - Frame rate switcher (60 FPS, 30 FPS)
  - Smooth digital PTZ zoom (1.0x to 5.0x)
  - Front / Back camera toggle
  - Key light / flashlight remote trigger
  - Stream pause / resume toggle
  - Live battery & charging status telemetry

---

## 📁 Repository Structure

```
pobs/
├── android/          # Native Android companion app (Kotlin, Camera2, MediaCodec, WebSocket)
└── plugin/           # OBS Studio C++ plugin (libobs, Qt6, FFmpeg, WebSocket Server)
```

---

## 🛠️ Building & Installation

### 1. Android Companion App (`android/`)

**Requirements:**
- Android SDK 26+ (Android 8.0+)
- JDK 17+

**Build Debug APK:**
```bash
cd android
./gradlew assembleDebug
```

**Install to Device via ADB:**
```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

---

### 2. OBS Studio Plugin (`plugin/`)

**Requirements:**
- OBS Studio 30+ (development headers / `libobs`)
- Qt 6 (Widgets, Core, Gui)
- FFmpeg (libavcodec, libavutil)
- CMake 3.16+
- C++17 compiler

**Build Plugin:**
```bash
cd plugin
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Install Plugin:**
- **Flatpak OBS:**
  ```bash
  mkdir -p ~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/pobs/bin/64bit
  cp build/pobs.so ~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/pobs/bin/64bit/
  ```
- **Native Linux OBS:**
  ```bash
  mkdir -p ~/.config/obs-studio/plugins/pobs/bin/64bit
  cp build/pobs.so ~/.config/obs-studio/plugins/pobs/bin/64bit/
  ```

---

## 📄 License

GPL-2.0 / MIT
