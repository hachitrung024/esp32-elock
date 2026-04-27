# elock

Smart lock firmware for ESP32-S3 running Zephyr 4.x. The device exposes a small web UI for PIN-based unlocking and falls back to a setup access point when no WiFi credentials are stored.

## Features

- **WiFi state machine** — STA when credentials are saved, AP fallback (`SmartLock-Setup` on `192.168.4.1`) for first-time setup or after 5 failed connection attempts.
- **Web UI** — single-page HTML served at `/`, with a 6-digit PIN keypad and live status cards polling `/status` every second.
- **PIN unlock** — `POST /unlock` drives the lock GPIO HIGH for 5 seconds, then auto-relocks.
- **Manual lock** — onboard button (GPIO0) immediately drops the lock GPIO LOW, short-circuiting any active unlock window.
- **Persistent config** — WiFi SSID/PSK stored in NVS via Zephyr's settings subsystem.

## Hardware

| Function | Pin | Notes |
|---|---|---|
| Lock output | GPIO48 | Active-high, drives relay/solenoid driver |
| Button | GPIO0 | Active-low, internal pull-up, 50 ms debounce |

Board: `esp32s3_devkitc/esp32s3/procpu`. Console on USB-CDC at 115200 baud.

## HTTP API

| Method | Path | Description |
|---|---|---|
| GET | `/` | Web UI (single-file HTML) |
| GET | `/status` | JSON: `{locked, wifi, ip, temp, humidity}` |
| POST | `/unlock` | Form `pin=<6-digit>`. 200 on match → unlock window opens; 403 otherwise |
| POST | `/config` | Form `ssid=…&psk=…`. AP-mode only. Saves to NVS and reboots |

Default PIN is `123456` ([src/config.h](src/config.h#L4)).

## Build, flash, monitor

```bash
# Pristine build
west build -p always -b esp32s3_devkitc/esp32s3/procpu app/elock

# Incremental
west build -b esp32s3_devkitc/esp32s3/procpu app/elock

# Flash (build dir auto-detected at /work/build)
west flash --skip-rebuild
```

Serial monitor — `west espressif monitor` requires a controlling TTY; if it fails, capture directly:

```bash
python3 -c "import serial,time,sys; s=serial.Serial('/dev/ttyACM0',115200,timeout=0.5);
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False); time.sleep(0.1);
end=time.time()+15
while time.time()<end:
    d=s.read(4096)
    if d: sys.stdout.write(d.decode('utf-8','replace')); sys.stdout.flush()"
```

## First-time setup

1. Flash and power on. With no saved credentials the device boots into AP mode.
2. Connect to the `SmartLock-Setup` open network from a phone or laptop.
3. Browse to `http://192.168.4.1/`, open the WiFi config link, submit SSID + password.
4. The device saves credentials to NVS and reboots into STA mode. The PIN page is then served on the device's local IP.

If STA association fails 5 times in a row, the device falls back to AP mode so credentials can be re-entered.

## Layout

```
app/elock/
├── CMakeLists.txt
├── prj.conf
├── app.overlay              # board-level peripherals (lock GPIO, button, WiFi STA+AP)
├── sections-rom.ld          # iterable section for HTTP resources
├── socs/esp32s3_procpu.overlay
└── src/
    ├── main.c               # spawns module init only
    ├── config.h             # PIN, GPIO, WiFi defaults, NVS keys
    ├── lock_ctrl.{c,h}      # lock GPIO + auto-relock thread
    ├── button.{c,h}         # GPIO0 polling + debounce
    ├── wifi_manager.{c,h}   # STA/AP state machine, NVS-backed creds
    ├── http_server.{c,h}    # routes: /, /status, /unlock, /config
    └── web/index.html       # embedded UI
```

See [PROGRESS.md](PROGRESS.md) for module responsibilities, threading model, and what's been verified on hardware. See [CLAUDE.md](CLAUDE.md) for environmental gotchas encountered while bringing this up on Zephyr 4.x.
