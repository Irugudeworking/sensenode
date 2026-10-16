# Установка Flipper Sense: Flipper Zero + ESP32-WROOM

Эта инструкция рассчитана на плату ESP32, вставленную в GPIO-разъём Flipper Zero, и прошивку репозитория `sensenode` версии v0.2.0. Установить нужно **две разные программы**: `flipper_sense.fap` на Flipper и собранную прошивку на ESP32. Готового универсального ESP32-образа нет: для него надо выбрать свой 2.4‑GHz канал и BSSID.

> Flipper Sense — экспериментальный индикатор изменения Wi‑Fi CSI, а не камера и не надёжный детектор людей. Используйте только свою сеть и помещение, где люди знают об эксперименте.

## 1. Проверить оборудование

- Нужны Flipper Zero, USB-кабель для подключения к компьютеру, карта microSD, ESP32-плата и точка доступа **2.4 GHz**.
- Плата, на которой проект проверяли, определилась как **ESP32-D0WDQ6 (ESP32-WROOM)**. Это **не ESP32-S3**. Если `esptool` покажет `ESP32-S3`, не прошивайте образ для `esp32`: потребуется отдельная сборка с целью `esp32s3` и проверка разводки платы.
- Плата должна получать 3.3 V, общий GND и UART: Flipper TX (pin 13) → ESP GPIO3/RX0; ESP GPIO1/TX0 → Flipper RX (pin 14). На некоторых backpack-платах эти соединения уже сделаны. Сверьте подписи именно на своей плате; не подавайте 5 V на GPIO ESP32.

## 2. Установить приложение на Flipper

