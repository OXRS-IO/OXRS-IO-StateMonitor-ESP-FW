# ESP32 State Monitor firmware for [OXRS](https://oxrs.io)

See [here](https://oxrs.io/docs/firmware/state-monitor-esp32.html) for documentation.

To download the binary for the latest release, see the [releases page](https://github.com/SuperHouse/OXRS-SHA-StateMonitor-ESP32-FW/releases).

You can upload binary releases using a variety of tools. On the command line, you can use esptool.py like this:

```esptool.py --chip esp32 --port "/dev/tty.usbserial-0001" --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x10000 OXRS-SHA-StateMonitor-ESP32-FW.ino.esp32.bin```

Adjust the path to esptool.py and the serial port identifier to suit your computer.
