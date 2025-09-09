Fourth Iteration (ESP32-S3) — JSON-configurable pins and actions

Contents:
- switchcase_macrokeys/switchcase_macrokeys.ino  (ESP32-S3 BLE keyboard + JSON config)
- config.json                                    (example config with actions and pin map)
- put_config.py                                  (Python helper to upload JSON over Serial)

Summary:
- The sketch runs a BLE keyboard (NimBLE) on ESP32/ESP32-S3.
- Configuration is stored in LittleFS at /config.json.
- You can change both actions and pin assignments via JSON.
- Upload config using Serial CLI: PUT <len> followed by raw JSON bytes.

Usage:
1) Open this folder in Arduino IDE and select your ESP32-S3 board.
2) Install libraries (Board Manager: ESP32; Libs: ArduinoJson, Keypad, Encoder, BleKeyboard, NimBLE-Arduino if needed).
3) Upload the sketch.
4) Connect the ESP32-S3 to your PC. It will expose BLE keyboard after boot.
5) To change config, edit config.json and upload it with:
     python put_config.py COMx config.json
   Replace COMx with your serial port.

Notes on pins:
- Pins are defined in config.json under "pins". You can map matrix rows/cols, encoders, mode button, LEDs, and optional pot.
- If you are migrating wiring from Arduino Pro Micro, set the pins to your ESP32-S3 equivalents here without recompiling the sketch.

