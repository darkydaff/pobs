# POBS - Native OBS Studio Plugin for Android Camera

POBS is a native Linux plugin for OBS Studio that integrates an Android phone (tested on Google Pixel 7a running Android 17) as a high-performance camera source using ADB, `scrcpy` 5.0.1, and V4L2 loopback.

---

## Features

* **Zero-Terminal Workflow:** Discover, pair, connect, and stream directly from OBS Studio.
* **Native Qt6 Dock & OBS Source:** Dedicated "POBS Android Camera" dock for device control and diagnostics, plus a native OBS Video Source ("POBS Android Camera").
* **Companion ContentProvider Integration:** Automatically queries `content://com.darkydaff.pobs.provider/config` for camera capabilities, battery status, and selected resolution/framerate.
* **Wireless ADB & USB Auto-Fallback:** Prefers healthy wireless ADB connections, automatically falls back to USB when wireless is unavailable.
* **Device Deduplication:** Correlates Wi-Fi IP:port and USB endpoints using hardware serial (`ro.serialno`), preventing duplicate sessions for the same physical device.
* **Robust scrcpy Lifecycle:** Asynchronously launches and monitors `scrcpy` with bounded backoff retries and zero orphaned processes.
* **Flatpak & Native OBS Compatibility:** Works out of the box with Flatpak OBS Studio (`com.obsproject.Studio`) and native system installs via safe host process bridging.

---

## Target Environment & Requirements

* **OS:** Debian GNU/Linux 13 (Trixie), KDE Plasma 6 (Wayland).
* **OBS Studio:** 32.2.2 (Flatpak or Native).
* **scrcpy:** 5.0.1.
* **Android Tools:** `adb` (platform-tools 34+).
* **Target Phone:** Google Pixel 7a (Android 17) with the POBS companion app installed.
* **Kernel Module:** `v4l2loopback`.

---

## V4L2 Loopback Virtual Device Setup

To provide a virtual camera device for `scrcpy` and OBS Studio, enable `v4l2loopback` on the Linux host:

```bash
# 1. Install kernel headers and v4l2loopback module
sudo apt-get update
sudo apt-get install -y linux-headers-$(uname -r) v4l2loopback-dkms

# 2. Load the module creating /dev/video0
sudo modprobe v4l2loopback devices=1 video_nr=0 card_label="POBS Camera" exclusive_caps=1

# 3. Ensure your user belongs to the 'video' group
sudo usermod -aG video $USER
```

To persist the virtual camera across system reboots:
```bash
echo "v4l2loopback" | sudo tee /etc/modules-load.d/v4l2loopback.conf
echo "options v4l2loopback devices=1 video_nr=0 card_label=\"POBS Camera\" exclusive_caps=1" | sudo tee /etc/modprobe.d/v4l2loopback.conf
```

---

## Build & Installation

### Build from Source
```bash
cd /home/darkydaff/git/pobs-plugin
cmake -B build -G Ninja
cmake --build build
```

### Run Automated Tests
```bash
./build/test_pobs
```

### Install into OBS Studio (Flatpak)
```bash
cmake --install build
```
The plugin binary and locale files are installed to:
* Binary: `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/pobs/bin/64bit/pobs.so`
* Locale Data: `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/pobs/data/locale/en-US.ini`

---

## Architecture

```
                       ┌───────────────────────────────────────────────┐
                       │                  OBS Studio                   │
                       │                                               │
                       │  ┌──────────────────┐  ┌───────────────────┐  │
                       │  │   POBS Qt6 Dock  │  │ POBS Camera Source│  │
                       │  └────────┬─────────┘  └─────────┬─────────┘  │
                       └───────────┼──────────────────────┼────────────┘
                                   │                      │
                                   ▼                      ▼
                       ┌───────────────────────────────────────────────┐
                       │                  PobsService                  │
                       │                                               │
                       │  ┌──────────────┐ ┌──────────────┐ ┌────────┐ │
                       │  │  AdbManager  │ │ScrcpyManager │ │Config  │ │
                       │  └──────┬───────┘ └──────┬───────┘ └────────┘ │
                       └─────────┼────────────────┼────────────────────┘
                                 │                │
            ┌────────────────────┴────────┐       │
            ▼                             ▼       ▼
┌────────────────────────┐    ┌────────────────────────────────────────┐
│ Android Device (Pixel) │    │ Host scrcpy (5.0.1)                     │
│ - Wireless ADB         │    │ --video-source=camera                  │
│ - ContentProvider      │    │ --v4l2-sink=/dev/video0                │
│ - Hardware Serial Deduq│    └───────────────────┬────────────────────┘
└────────────────────────┘                        │ (Video Stream)
                                                  ▼
                                      ┌───────────────────────┐
                                      │  /dev/video0 (V4L2)   │
                                      └───────────────────────┘
```

---

## Workflow

1. **One-Time Pairing (Wireless ADB):**
   * On your Pixel 7a: **Settings → System → Developer options → Wireless debugging → Pair device with pairing code**.
   * Note the IP:Port and 6-digit code shown in the dialog.
   * In OBS Studio under the **POBS Android Camera** dock, enter the IP:Port and Pairing Code and click **Pair Device**.
2. **Connecting:**
   * Enter the connection endpoint from the main Wireless debugging screen (e.g., `192.168.1.89:45587`) and click **Connect Wireless**.
   * When connected, the dock badge turns green: **Wireless ADB**, displaying current battery percentage (e.g. `80%`).
3. **Starting the Camera:**
   * Click **Start Camera** in the dock or add a **POBS Android Camera** source to your scene.
   * `scrcpy` starts with the companion app's configured settings (`1920x1080 @ 60 FPS`, front camera, 12 Mbps bitrate) feeding `/dev/video0`.
4. **USB Fallback:**
   * If Wi-Fi is disconnected or unavailable, plug the phone in via USB. The dock detects the connection and transitions to **USB ADB** without creating duplicate sources.
