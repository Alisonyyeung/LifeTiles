# LifeTiles

**A Smart, Customizable Digital Display for Time, Weather, Quotes, Tasks, Image/Animation, and Personal Messages**

Firmware for the [Waveshare ESP32-S3-Touch-LCD-4.3](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3) (800×480 RGB touch panel). The UI is built with **LVGL 8** and **PlatformIO**. When Wi‑Fi is connected, a built-in web server lets you manage photos, to-dos, and messages from a phone or PC on your local network.

---

## Features

| Area         | What you get                                                                                      |
| ------------ | ------------------------------------------------------------------------------------------------- |
| **Home**     | Greeting with your name, live clock (HKT), daily quote (Quotable), weather shortcut, liked quotes |
| **Image**    | Slideshow of photos/animations from LittleFS (JPEG, PNG, WebP, GIF→`.seq`)                        |
| **To-Do**    | Up to 10 tasks on-device; sync via web API                                                        |
| **Message**  | Styled personal message board (dialogue, festive, love, warning), emoji, marquee                  |
| **Weather**  | 7-day forecast + hourly strip (Open-Meteo, no API key)                                            |
| **Settings** | Wi‑Fi (saved networks, static IP), brightness, dark/light theme, display name                     |
| **Web UI**   | Upload images, edit message & todos, preview storage — at `http://<device-ip>/`                   |

---

## Hardware

- **Board:** Waveshare ESP32-S3-Touch-LCD-4.3 (treated as `esp32s3box` in PlatformIO)
- **Display:** 800×480 RGB LCD (ST7262), capacitive touch
- **Flash:** 8 MB (custom partition table with LittleFS)
- **PSRAM:** Required (`BOARD_HAS_PSRAM`)

Panel and IO-expander (CH422G) drivers are vendored under `lib/ESP32_Display_Panel` and `lib/ESP32_IO_Expander` per the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3).

---

## Screens & navigation

Four main tiles live in a horizontal **LVGL tileview**. Swipe left/right between them. **Swipe down** on Comment, Home, or Image opens the **menu** (Weather, Settings, and quick jumps).

```
  ← swipe
┌──────────┬──────────┬──────────┬──────────┐
│ Message  │   Home   │  Image   │  To-Do   │
│ (board)  │ dashboard│ gallery  │  list    │
└──────────┴──────────┴──────────┴──────────┘
                              swipe →
```

| Screen       | Tile / access               | Highlights                                                                                |
| ------------ | --------------------------- | ----------------------------------------------------------------------------------------- |
| **Home**     | Center tile (default)       | Time, quote (shuffle / today), double-tap to like, heart list, settings & weather buttons |
| **Image**    | Swipe right from Home       | Cycle images; animated `.seq` from GIFs; tap for viewer                                   |
| **To-Do**    | Swipe right from Image      | Check off tasks; encouragement on complete                                                |
| **Message**  | Swipe left from Home        | Bubble styles, font size, scroll/marquee, colored emoji                                   |
| **Weather**  | Menu or Home → weather icon | Forecast, backgrounds, Google-style icons                                                 |
| **Settings** | Menu or Home → gear         | Wi‑Fi, static IP, brightness, theme, username                                             |
| **Wi‑Fi**    | Settings → edit Wi‑Fi       | SSID/password, up to 3 saved networks, show-password toggle                               |

Overlay screens (Settings, Weather, Wi‑Fi) slide in from the right; close control returns to the tileview.

---

## Project structure

