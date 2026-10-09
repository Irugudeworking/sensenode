# Reporting a Flipper Sense bug

Before opening an issue, write down the exact screen message and whether the ESP32 can be read by esptool through the Flipper USB-UART Bridge.

Include:

- Flipper firmware version and app version/build.
- ESP-IDF version and the ESP32 chip family/revision from `esptool chip_id`.
- Operating system and serial-port name (remove usernames and other private path details).
- Exact steps that lead to the issue and what you expected to happen.
- The exact Flipper screen text or a photo with personal information out of frame.
- Build/flash output around the first error, after removing Wi-Fi passwords, tokens, serial numbers, and private MAC addresses.
- Whether UART is set to pins 13/14 (USART), 115200 baud, 8N1, with flow control off.

Do not attach `esp32/sdkconfig`, Wi-Fi credentials, logs containing private MAC addresses, or a full device dump. CSI-based activity sensing is experimental; describe a repeatable software or wiring failure rather than expecting a particular movement score.
