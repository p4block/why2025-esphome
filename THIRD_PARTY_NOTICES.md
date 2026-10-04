# Third-party sources

| Source | Use | License |
| --- | --- | --- |
| [WHY2025 BadgeVMS](https://gitlab.com/why2025/team-badge/firmware) | ST7703 panel initialization data; hardware reference | GPL-3.0-or-later; project license retained |
| [Bosch BME690 SensorAPI](https://github.com/boschsensortec/BME690_SensorAPI) v1.1.0 | Bundled `bme69x.c`, `bme69x.h`, `bme69x_defs.h` | BSD-3-Clause, included alongside source |
| [ESPHome](https://github.com/esphome/esphome) 2026.9.1 | Framework/components, downloaded by pip | MIT |
| [ESP-Hosted-MCU](https://github.com/espressif/esp-hosted-mcu) commit `471900d45c3b9208da10a01beb200b97f15a161a` (v2.12.12) | C6 radio source, downloaded by builder | Apache-2.0 |
| [ESPHome ESP-Hosted firmware overlay](https://github.com/esphome/esp-hosted-firmware) commit `e4c39e0187db8fb6f9921cf8fa875463a792ce7f` | CustomRpc/ESP-NOW compatibility, downloaded by builder | See upstream license and per-file headers |
| [ESP-IDF](https://github.com/espressif/esp-idf) 5.5.5 | Toolchain/framework, downloaded by ESPHome | Apache-2.0 plus component-specific licenses |
| [LVGL](https://github.com/lvgl/lvgl) | Runtime UI and built-in font, downloaded by ESPHome | MIT |
| [Waveshare display configuration](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B) | Timing/register comparison reference | See upstream notices |

No upstream radio binaries, device flash dumps, compiled ESPHome firmware,
secrets, or local build dependency trees are distributed in this source package.
