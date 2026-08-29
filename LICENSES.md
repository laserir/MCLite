# Third-Party Licenses

MCLite uses the following open-source libraries. Their license terms apply to the respective components.

| Library | Version | License | Copyright |
|---------|---------|---------|-----------|
| [MeshCore](https://github.com/meshcore-dev/MeshCore) | companion-v1.16.0 | MIT | (c) 2025 Scott Powell / rippleradios.com |
| [LVGL](https://github.com/lvgl/lvgl) | 8.4.0 | MIT | (c) 2021 LVGL Kft |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | 1.2.21 | MIT + BSD-2-Clause | (c) lovyan03 |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | 7.4.3 | MIT | (c) 2014-2026 Benoit Blanchon |
| [RadioLib](https://github.com/jgromes/RadioLib) | 7.6.0 | MIT | (c) 2018 Jan Gromes |
| [base64](https://github.com/Densaugeo/base64_arduino) | 1.4.0 | MIT | (c) 2016 Densaugeo |
| [PNGdec](https://github.com/bitbank2/PNGdec) | 1.1.6 | Apache-2.0 | (c) 2020-2024 Larry Bank |
| [orlp/ed25519](https://github.com/orlp/ed25519) | bundled | Zlib | (c) 2015 Orson Peters |
| [Crypto](https://github.com/rweather/arduinolibs) | 0.4.0 | MIT | (c) 2015, 2018 Southern Storm Software |
| [RTClib](https://github.com/adafruit/RTClib) | 2.1.4 | MIT | (c) 2019 Adafruit Industries |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) | 1.17.4 | MIT | (c) 2017 Adafruit Industries |
| [CayenneLPP](https://github.com/ElectronicCats/CayenneLPP) | 1.6.1 | MIT | (c) Electronic Cats |
| [Melopero RV3028](https://github.com/melopero/Melopero_RV-3028_Arduino_Library) | 1.2.0 | MIT | (c) 2020 Melopero |
| [SensorLib](https://github.com/lewisxhe/SensorLib) | 0.4.1 | MIT | (c) Lewis He (T-Watch Ultra) |
| [XPowersLib](https://github.com/lewisxhe/XPowersLib) | 0.2.9 | MIT | (c) Lewis He (T-Watch Ultra) |
| [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) | 1.1.0 | LGPL-2.1 | (c) 2008-2024 Mikal Hart |
| [Arduino ESP32 core](https://github.com/espressif/arduino-esp32) | 2.0.17 | LGPL-2.1 | (c) Espressif Systems |

## Tools

| Library | Version | License | Copyright |
|---------|---------|---------|-----------|
| [ESP Web Tools](https://github.com/esphome/esp-web-tools) | 10.x | Apache-2.0 | (c) 2021 ESPHome |

## Assets

| Asset | License | Copyright |
|-------|---------|-----------|
| [OpenMoji](https://openmoji.org/) emoji font (monochrome subset, `firmware/src/ui/fonts/emoji_font_*.c`) | CC-BY-SA 4.0 | (c) OpenMoji project (HfG Schwaebisch Gmuend) |
| [OpenMoji](https://openmoji.org/) colour glyphs (`firmware/src/ui/fonts/emoji_color_*.c`, source PNGs in `firmware/assets/fonts/.openmoji-cache/`) | CC-BY-SA 4.0 | (c) OpenMoji project (HfG Schwaebisch Gmuend) |

## License Notes

- **MIT-licensed** libraries are used under the terms of the MIT License. See each project's repository for the full license text.
- **Apache-2.0-licensed** libraries (PNGdec) are used under the terms of the Apache License 2.0.
- **Zlib-licensed** libraries (orlp/ed25519) are used under the terms of the Zlib license.
- **LGPL-2.1-licensed** libraries (TinyGPSPlus, Arduino ESP32 core) are dynamically linked. Users may replace them by rebuilding with PlatformIO.
- The **OpenMoji** emoji glyphs are used under **CC-BY-SA 4.0** with attribution to the OpenMoji project.
  Two subsets are included, both generated rather than hand-edited:
  - a monochrome subset embedded as LVGL fonts, see `firmware/assets/fonts/generate_emoji_fonts.sh`;
  - a 34-glyph colour subset embedded as LVGL images, see `firmware/assets/fonts/generate_color_emoji.py`.
  The colour subset's original PNGs are also checked in under `firmware/assets/fonts/.openmoji-cache/`, so the
  generated output is reproducible offline. That directory is redistributed OpenMoji artwork, under the same
  CC-BY-SA 4.0 terms.
- MCLite itself is released under the **MIT License**. See [LICENSE](LICENSE).