1. Скачайте [`flipper_sense.fap` из последнего релиза](https://github.com/Irugudeworking/sensenode/releases/latest/download/flipper_sense.fap).
2. Подключите Flipper по USB. В [qFlipper → File manager](https://docs.flipper.net/zero/qflipper) откройте `SD Card/apps/GPIO/` и загрузите туда файл. Если там уже есть `flipper_sense.fap`, замените его.
3. Пока **не запускайте** Sense Node: сначала прошейте ESP32. Приложение из релиза собрано для Flipper API 87.1; при несовместимой версии прошивки Flipper понадобится сборка `.fap` из `flipper_fap/` под вашу версию.

## 3. Подготовить 2.4‑GHz Wi‑Fi

Узнайте **канал** (число 1–14) и **BSSID** (MAC-адрес самой точки доступа) для сети **2.4 GHz**. Их можно посмотреть в интерфейсе своего роутера. На Linux подойдёт `nmcli -f BSSID,SSID,CHAN dev wifi list`; на Windows — `netsh wlan show networks mode=bssid`.

Не путайте BSSID сети 2.4 GHz с адресом её 5‑GHz сети: даже у одного роутера это разные адреса. Классический ESP32-WROOM не принимает 5 GHz. Пароль Wi‑Fi в проект вводить не нужно: ESP32 не подключается к сети.

На выбранной сети должен быть **активный 2.4‑GHz клиент**, обменивающийся данными. Наличие SSID в списке сетей само по себе не гарантирует появления CSI: тихая точка доступа может не давать подходящих кадров. Для первого теста держите ноутбук/телефон на этой 2.4‑GHz сети и создайте обычный сетевой трафик, например откройте несколько страниц.

## 4. Собрать прошивку ESP32

Скачайте исходники кнопкой **Code → Download ZIP** на [странице проекта](https://github.com/Irugudeworking/sensenode) и распакуйте их. Установите [ESP-IDF 5.5.1 для ESP32](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32/get-started/). Для Linux/macOS пример установки ESP-IDF:

```bash
mkdir -p "$HOME/esp"
cd "$HOME/esp"
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32
```

В новом терминале включите окружение ESP-IDF, перейдите в распакованную папку проекта и соберите прошивку:

```bash
source "$HOME/esp/esp-idf/export.sh"
cd /ПУТЬ/К/sensenode/esp32
idf.py set-target esp32
idf.py menuconfig
```

В меню **Flipper Sense Node** измените:

1. `Authorized CSI transmitter/AP channel (2.4 GHz)` — ваш канал 2.4 GHz.
2. `Authorized CSI transmitter/AP BSSID (MAC)` — BSSID этой точки доступа в формате `AA:BB:CC:DD:EE:FF`.

В **Serial flasher config → Flash size** укажите размер флеша вашей платы, если он отличается от значения по умолчанию. Сохраните настройки и выйдите, затем выполните `idf.py build`. Ожидаемые файлы: `build/bootloader/bootloader.bin`, `build/partition_table/partition-table.bin`, `build/flipper_sense_node.bin`.

На Windows установите ESP-IDF 5.5.1 по [официальной инструкции](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32/get-started/), откройте **ESP-IDF PowerShell** и выполните `idf.py set-target esp32`, `idf.py menuconfig`, `idf.py build` в папке `esp32`.

`esp32/sdkconfig` содержит ваш BSSID. **Не публикуйте этот файл и собранный персональный `.bin`** в открытом репозитории.

## 5. Прошить ESP32 через Flipper

1. Закройте **Sense Node** и любые другие приложения Flipper, работающие с UART.
2. Откройте на Flipper **Apps → GPIO → USB-UART Bridge**. Выберите **USART / pins 13,14**, **115200 бод**, без flow control. Не закрывайте мост до окончания записи.
3. На ESP32 **зажмите BOOT → нажмите EN/RST на 0,5–1 секунду → отпустите EN/RST → подержите BOOT ещё 1–2 секунды → отпустите BOOT**.
4. Найдите порт Flipper: Linux обычно `/dev/ttyACM0`, Windows `COMx`, macOS `/dev/cu.usbmodem...`. Подставьте свой порт вместо `/dev/ttyACM0` в командах ниже. На компьютере можно держать открытой только одну программу, использующую этот порт (закройте qFlipper/serial monitor при конфликте).
5. Из папки `esp32/` проверьте, что виден **именно ESP32**, а не CLI Flipper:

```bash
python -m esptool --chip esp32 --port /dev/ttyACM0 --baud 115200 --before no-reset --after no-reset chip-id
```

Успех: строка `Connected to ESP32`, тип чипа `ESP32-D0WDQ6` (или другой классический ESP32) и MAC. Если получаете `Write timeout` или `Failed to connect`, проверьте мост и повторите BOOT/EN. **Не переходите к записи, пока этот шаг не сработал.**

6. Запишите три файла. Пример для платы с **4 MB flash** (размер должен совпадать с вашей платой; узнать его можно командой `python -m esptool --chip esp32 --port /dev/ttyACM0 --before no-reset flash-id`):

```bash
python -m esptool --chip esp32 --port /dev/ttyACM0 --baud 115200 --before no-reset --after no-reset write-flash --flash-mode dio --flash-freq 40m --flash-size 4MB 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/flipper_sense_node.bin
```

Подождите сообщения `Hash of data verified` для каждого файла. Не отключайте ESP32 во время записи. В Windows используйте свой `COMx` и ту же команду одной строкой в ESP-IDF PowerShell.

7. Нажмите **EN/RST один раз без BOOT**. Выйдите из USB-UART Bridge, затем откройте на Flipper **Apps → GPIO → Sense Node**. Эти два приложения нельзя запускать одновременно.

## 6. Проверить работу

- Сначала виден `Baseline N/50`: держите зону наблюдения спокойной, пока число не дойдёт до 50. Скорость зависит от подходящих CSI-кадров.
- Затем появятся полосы `Presence`, `Motion`, `Signal`. Это относительные оценки изменения радиосигнала, а не число людей.
- **Left** — новая калибровка; **OK** — пауза/запуск; **Right** — запрос статуса; **Back** — выход.

Если экран не меняется, используйте подсказку ниже. Даже после правильной установки нельзя обещать, что конкретный роутер и размещение платы обеспечат достаточный поток CSI: это экспериментальная система.

| Надпись на экране | Что означает | Что проверить |
| --- | --- | --- |
| `UART unavailable` / `UART is busy` | Flipper не получил USART | Закройте USB-UART Bridge и другое UART-приложение; откройте Sense Node снова. |
| `ESP32 not connected` | Нет корректного статуса от ESP | Нажмите EN/RST **без BOOT**; проверьте питание, GND, USART 13/14 и 115200 бод. |
| `No Wi-Fi CSI frames` | ESP не получил CSI callbacks | Убедитесь, что ESP слушает актуальный канал 2.4 GHz и там есть активный трафик. Эта надпись **не** означает, что вокруг вообще нет Wi-Fi. |
| `MAC/channel mismatch` | Есть CSI, но от другого MAC | Сверьте BSSID **2.4 GHz**, канал и пересоберите/перепрошейте ESP32. |
| `Baseline` не растёт | Не хватает подходящих кадров | Проверьте две предыдущие строки; сохраните 2.4‑GHz клиент активным. |

Для более подробного разбора см. [основной README](../README.md#troubleshooting) и [шаблон сообщения об ошибке](BUG_REPORT.md).