```
LifeTiles/
├── src/                    # Application logic
│   ├── main.cpp            # Boot: panel, LVGL, Wi‑Fi, services
│   ├── screen_nav.cpp      # Tileview + navigation to overlays
│   ├── main_screen.cpp     # Home / quotes / likes
│   ├── image_screen.cpp    # Image viewer & playback
│   ├── todo_screen.cpp     # To-do UI
│   ├── comment_screen.cpp  # Message board UI
│   ├── weather_screen.cpp  # Weather UI
│   ├── settings_screen.cpp
│   ├── wifi_settings_screen.cpp
│   ├── image_upload_server.cpp  # HTTP server + web UI
│   ├── wifi_manager.cpp / wifi_storage.cpp
│   ├── quotes_api.cpp / weather_api.cpp
│   └── *_storage.cpp       # LittleFS / NVS persistence
├── include/                # Public headers
├── data/                   # LittleFS assets (weather icons, backgrounds, …)
├── partitions/             # 8 MB flash layout
├── tools/                  # Image/weather/font prep scripts
├── lib/
│   ├── lv_conf.h           # LVGL configuration
│   ├── ESP_Panel_Conf.h    # LCD 800×480 pin/bus config
│   ├── ESP32_Display_Panel/
│   ├── ESP32_IO_Expander/
│   └── webp_embedded/      # libwebp build config
└── platformio.ini
```

**Runtime tasks (simplified)**

- **LVGL task** — UI rendering and touch (main thread coordination via `lvgl_port`)
- **HTTP task** (core 0) — `image_upload_server` handles browser + API
- **Worker tasks** — quotes, weather fetch, Wi‑Fi connect (FreeRTOS)

Network HTTPS calls share a **mutex** (`net_mutex`) to avoid TLS heap clashes.

---

## Web server

After Wi‑Fi connects, the device prints:

```text
Image upload UI: http://<ip>/
```

Open that URL in a browser on the same LAN.

| Endpoint                          | Method   | Purpose                                       |
| --------------------------------- | -------- | --------------------------------------------- |
| `/`                               | GET      | Main web UI (upload, gallery, message, todos) |
| `/ping`                           | GET      | Health check                                  |
| `/upload`                         | POST     | Upload image (multipart)                      |
| `/api/storage`                    | GET      | Storage usage                                 |
| `/api/preview`                    | GET      | Thumbnail/preview                             |
| `/api/select`                     | POST     | Set active slideshow image                    |
| `/api/delete`, `/api/delete_bulk` | POST     | Remove files                                  |
| `/api/rename`                     | POST     | Rename file                                   |
| `/api/comment`                    | GET/POST | Read/write message board                      |
| `/api/todos`                      | GET/POST | Read/write to-do list (JSON)                  |

On connect, the firmware syncs **NTP** (Hong Kong time), fetches **today’s quote**, and restarts the HTTP server so the IP stays correct after network changes.

---

## Data storage

### NVS (Preferences, namespace `myscreen`)

| Key / area               | Content                                       |
| ------------------------ | --------------------------------------------- |
| `wifi_ssid`, `wifi_pass` | Active Wi‑Fi credentials                      |
| `wifi_st_*`              | Static IP enable, IP, gateway, subnet         |
| `wifi_st_pv`             | Flag: reprovision static IP after SSID change |
| `img_sel`                | Last selected image basename                  |
| `qt_*`                   | Cached “quote of the day”                     |
| `username`               | Display name for greeting                     |
| `theme`, `brightness`    | UI preferences                                |

Wi‑Fi passwords are **not** stored in source code; optional compile-time defaults live in `include/wifi_config.h` (use placeholders for git).

### LittleFS (on-device flash filesystem)

| Path                 | Content                                                               |
| -------------------- | --------------------------------------------------------------------- |
| `/images/`           | Uploaded photos; optional `*_scaled.*` from `tools/prepare_images.py` |
| `/todos.json`        | To-do list (max 10 tasks)                                             |
| `/comment.txt`       | Message text                                                          |
| `/comment_meta.json` | Style, font size, scroll, festive color                               |
| `/comment_at.txt`    | Message timestamp                                                     |
| `/liked_quotes.json` | Up to 10 liked quotes                                                 |
| `/wifi_hist.json`    | Up to 3 saved Wi‑Fi networks (SSID + password)                        |

GIF animations should be converted to **`.seq`** (see `tools/gif_to_seq.py`) for smooth playback.

### `data/` folder (flashed with filesystem)

