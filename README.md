# Flipper Sense

Flipper Sense is a small, local room-activity experiment. An ESP32 listens for Wi-Fi Channel State Information (CSI) from one configured radio address and sends summary values to a Flipper Zero over UART. The Flipper displays signal-frame quality, motion deviation, and a thresholded activity estimate.

This project does not make a camera-like image, identify people, count people, measure vital signs, or determine a person's location. CSI measurements are sensitive to the room, radio placement, channel, packet rate, and ordinary changes in the environment. Treat the display as an experiment, not a security alarm or a reliable occupancy detector.

The ESP connected to the Flipper in the original setup was identified by `esptool` as a classic **ESP32-D0WDQ6** (ESP32-WROOM family), not an ESP32-S3. This source therefore targets `esp32`. The Flipper app is an external app (`.fap`); it does not replace Flipper firmware.

## Quick download and install

1. Download [`flipper_sense.fap`](https://github.com/Irugudeworking/sensenode/releases/latest/download/flipper_sense.fap) from the [latest release](https://github.com/Irugudeworking/sensenode/releases/latest).
2. Connect the Flipper Zero over USB and open qFlipper's file manager. Copy the downloaded file to `SD Card/apps/GPIO/flipper_sense.fap` (`/ext/apps/GPIO/flipper_sense.fap` on the device). Replace the older copy if one is present.
3. Exit **USB-UART Bridge** if it is open. On the Flipper, open **Apps → GPIO → Sense Node**. Press **OK** to retry if the UART is busy.

The FAP was built for Flipper firmware API 87.1. If it will not launch on your firmware, build it from the source below using a matching uFBT SDK. The ESP32 backpack must also run the matching firmware below. Its Wi-Fi source address is configured per installation, so there is no universal ready-to-flash ESP32 image in this release.

## Project contents

- `flipper_fap/` — Flipper Zero application source and manifest.
- `esp32/` — ESP-IDF firmware for the ESP32 CSI receiver.
- `docs/` — additional setup and bug-report notes.

Generated ESP-IDF settings and build products are intentionally ignored by Git. Configure your own radio address locally; never publish a home or workplace access-point MAC address in `sdkconfig` or a preconfigured binary.

## Hardware and wiring

The tested arrangement is an ESP32-WROOM backpack connected across the Flipper Zero GPIO header pins 9–18. Confirm the pin labels on your own board before applying power.

| Flipper Zero header | ESP32-WROOM signal | Purpose |
| --- | --- | --- |
| Pin 9, 3V3 | 3V3/VCC input on the backpack | Power |
| Pin 8, GND | GND | Shared ground |
| Pin 13, USART TX | GPIO 3 / UART0 RX | Commands from Flipper to ESP32 |
| Pin 14, USART RX | GPIO 1 / UART0 TX | Status from ESP32 to Flipper |

The UART data lines cross: Flipper TX goes to ESP RX, and ESP TX goes to Flipper RX. Use the board's intended 3.3 V supply and logic. Do not connect a 5 V GPIO signal to the ESP32. The ESP32 firmware uses UART0 at 115200 baud, 8 data bits, no parity, one stop bit.

## What you need

- A Flipper Zero with external apps enabled.
- The ESP32-WROOM backpack described above.
- A supported ESP-IDF installation. The firmware was built with ESP-IDF 5.5.1.
- uFBT (the Flipper app build tool), plus Python 3 and a C compiler toolchain.
- A USB cable connecting the Flipper to the computer.
- For useful CSI measurements, a Wi-Fi transmitter or access point that you own or are authorized to use. One ESP32 receiver by itself does not create Wi-Fi traffic.

## Build the ESP32 firmware

Install ESP-IDF from Espressif, then open a shell where its environment is enabled. On Linux, if you installed it in `~/esp/esp-idf`, for example:

```bash
source "$HOME/esp/esp-idf/export.sh"
cd esp32
idf.py set-target esp32
idf.py menuconfig
```

In `menuconfig`, open **Flipper Sense Node** and set:

1. **Authorized CSI transmitter/AP channel (2.4 GHz)** to the radio's 2.4 GHz channel.
2. **Authorized CSI transmitter/AP BSSID (MAC)** to the BSSID of the transmitter or access point you administer.

The checked-in defaults use a dummy MAC and channel 6, so the firmware will not report useful CSI until you configure a real, authorized source. The ESP32 does not join the access point and does not need Wi-Fi credentials. It accepts CSI only when the received source MAC matches the configured address.

Build the firmware:

```bash
idf.py build
```

The main application image is `esp32/build/flipper_sense_node.bin`; the bootloader and partition table are generated beside it.

## Flash the ESP32 through the Flipper Zero

The Flipper's USB-UART Bridge provides the serial connection used to flash the attached ESP32. The Flipper itself is not the ESP32 programmer; it is bridging USB serial to its GPIO USART pins.

1. Close Flipper Sense if it is running.
2. On the Flipper, open **Apps → GPIO → USB-UART Bridge**.
3. In the bridge settings, select **UART pins 13,14** (USART), set **115200 baud**, and leave flow control disabled. The USB channel can be left at its default.
4. Put the ESP32 into its ROM downloader: hold **BOOT**, tap and release **EN/RST**, then release **BOOT**. Keep the USB-UART Bridge open.
5. Identify the serial port on the computer. Linux usually shows `/dev/ttyACM0` or a stable path under `/dev/serial/by-id/`; Windows shows a `COM` port in Device Manager; macOS shows a `/dev/cu.usbmodem…` device.
6. Confirm the connection before writing flash:

   ```bash
   python -m esptool --chip esp32 --port /dev/ttyACM0 --baud 115200 --before no_reset chip_id
   ```

   Replace `/dev/ttyACM0` with your port. If it cannot connect, re-enter downloader mode and check the selected Flipper UART pins.

7. From the `esp32/` directory, flash the built image. Since a USB-UART Bridge generally cannot toggle the ESP32's BOOT and RESET pins, enter downloader mode manually first and tell esptool not to reset the chip:

   ```bash
   python -m esptool --chip esp32 --port /dev/ttyACM0 --baud 115200 \
     --before no_reset --after no_reset write_flash \
     --flash_mode dio --flash_freq 40m --flash_size 2MB \
     0x1000 build/bootloader/bootloader.bin \
     0x8000 build/partition_table/partition-table.bin \
     0x10000 build/flipper_sense_node.bin
   ```

   Use your computer's actual port in place of `/dev/ttyACM0`. Wait for esptool to report that the image hash was verified.

8. Press the ESP32 **EN/RST** button once without holding BOOT. Exit USB-UART Bridge on the Flipper.

If the ESP32 module has a different flash size, inspect it with `esptool flash_id` and set `--flash_size` to match. This project's partition table expects at least 2 MB of flash.

## Build and install the Flipper app

Install uFBT following the Flipper Devices uFBT instructions. From a terminal:

```bash
cd flipper_fap
ufbt
```

The built app is `flipper_fap/dist/flipper_sense.fap`. Copy it to the Flipper SD card at `/ext/apps/GPIO/flipper_sense.fap` using qFlipper, or use uFBT's `ufbt launch` command with the Flipper connected by USB. Open **Apps → GPIO → Sense Node**.

The app requests the Flipper's USART resource and temporarily pauses the Expansion service while it runs. Do not run USB-UART Bridge at the same time. When the app exits, it restores the Expansion service.

Controls:

- **Left** — collect a new empty-room baseline. Keep the monitored area still while it calibrates.
- **OK** — arm/pause sensing. If the screen says UART is busy or unavailable, OK retries acquiring the port after you close the bridge or other UART app.
- **Right** — request an immediate status report.
- **Back** — exit the app.

## Read the display

- **Signal** is the recent count of accepted, configured-source CSI frames scaled to 0–100. It is not Wi-Fi signal strength.
- **Motion** is a smoothed deviation from the empty-room CSI baseline.
- **Presence** is a thresholded activity estimate derived from that deviation. A zero can mean no movement or too few matching frames; check Signal as well.
- **Calibrating** means the receiver is learning a baseline from accepted CSI frames. If it never finishes, check the channel, BSSID, traffic source, and wiring.

The signal path is based on common CSI sensing approaches, including the open-source [RuView project](https://github.com/ruvnet/RuView) and published Wi-Fi sensing research such as [Person-in-WiFi](https://arxiv.org/abs/1904.00276). This small single-receiver implementation is not equivalent to those systems' multi-radio calibration and inference pipelines.

## Troubleshooting

### `UART is busy` or `UART unavailable`

1. Exit USB-UART Bridge and any other app using GPIO serial pins 13/14.
2. Exit Flipper Sense, reopen it, and press OK to retry.
3. Confirm the ESP32 backpack is fully seated and that it is not holding the Flipper in download mode.
4. Restart the Flipper if another app left the USART service occupied.

### `ESP32 not connected`

1. Confirm the ESP32 has booted normally: press EN/RST once and do not hold BOOT.
2. Check common ground and crossed TX/RX: Flipper pin 13 → ESP GPIO3/RX0; Flipper pin 14 ← ESP GPIO1/TX0.
3. Check that the FAP uses the USART pins 13/14 and 115200 baud.
4. Open USB-UART Bridge briefly and use `esptool chip_id` to verify the serial route, then exit Bridge before reopening Flipper Sense.

The offline screen shows `RX bytes: N`. If it stays at zero, the app is receiving no serial data at all: check ESP power, boot mode, seating, and the UART pins. If it rises but the app remains offline, the ESP is sending data that does not match the `SENSE,...` status protocol; confirm the matching ESP firmware is flashed.

### The app connects but Signal stays at zero

The firmware only accepts frames from one configured MAC address on one fixed 2.4 GHz channel. Confirm that the transmitter is active, on that channel, and that the configured MAC is its BSSID/source address. A quiet access point may not send enough frames; an authorized, periodic test transmitter is more repeatable.

### Flashing cannot connect

Re-enter downloader mode by holding BOOT while tapping EN/RST, then release BOOT. Keep USB-UART Bridge open on pins 13/14, no flow control, and 115200 baud. Verify the port name and run `esptool chip_id` before writing flash.

## Verification and known limits

The code was compiled locally with ESP-IDF 5.5.1 for `esp32` and uFBT against Flipper API 87.1. A classic ESP32-WROOM-family chip was identified and an image was written through Flipper USB-UART Bridge with esptool SHA verification. On the connected Flipper, the FAP launched and remained running after OK, Left, and Right inputs. The latest serial diagnostic reported zero received bytes from the attached ESP32. Live sensing is therefore not yet verified on this assembled hardware; check ESP boot and wiring before interpreting any presence score.

No automated room-sensing accuracy evaluation is included. The calculated scores are heuristics; they should not be used for emergency response, access control, or decisions about a person's identity or activities.

## Privacy

Use only radios and spaces you are authorized to monitor, and tell people present. Configure a local transmitter/AP MAC on your own build; do not commit your generated `sdkconfig`, Wi-Fi credentials, personal MAC addresses, or logs containing them. The firmware keeps CSI processing local and sends summary status over the wired UART; it does not store raw CSI or send sensor data over a network.

For reporting a software defect, see [the bug report guide](docs/BUG_REPORT.md).