Shipped assets: weather icons, weather backgrounds, comment decoration PNGs, fonts. Uploaded via PlatformIO **Upload Filesystem Image**.

---

## Setup

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB cable to the ESP32-S3 board
- Wi‑Fi network (2.4 GHz)

### 1. Clone and open

```bash
git clone <your-repo-url> LifeTiles
cd LifeTiles
```

### 2. Wi‑Fi (optional at build time)

Copy and edit local config (do **not** commit real passwords):

```cpp
// include/wifi_config.h
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "your_password"
```

On first boot, credentials are copied to NVS. **Recommended:** leave placeholders in git and configure Wi‑Fi on the device under **Settings → View / edit Wi‑Fi settings** (supports up to 3 saved networks).

### 3. Build & flash firmware

1. Open the project in PlatformIO.
2. Select environment **`esp32s3box`**.
3. Put the board in download mode if needed: hold **BOOT**, press **RESET**, release **BOOT**.
4. **Upload** (`pio run -t upload`).

### 4. Flash LittleFS (first time & after `data/` changes)

```bash
pio run -t uploadfs
```

This runs `tools/prepare_images.py` and `tools/prepare_weather_backgrounds.py` before building the filesystem image.

### 5. Serial monitor

```bash
pio device monitor -b 115200
```

Watch for Wi‑Fi IP and `Image upload UI: http://...`.

### 6. Weather location (optional)

Default forecast is **Hong Kong** (`latitude=22.32`, `longitude=114.17` in `src/weather_api.cpp`). Change those coordinates and the `timezone` query parameter for your city. Open-Meteo does not require an API key.

---

## Developer tools (`tools/`)

| Script                           | Purpose                                              |
| -------------------------------- | ---------------------------------------------------- |
| `prepare_images.py`              | Scale/optimize images under `data/images` for upload |
| `prepare_weather_backgrounds.py` | Prepare weather background JPEGs                     |
| `gif_to_seq.py`                  | Convert GIF → `.seq` animation for the image screen  |
| `gen_google_weather_icons.py`    | Regenerate embedded weather icons                    |
| `gen_comment_*.py`               | Emoji fonts, CJK font, bubble decorations            |

---

## Configuration highlights

| File                          | Role                                                                        |
| ----------------------------- | --------------------------------------------------------------------------- |
| `platformio.ini`              | Board, 8 MB flash, LittleFS, lib deps (LVGL, ArduinoJson, libwebp, JPEGDEC) |
| `lib/lv_conf.h`               | LVGL features (fonts, imgfont, etc.)                                        |
| `lib/ESP_Panel_Conf.h`        | Resolution 800×480, RGB bus, touch                                          |
| `partitions/myscreen_8MB.csv` | App + LittleFS partition sizes                                              |

---

## Security note (before publishing)

- Do not commit `include/wifi_config.h` with real SSID/password.
- Device stores Wi‑Fi and message data in **plaintext** on flash (`/wifi_hist.json`, NVS).
- Web server has **no authentication** — use only on a trusted home LAN.

---

## Licenses & credits

- **LVGL** 8.3 — UI
- **Waveshare** — ESP32-S3-Touch-LCD-4.3 hardware & driver examples
- **Open-Meteo** — weather data
- **Quotable** — quotes API
- **libwebp**, **JPEGDEC** — image decode

---

## Troubleshooting

| Issue                | Things to try                                                         |
| -------------------- | --------------------------------------------------------------------- |
| Upload fails         | BOOT + RESET sequence; check USB port and driver                      |
| Blank display        | Verify `ESP_Panel_Conf.h` matches your board revision                 |
| No Wi‑Fi             | Set network in Settings; check 2.4 GHz; serial log for errors         |
| Web UI unreachable   | Same LAN as device; use IP from serial log after connect              |
| GIF won’t animate    | Convert to `.seq` with `tools/gif_to_seq.py` and upload to `/images/` |
| Quotes/weather empty | Wait for NTP; confirm internet access after Wi‑Fi connects            |

Feedback and contributions welcome.
